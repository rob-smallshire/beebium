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

#include "app/MachinesPage.hpp"

#include "app/FileReveal.hpp"
#include "app/configuration/SectionWidgets.hpp"
#include "servers/PresetLibrary.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

const int ID_ROLE = Qt::UserRole;

// The name for a duplicate: a name of the user's own presets is refused.
std::optional<QString> ask_duplicate_name(QWidget* parent, const QString& suggested,
                                          const QStringList& taken) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Duplicate Preset"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(QObject::tr("&Name:"), &dialog);
    auto* field = new QLineEdit(suggested, &dialog);
    field->selectAll();
    label->setBuddy(field);
    row->addWidget(label);
    row->addWidget(field, 1);
    layout->addLayout(row);
    auto* refusal = section_caption(&dialog);
    layout->addWidget(refusal);
    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* commit = buttons->addButton(QObject::tr("Duplicate"), QDialogButtonBox::AcceptRole);
    commit->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    const auto judge = [=] {
        const QString name = field->text().trimmed();
        const bool used = taken.contains(name);
        refusal->setText(used ? QObject::tr("A preset named “%1” already exists.").arg(name)
                              : QString());
        refusal->setVisible(used);
        commit->setEnabled(!name.isEmpty() && !used);
    };
    QObject::connect(field, &QLineEdit::textChanged, &dialog, judge);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    judge();
    dialog.resize(420, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return field->text().trimmed();
}

QString failure(const QString& sentence, const QString& server) {
    return server.trimmed().isEmpty() ? sentence : sentence + QLatin1Char('\n') + server.trimmed();
}

}  // namespace

MachinesPage::MachinesPage(PresetLibraryHost host, QWidget* parent)
    : QWidget(parent), host_(std::move(host)) {
    setObjectName(QStringLiteral("machinesPage"));
    ask_name_ = [this](const QString& suggested, const QStringList& taken) {
        return ask_duplicate_name(this, suggested, taken);
    };
    ask_filepath_ = [this](const QString& suggested) -> std::optional<QString> {
        const QString filepath = QFileDialog::getSaveFileName(
            this, tr("Export Preset"), suggested, tr("Beebium presets (*.beebium-preset)"));
        return filepath.isEmpty() ? std::nullopt : std::optional<QString>(filepath);
    };
    confirm_delete_ = [this](const QString& name) {
        QMessageBox box(QMessageBox::Warning, tr("Delete Preset?"),
                        tr("Are you sure you want to delete “%1”? This action cannot be undone.")
                            .arg(name),
                        QMessageBox::NoButton, this);
        box.addButton(QMessageBox::Cancel);
        QPushButton* doom = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == doom;
    };
    reveal_ = [](const QString& filepath) { reveal_file(filepath); };

    auto* layout = new QVBoxLayout(this);
    pages_ = new QStackedWidget(this);
    state_ = new QLabel(pages_);
    state_->setAlignment(Qt::AlignCenter);
    state_->setWordWrap(true);
    state_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pages_->addWidget(state_);
    auto* library = new QWidget(pages_);
    auto* lists = new QVBoxLayout(library);
    lists->setContentsMargins(0, 0, 0, 0);
    lists->addWidget(section_heading(tr("Built-in Machine Presets"), library));
    built_in_ = new QListWidget(library);
    built_in_->setObjectName(QStringLiteral("builtInPresets"));
    lists->addWidget(built_in_, 3);
    lists->addWidget(section_heading(tr("My Machine Presets"), library));
    no_user_ = section_caption(library);
    no_user_->setText(tr("No custom presets yet"));
    lists->addWidget(no_user_);
    user_ = new QListWidget(library);
    user_->setObjectName(QStringLiteral("userPresets"));
    lists->addWidget(user_, 2);
    pages_->addWidget(library);
    layout->addWidget(pages_, 1);

    auto* buttons = new QHBoxLayout;
    duplicate_ = new QPushButton(tr("D&uplicate..."), this);
    export_ = new QPushButton(tr("E&xport..."), this);
    delete_ = new QPushButton(tr("&Delete..."), this);
    reveal_button_ = new QPushButton(tr("Open Containing &Folder"), this);
    buttons->addWidget(duplicate_);
    buttons->addWidget(export_);
    buttons->addWidget(delete_);
    buttons->addStretch(1);
    buttons->addWidget(reveal_button_);
    layout->addLayout(buttons);
    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPalette palette = error_->palette();
    palette.setColor(QPalette::WindowText, QColor(0xd0, 0x3b, 0x3b));
    error_->setPalette(palette);
    error_->hide();
    layout->addWidget(error_);

    for (QListWidget* list : {built_in_, user_}) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QListWidget::itemSelectionChanged, this, [this, list] {
            // One selection across the two lists.
            if (!list->selectedItems().isEmpty()) {
                QListWidget* other = list == built_in_ ? user_ : built_in_;
                const QSignalBlocker quiet(other);
                other->clearSelection();
                other->setCurrentItem(nullptr);
            }
            update_buttons();
        });
        connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint& at) {
            QListWidgetItem* item = list->itemAt(at);
            if (!item) {
                return;
            }
            list->setCurrentItem(item);
            if (const auto preset = selected()) {
                QMenu* menu = row_menu(*preset);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->popup(list->viewport()->mapToGlobal(at));
            }
        });
    }
    connect(duplicate_, &QPushButton::clicked, this, [this] {
        if (const auto preset = selected()) {
            duplicate(*preset);
        }
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        if (const auto preset = selected()) {
            export_preset(*preset);
        }
    });
    connect(delete_, &QPushButton::clicked, this, [this] {
        if (const auto preset = selected()) {
            remove(*preset);
        }
    });
    connect(reveal_button_, &QPushButton::clicked, this, [this] {
        if (const auto preset = selected()) {
            reveal(*preset);
        }
    });
    update_buttons();
    reload();
}

void MachinesPage::reload() {
    const std::uint64_t generation = ++generation_;
    if (presets_.empty()) {
        state_->setText(tr("Loading..."));
        pages_->setCurrentWidget(state_);
    }
    if (!host_.list_presets) {
        return;
    }
    QPointer<MachinesPage> guard(this);
    host_.list_presets([guard, generation](std::vector<PresetListing> presets, QString error) {
        if (!guard || generation != guard->generation_) {
            return;
        }
        if (!error.isEmpty()) {
            guard->presets_.clear();
            guard->state_->setText(error);
            guard->pages_->setCurrentWidget(guard->state_);
            guard->update_buttons();
            return;
        }
        guard->presets_ = std::move(presets);
        std::vector<PresetListing> built_in;
        std::vector<PresetListing> own;
        for (const auto& preset : guard->presets_) {
            (preset.built_in() ? built_in : own).push_back(preset);
        }
        guard->fill(guard->built_in_, library_order(built_in));
        guard->fill(guard->user_, library_order(own));
        guard->no_user_->setVisible(own.empty());
        guard->user_->setVisible(!own.empty());
        guard->pages_->setCurrentIndex(1);
        guard->update_buttons();
    });
}

void MachinesPage::fill(QListWidget* list, const std::vector<PresetListing>& presets) {
    const QString was = list->currentItem() ? list->currentItem()->data(ID_ROLE).toString() : QString();
    const QSignalBlocker quiet(list);
    list->clear();
    const QIcon icon = QIcon::fromTheme(QStringLiteral("computer"));
    for (const auto& preset : presets) {
        auto* item = new QListWidgetItem(list);
        item->setData(ID_ROLE, preset.id);
        item->setData(Qt::AccessibleTextRole, preset.name);
        auto* row = new QWidget(list);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(6, 3, 6, 3);
        auto* glyph = new QLabel(row);
        glyph->setPixmap(icon.pixmap(24, 24));
        line->addWidget(glyph);
        auto* text = new QVBoxLayout;
        text->setSpacing(0);
        auto* name = new QLabel(preset.name, row);
        name->setObjectName(QStringLiteral("presetName"));
        text->addWidget(name);
        if (!preset.description.isEmpty()) {
            QLabel* description = section_caption(row);
            description->setText(preset.description);
            text->addWidget(description);
        }
        line->addLayout(text, 1);
        item->setSizeHint(row->sizeHint());
        list->setItemWidget(item, row);
        if (preset.id == was) {
            list->setCurrentItem(item);
        }
    }
}

std::optional<PresetListing> MachinesPage::selected() const {
    for (QListWidget* list : {built_in_, user_}) {
        const auto items = list->selectedItems();
        if (!items.isEmpty()) {
            const QString id = items.first()->data(ID_ROLE).toString();
            for (const auto& preset : presets_) {
                if (preset.id == id) {
                    return preset;
                }
            }
        }
    }
    return std::nullopt;
}

QListWidgetItem* MachinesPage::row(const QString& id) const {
    for (QListWidget* list : {built_in_, user_}) {
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(ID_ROLE).toString() == id) {
                return list->item(i);
            }
        }
    }
    return nullptr;
}

void MachinesPage::update_buttons() {
    const auto preset = selected();
    duplicate_->setEnabled(preset.has_value() && host_.duplicate);
    export_->setEnabled(preset.has_value() && host_.export_preset);
    delete_->setEnabled(preset && !preset->built_in() && host_.remove);
    const QString refusal =
        preset && !preset->filepath.isEmpty() ? reveal_refusal(preset->filepath, true) : QString();
    reveal_button_->setEnabled(preset && !preset->filepath.isEmpty() && refusal.isEmpty());
    reveal_button_->setToolTip(refusal);
}

QMenu* MachinesPage::row_menu(const PresetListing& preset) {
    auto* menu = new QMenu;
    QAction* copy = menu->addAction(tr("Duplicate..."));
    copy->setEnabled(static_cast<bool>(host_.duplicate));
    connect(copy, &QAction::triggered, this, [this, preset] { duplicate(preset); });
    QAction* out = menu->addAction(tr("Export..."));
    out->setEnabled(static_cast<bool>(host_.export_preset));
    connect(out, &QAction::triggered, this, [this, preset] { export_preset(preset); });
    if (!preset.built_in()) {
        menu->addSeparator();
        QAction* gone = menu->addAction(tr("Delete..."));
        gone->setEnabled(static_cast<bool>(host_.remove));
        connect(gone, &QAction::triggered, this, [this, preset] { remove(preset); });
    }
    menu->addSeparator();
    QAction* folder = menu->addAction(tr("Open Containing Folder"));
    const QString refusal = preset.filepath.isEmpty() ? QString() : reveal_refusal(preset.filepath, true);
    folder->setEnabled(!preset.filepath.isEmpty() && refusal.isEmpty());
    folder->setToolTip(refusal);
    connect(folder, &QAction::triggered, this, [this, preset] { reveal(preset); });
    return menu;
}

void MachinesPage::duplicate(const PresetListing& source) {
    if (!host_.duplicate) {
        return;
    }
    QStringList taken;
    for (const auto& preset : presets_) {
        if (!preset.built_in()) {
            taken << preset.name;
        }
    }
    const auto name = ask_name_(duplicate_name(source.name, taken), taken);
    if (!name) {
        return;
    }
    show_error({});
    QPointer<MachinesPage> guard(this);
    host_.duplicate(source, *name, [guard](bool ok, QString error) {
        if (!guard) {
            return;
        }
        if (!ok) {
            guard->show_error(failure(tr("The preset could not be duplicated."), error));
            return;
        }
        guard->reload();
        Q_EMIT guard->changed();
    });
}

void MachinesPage::export_preset(const PresetListing& preset) {
    if (!host_.export_preset) {
        return;
    }
    const QString file_name = preset.filepath.isEmpty() ? preset.id + QStringLiteral(".beebium-preset")
                                                        : QFileInfo(preset.filepath).fileName();
    const auto filepath = ask_filepath_(QDir::home().filePath(file_name));
    if (!filepath) {
        return;
    }
    show_error({});
    QPointer<MachinesPage> guard(this);
    host_.export_preset(preset, *filepath, [guard](bool ok, QString error) {
        if (guard && !ok) {
            guard->show_error(failure(tr("The preset could not be exported."), error));
        }
    });
}

void MachinesPage::remove(const PresetListing& preset) {
    if (!host_.remove || preset.built_in() || !confirm_delete_(preset.name)) {
        return;
    }
    show_error({});
    QPointer<MachinesPage> guard(this);
    host_.remove(preset, [guard](bool ok, QString error) {
        if (!guard) {
            return;
        }
        if (!ok) {
            guard->show_error(failure(tr("The preset could not be deleted."), error));
            return;
        }
        guard->reload();
        Q_EMIT guard->changed();
    });
}

void MachinesPage::reveal(const PresetListing& preset) {
    if (!preset.filepath.isEmpty()) {
        reveal_(preset.filepath);
    }
}

void MachinesPage::show_error(const QString& text) {
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
}

}  // namespace beebium::linux_client
