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

#include "app/configuration/KeyboardSection.hpp"

#include "app/configuration/SectionWidgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

QLabel* subheading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont medium = label->font();
    medium.setWeight(QFont::Medium);
    label->setFont(medium);
    return label;
}

}  // namespace

KeyboardSection::KeyboardSection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("keyboardSection"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(section_heading(tr("Startup"), this));

    auto* mode_row = new QHBoxLayout;
    auto* mode_label = new QLabel(tr("Screen mode"), this);
    mode_row->addWidget(mode_label);
    screen_mode_ = new QComboBox(this);
    screen_mode_->setObjectName(QStringLiteral("startupScreenMode"));
    mode_label->setBuddy(screen_mode_);
    mode_row->addWidget(screen_mode_);
    mode_row->addStretch(1);
    layout->addLayout(mode_row);

    auto_boot_ = new QCheckBox(tr("Auto-boot"), this);
    auto_boot_->setObjectName(QStringLiteral("startupAutoBoot"));
    layout->addWidget(auto_boot_);
    auto_boot_description_ = section_caption(this);
    layout->addWidget(auto_boot_description_);

    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(rule);

    layout->addWidget(subheading(tr("Links"), this));
    links_layout_ = new QVBoxLayout;
    layout->addLayout(links_layout_);

    auto* byte_row = new QHBoxLayout;
    auto* hex_label = new QLabel(tr("Hex"), this);
    byte_row->addWidget(hex_label);
    hex_ = new QLineEdit(this);
    hex_->setObjectName(QStringLiteral("startupHex"));
    hex_->setMaximumWidth(hex_->fontMetrics().horizontalAdvance(QStringLiteral("0x000")) + 16);
    hex_label->setBuddy(hex_);
    byte_row->addWidget(hex_);
    auto* decimal_label = new QLabel(tr("Decimal"), this);
    byte_row->addWidget(decimal_label);
    decimal_ = new QLineEdit(this);
    decimal_->setObjectName(QStringLiteral("startupDecimal"));
    decimal_->setMaximumWidth(hex_->maximumWidth());
    decimal_label->setBuddy(decimal_);
    byte_row->addWidget(decimal_);
    byte_row->addStretch(1);
    layout->addLayout(byte_row);

    diagnostic_ = new DiagnosticLine(this);
    layout->addWidget(diagnostic_);
    layout->addStretch(1);

    connect(screen_mode_, &QComboBox::activated, this, [this](int index) {
        StartupLinks edited = links();
        edited.set_screen_mode(screen_mode_->itemData(index).toInt());
        write(edited);
    });
    connect(auto_boot_, &QCheckBox::clicked, this, [this](bool on) {
        StartupLinks edited = links();
        edited.set_auto_boot(on);
        write(edited);
    });
    // A refused entry shows the byte as it is.
    connect(hex_, &QLineEdit::editingFinished, this, [this] {
        StartupLinks edited = links();
        if (edited.set_hex(hex_->text().trimmed().toStdString()) && edited.byte() != links().byte()) {
            write(edited);
        } else {
            show_links();
        }
    });
    connect(decimal_, &QLineEdit::editingFinished, this, [this] {
        StartupLinks edited = links();
        if (edited.set_decimal(decimal_->text().trimmed().toStdString()) &&
            edited.byte() != links().byte()) {
            write(edited);
        } else {
            show_links();
        }
    });
}

StartupLinks KeyboardSection::links() const {
    return StartupLinks::from_json(*layout_,
                                   dialog_.draft().get("/startup").value_or(ordered_json::object()));
}

void KeyboardSection::write(const StartupLinks& links) {
    dialog_.draft().set("/startup", links.to_json());
    show_links();
    Q_EMIT edited();
}

void KeyboardSection::reload() {
    const auto& schema = dialog_.schema();
    layout_ = schema ? LinksLayout::from_schema(*schema) : std::nullopt;
    if (!layout_) {
        layout_ = LinksLayout{};
    }
    {
        const QSignalBlocker quiet(screen_mode_);
        screen_mode_->clear();
        int low = 0;
        int high = 7;
        if (schema && schema->section("startup")) {
            const auto& mode = (*schema->section("startup"))["screen_mode"];
            low = mode.value("min", low);
            high = mode.value("max", high);
        }
        for (int mode = low; mode <= high; ++mode) {
            screen_mode_->addItem(QString::number(mode), mode);
        }
    }
    QString behaviour = tr("reset or BREAK boots; SHIFT+reset or SHIFT+BREAK does not");
    for (const auto& bit : layout_->bits) {
        if (bit.bit == layout_->auto_boot_bit && !bit.description.empty()) {
            behaviour = QString::fromStdString(bit.description);
        }
    }
    auto_boot_description_->setText(behaviour);
    auto_boot_->setToolTip(behaviour);

    for (QWidget* row : link_rows_) {
        row->hide();
        row->deleteLater();
    }
    link_rows_.clear();
    link_boxes_.clear();
    for (const auto& bit : layout_->bits) {
        auto* row = new QWidget(this);
        auto* rows = new QVBoxLayout(row);
        rows->setContentsMargins(0, 0, 0, 0);
        auto* box = new QCheckBox(
            QStringLiteral("%1: %2").arg(QString::fromStdString(bit.link), QString::fromStdString(bit.label)),
            row);
        box->setObjectName(QStringLiteral("startupLinkBit%1").arg(bit.bit));
        rows->addWidget(box);
        if (!bit.description.empty()) {
            QLabel* description = section_caption(row);
            description->setText(QString::fromStdString(bit.description));
            rows->addWidget(description);
            box->setToolTip(description->text());
        }
        const int number = bit.bit;
        connect(box, &QCheckBox::clicked, this, [this, number](bool made) {
            StartupLinks edited = links();
            edited.set_link_made(number, made);
            write(edited);
        });
        links_layout_->addWidget(row);
        link_rows_.push_back(row);
        link_boxes_.emplace_back(bit.bit, box);
    }
    show_links();
}

void KeyboardSection::show_links() {
    const StartupLinks now = links();
    {
        const QSignalBlocker quiet(screen_mode_);
        screen_mode_->setCurrentIndex(screen_mode_->findData(now.screen_mode()));
    }
    auto_boot_->setChecked(now.auto_boot());
    for (auto& [bit, box] : link_boxes_) {
        box->setChecked(now.link_made(bit));
    }
    hex_->setText(QString::fromStdString(now.hex()));
    decimal_->setText(QString::number(now.byte()));
}

QCheckBox* KeyboardSection::link(int bit) const {
    for (const auto& [b, box] : link_boxes_) {
        if (b == bit) {
            return box;
        }
    }
    return nullptr;
}

void KeyboardSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostic_->show_for(diagnostics, "/startup");
}

}  // namespace beebium::linux_client
