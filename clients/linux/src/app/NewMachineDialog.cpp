// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#include "app/NewMachineDialog.hpp"

#include "app/ClientSettings.hpp"
#include "app/SidebarMode.hpp"
#include "app/configuration/AttachmentChooser.hpp"
#include "app/configuration/SectionWidgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

namespace {

constexpr int PRESET_INDEX_ROLE = Qt::UserRole;

std::vector<NewMachineDialog::SectionMaker>& section_makers() {
    static std::vector<NewMachineDialog::SectionMaker> makers;
    return makers;
}

QLabel* secondary(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}

// Where a draft waits for a launch: removed when the client ends.
QString draft_dirpath() {
    const QString dirpath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(QStringLiteral("beebium-drafts"));
    QDir().mkpath(dirpath);
    return dirpath;
}

}  // namespace

void NewMachineDialog::add_section_maker(SectionMaker maker) {
    section_makers().push_back(std::move(maker));
}

NewMachineDialog::NewMachineDialog(NewMachineHost host, ClientSettings* settings, QWidget* parent)
    : QDialog(parent), host_(std::move(host)), settings_(settings),
      resolver_(std::make_unique<PresetResolver>(host_.resolve_runner)) {
    setWindowTitle(tr("New Machine"));
    setObjectName(QStringLiteral("newMachineDialog"));
    resize(600, 640);
    auto* layout = new QVBoxLayout(this);

    banner_ = new QLabel(this);
    banner_->setWordWrap(true);
    banner_->hide();
    layout->addWidget(banner_);

    auto* form = new QFormLayout;
    picker_ = new QComboBox(this);
    picker_->setObjectName(QStringLiteral("presetPicker"));
    form->addRow(tr("Machine &Preset:"), picker_);
    preset_description_ = secondary(this);
    form->addRow(QString(), preset_description_);
    description_ = new QLineEdit(this);
    description_->setObjectName(QStringLiteral("machineDescription"));
    description_->setPlaceholderText(tr("Station {econet-station} (AUN, Model B)"));
    form->addRow(tr("Machine &description:"), description_);
    layout->addLayout(form);

    // The configuration editor: its sections on the left, one on the right.
    auto* editor = new QHBoxLayout;
    sections_list_ = new QListWidget(this);
    sections_list_->setObjectName(QStringLiteral("sections"));
    sections_list_->setFixedWidth(150);
    sections_stack_ = new QStackedWidget(this);
    editor->addWidget(sections_list_);
    editor->addWidget(sections_stack_, 1);
    layout->addLayout(editor, 1);
    connect(sections_list_, &QListWidget::currentRowChanged, sections_stack_,
            &QStackedWidget::setCurrentIndex);

    auto* save_row = new QHBoxLayout;
    save_as_ = new QCheckBox(tr("Save as new preset"), this);
    save_as_name_ = new QLineEdit(this);
    save_as_name_->setObjectName(QStringLiteral("saveAsName"));
    save_as_name_->setEnabled(false);
    save_row->addWidget(save_as_);
    save_row->addWidget(save_as_name_, 1);
    layout->addLayout(save_row);
    connect(save_as_, &QCheckBox::toggled, save_as_name_, &QWidget::setEnabled);

    diagnostics_list_ = new QListWidget(this);
    diagnostics_list_->setObjectName(QStringLiteral("diagnostics"));
    diagnostics_list_->setFocusPolicy(Qt::NoFocus);
    diagnostics_list_->setMaximumHeight(110);
    diagnostics_list_->hide();
    layout->addWidget(diagnostics_list_);

    launch_error_ = new QLabel(this);
    launch_error_->setObjectName(QStringLiteral("launchError"));
    launch_error_->setWordWrap(true);
    launch_error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    launch_error_->hide();
    layout->addWidget(launch_error_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    create_ = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    create_->setDefault(true);
    create_->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(picker_, &QComboBox::activated, this, &NewMachineDialog::choose_preset);
    connect(description_, &QLineEdit::editingFinished, this, [this] {
        const QString text = description_->text().trimmed();
        const auto now = draft_.get("/machine_description");
        const QString before = now && now->is_string() ? QString::fromStdString(now->get<std::string>())
                                                       : QString();
        if (text == before) {
            return;
        }
        // Blank is no subtitle: the key goes, never "".
        if (text.isEmpty()) {
            draft_.unset("/machine_description");
        } else {
            draft_.set("/machine_description", text.toStdString());
        }
        draft_edited();
    });
    connect(resolver_.get(), &PresetResolver::resolved, this, &NewMachineDialog::on_resolved);
    connect(resolver_.get(), &PresetResolver::failed, this, [this](const QString& message) {
        loading_ = false;
        settled_ = false;
        show_launch_error(message);
        create_->setEnabled(false);
    });
    reload();
}

NewMachineDialog::~NewMachineDialog() = default;

std::optional<QString> NewMachineDialog::choose(const FormBrowse& browse) {
    if (chooser_) {
        return chooser_(browse);
    }
    const QString title = QString::fromStdString(browse.label);
    const QString current = QString::fromStdString(browse.current);
    QString chosen;
    switch (browse.chooser) {
    case FormBrowse::Chooser::open_file:
        chosen = QFileDialog::getOpenFileName(this, title, current);
        break;
    case FormBrowse::Chooser::save_file:
        chosen = QFileDialog::getSaveFileName(this, title, current);
        break;
    case FormBrowse::Chooser::directory:
        chosen = QFileDialog::getExistingDirectory(this, title, current);
        break;
    }
    return chosen.isEmpty() ? std::nullopt : std::optional<QString>(chosen);
}

QString NewMachineDialog::launch_error() const {
    return launch_error_->isHidden() ? QString() : launch_error_->text();
}

void NewMachineDialog::reload() {
    picker_->clear();
    presets_.clear();
    preset_.reset();
    create_->setEnabled(false);
    show_launch_error({});
    if (!host_.list_presets) {
        return;
    }
    QPointer<NewMachineDialog> guard(this);
    host_.list_presets([guard](std::vector<PresetListing> presets, QString error) {
        if (!guard) {
            return;
        }
        guard->banner_->setText(error);
        guard->banner_->setVisible(!error.isEmpty());
        guard->presets_ = std::move(presets);
        // Built-in first, then the user's, under headings.
        const QString last = guard->settings_ ? guard->settings_->store()
                                                    .value(QString::fromLatin1(LAST_PRESET_KEY))
                                                    .toString()
                                              : QString();
        int chosen = -1;
        for (const bool built_in : {true, false}) {
            bool headed = false;
            for (std::size_t i = 0; i < guard->presets_.size(); ++i) {
                const PresetListing& preset = guard->presets_[i];
                if (preset.built_in() != built_in) {
                    continue;
                }
                if (!headed) {
                    guard->picker_->addItem(built_in ? tr("Built-in") : tr("My Presets"));
                    const int heading = guard->picker_->count() - 1;
                    guard->picker_->setItemData(heading, QVariant(), PRESET_INDEX_ROLE);
                    if (auto* model = qobject_cast<QStandardItemModel*>(guard->picker_->model())) {
                        model->item(heading)->setEnabled(false);
                    }
                    headed = true;
                }
                guard->picker_->addItem(QStringLiteral("  ") + preset.name);
                guard->picker_->setItemData(guard->picker_->count() - 1,
                                            static_cast<int>(i), PRESET_INDEX_ROLE);
            }
        }
        // The last one chosen, else the first built-in.
        for (int row = 0; row < guard->picker_->count(); ++row) {
            const QVariant index = guard->picker_->itemData(row, PRESET_INDEX_ROLE);
            if (!index.isValid()) {
                continue;
            }
            if (chosen < 0 ||
                guard->presets_[static_cast<std::size_t>(index.toInt())].id == last) {
                chosen = row;
            }
            if (guard->presets_[static_cast<std::size_t>(index.toInt())].id == last) {
                break;
            }
        }
        if (chosen >= 0) {
            guard->picker_->setCurrentIndex(chosen);
            guard->choose_preset(chosen);
        }
    });
}

void NewMachineDialog::choose_preset(int row) {
    const QVariant index = picker_->itemData(row, PRESET_INDEX_ROLE);
    if (!index.isValid()) {
        return;
    }
    preset_ = presets_[static_cast<std::size_t>(index.toInt())];
    preset_description_->setText(preset_->description);
    preset_description_->setVisible(!preset_->description.isEmpty());
    if (settings_) {
        settings_->store().setValue(QString::fromLatin1(LAST_PRESET_KEY), preset_->id);
    }
    create_->setEnabled(false);
    show_launch_error({});
    loading_ = true;
    settled_ = false;
    resolver_->set_executable(host_.executable ? host_.executable(preset_->model) : QString());
    resolver_->resolve_preset(preset_->filepath.isEmpty() ? preset_->id : preset_->filepath);
    // The model's schema, once per model.
    const QString model = preset_->model;
    if (schemas_.count(model)) {
        schema_ = schemas_.at(model);
        return;
    }
    schema_.reset();
    if (host_.describe_schema) {
        QPointer<NewMachineDialog> guard(this);
        host_.describe_schema(model, [guard, model](std::optional<PresetSchema> schema) {
            if (!guard || !schema) {
                return;
            }
            guard->schemas_.emplace(model, *schema);
            if (guard->preset_ && guard->preset_->model == model) {
                guard->schema_ = std::move(schema);
                if (!guard->loading_) {
                    guard->rebuild_sections();
                }
            }
        });
    }
}

void NewMachineDialog::on_resolved(const PresetResolution& resolution) {
    diagnostics_ = resolution.diagnostics;
    if (loading_) {
        // The preset's own resolution: the draft starts from it.
        loading_ = false;
        if (resolution.preset) {
            draft_ = MachineDraft(*resolution.preset);
        }
        const auto description = draft_.get("/machine_description");
        description_->setText(description && description->is_string()
                                  ? QString::fromStdString(description->get<std::string>())
                                  : QString());
        rebuild_sections();
    } else {
        for (ConfigurationSection* section : sections_) {
            section->set_diagnostics(diagnostics_);
        }
    }
    settled_ = true;
    present_diagnostics();
    update_markers();
}

void NewMachineDialog::draft_edited() {
    settled_ = false;
    create_->setEnabled(false);
    update_markers();
    resolver_->resolve(draft_);
}

void NewMachineDialog::present_diagnostics() {
    diagnostics_list_->clear();
    std::vector<PresetDiagnostic> ordered = diagnostics_;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const PresetDiagnostic& a, const PresetDiagnostic& b) {
                         return a.error() && !b.error();
                     });
    for (const auto& d : ordered) {
        QString text = QString::fromStdString(d.message);
        if (!d.location.empty()) {
            text += QLatin1Char('\n') + QString::fromStdString(d.location);
        }
        auto* item = new QListWidgetItem(
            style()->standardIcon(d.error() ? QStyle::SP_MessageBoxCritical
                                            : QStyle::SP_MessageBoxWarning),
            text, diagnostics_list_);
        item->setData(Qt::UserRole, QString::fromStdString(d.path));
    }
    diagnostics_list_->setVisible(!ordered.empty());
    const bool blocked = std::any_of(diagnostics_.begin(), diagnostics_.end(),
                                     [](const PresetDiagnostic& d) { return d.error(); });
    create_->setEnabled(settled_ && preset_.has_value() && !blocked);
}

void NewMachineDialog::rebuild_sections() {
    const int current = sections_list_->currentRow();
    sections_list_->clear();
    for (ConfigurationSection* section : sections_) {
        sections_stack_->removeWidget(section);
        section->deleteLater();
    }
    sections_.clear();
    if (!schema_) {
        return;
    }
    auto makers = configuration_section_makers();
    makers.insert(makers.end(), section_makers().begin(), section_makers().end());
    for (const auto& make : makers) {
        if (ConfigurationSection* section = make(*this)) {
            sections_.push_back(section);
        }
    }
    // In the sidebar's order.
    std::stable_sort(sections_.begin(), sections_.end(),
                     [](ConfigurationSection* a, ConfigurationSection* b) {
                         return static_cast<int>(a->mode()) < static_cast<int>(b->mode());
                     });
    for (ConfigurationSection* section : sections_) {
        new QListWidgetItem(sidebar_mode_icon(section->mode()), sidebar_mode_label(section->mode()),
                            sections_list_);
        sections_stack_->addWidget(section);
        connect(section, &ConfigurationSection::edited, this, &NewMachineDialog::draft_edited);
        section->reload();
        section->set_diagnostics(diagnostics_);
    }
    if (!sections_.empty()) {
        sections_list_->setCurrentRow(std::clamp(current, 0, static_cast<int>(sections_.size()) - 1));
    }
    update_markers();
}

void NewMachineDialog::update_markers() {
    for (int row = 0; row < sections_list_->count() && row < static_cast<int>(sections_.size());
         ++row) {
        ConfigurationSection* section = sections_[static_cast<std::size_t>(row)];
        bool changed = false;
        for (const auto& pointer : section->pointers()) {
            changed = changed || draft_.changed(pointer);
        }
        const QString label = sidebar_mode_label(section->mode());
        sections_list_->item(row)->setText(changed ? label + QStringLiteral(" •") : label);
    }
}

void NewMachineDialog::accept() {
    create();
}

void NewMachineDialog::reject() {
    QDialog::reject();
    // Next time, the chosen preset afresh.
    draft_ = MachineDraft();
    reload();
}

void NewMachineDialog::create() {
    if (!create_->isEnabled() || !preset_) {
        return;
    }
    show_launch_error({});
    if (!save_as_->isChecked() || save_as_name_->text().trimmed().isEmpty()) {
        launch_draft();
        return;
    }
    // Saved as a new preset first: a failure stops the launch.
    MachineDraft named = draft_;
    named.set("/name", save_as_name_->text().trimmed().toStdString());
    const QString filepath = QDir(draft_dirpath())
                                 .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                           QStringLiteral(".json"));
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        show_launch_error(tr("The new preset could not be written."));
        return;
    }
    file.write(QByteArray::fromStdString(named.serialise()));
    file.close();
    create_->setEnabled(false);
    QPointer<NewMachineDialog> guard(this);
    host_.import_preset(preset_->model, filepath, [guard, filepath](bool ok, QString error) {
        QFile::remove(filepath);
        if (!guard) {
            return;
        }
        if (!ok) {
            guard->show_launch_error(error.isEmpty() ? tr("The new preset could not be saved.")
                                                     : error);
            guard->present_diagnostics();
            return;
        }
        guard->launch_draft();
    });
}

void NewMachineDialog::launch_draft() {
    const QString filepath = QDir(draft_dirpath())
                                 .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                           QStringLiteral(".json"));
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        show_launch_error(tr("The machine could not be written for the server to start."));
        return;
    }
    file.write(QByteArray::fromStdString(draft_.serialise()));
    file.close();
    if (host_.launch) {
        host_.launch(*preset_, filepath);
    }
    QDialog::accept();
    draft_ = MachineDraft();
    reload();
}

std::optional<std::string> NewMachineDialog::pick(const AttachmentChoice& choice) {
    if (attachment_picker_) {
        return attachment_picker_(choice);
    }
    AttachmentChooser chooser(choice, this);
    if (chooser.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return chooser.chosen();
}

const QStringList& NewMachineDialog::serial_ports() {
    if (!serial_ports_asked_ && host_.list_serial_ports) {
        serial_ports_asked_ = true;
        QPointer<NewMachineDialog> guard(this);
        host_.list_serial_ports([guard](QStringList paths) {
            if (guard) {
                guard->serial_ports_ = std::move(paths);
                Q_EMIT guard->serial_ports_listed();
            }
        });
    }
    return serial_ports_;
}

const RomDescription* NewMachineDialog::rom_description(const std::string& image_uri) {
    if (image_uri.empty()) {
        return nullptr;
    }
    const auto found = roms_.find(image_uri);
    if (found != roms_.end()) {
        return found->second ? &*found->second : nullptr;
    }
    roms_.emplace(image_uri, std::nullopt);
    if (!host_.describe_rom || !preset_) {
        return nullptr;
    }
    QPointer<NewMachineDialog> guard(this);
    host_.describe_rom(preset_->model, path_of_uri(image_uri),
                       [guard, image_uri](std::optional<RomDescription> rom) {
                           if (!guard || !rom) {
                               return;
                           }
                           guard->roms_[image_uri] = std::move(rom);
                           Q_EMIT guard->rom_described();
                       });
    // The answer may already be in.
    const auto answered = roms_.find(image_uri);
    return answered != roms_.end() && answered->second ? &*answered->second : nullptr;
}

void NewMachineDialog::show_launch_error(const QString& text) {
    launch_error_->setText(text);
    launch_error_->setVisible(!text.isEmpty());
}

}  // namespace beebium::linux_client
