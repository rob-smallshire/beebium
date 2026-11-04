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

#include "app/VideoSidebar.hpp"

#include <QButtonGroup>
#include <QColorDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

QColor to_qcolor(const Rgb& colour) {
    return QColor(colour.r, colour.g, colour.b);
}

Rgb to_rgb(const QColor& colour) {
    return Rgb{static_cast<std::uint8_t>(colour.red()), static_cast<std::uint8_t>(colour.green()),
               static_cast<std::uint8_t>(colour.blue())};
}

QFrame* divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

// A section's name above its control: smaller, in the secondary colour.
QLabel* heading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}

// Two or more choices side by side, one of them chosen: the macOS
// segmented picker's part.
QButtonGroup* segmented(const QStringList& choices, QWidget* parent, QVBoxLayout* into) {
    auto* group = new QButtonGroup(parent);
    group->setExclusive(true);
    auto* row = new QHBoxLayout;
    row->setSpacing(0);
    for (int i = 0; i < choices.size(); ++i) {
        auto* choice = new QToolButton(parent);
        choice->setText(choices[i]);
        choice->setCheckable(true);
        choice->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
        choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(choice, i);
        row->addWidget(choice);
    }
    into->addLayout(row);
    return group;
}

}  // namespace

VideoSidebar::VideoSidebar(QWidget* parent)
    : QWidget(parent),
      choose_colour_([this](const QColor& current, const QString& title) -> std::optional<QColor> {
          const QColor chosen = QColorDialog::getColor(current, this, title);
          return chosen.isValid() ? std::optional<QColor>(chosen) : std::nullopt;
      }) {
    setObjectName(QStringLiteral("videoMode"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    layout->addWidget(heading(tr("Pixels"), this));
    pixels_ = segmented({tr("Authentic"), tr("Crisp")}, this, layout);
    connect(pixels_, &QButtonGroup::idClicked, this, [this](int id) {
        change([id](VideoSettings& s) {
            s.pixels = id == 0 ? PixelShape::authentic : PixelShape::crisp;
        });
    });
    layout->addWidget(divider(this));

    layout->addWidget(heading(tr("Display Style"), this));
    style_ = segmented({tr("Standard"), tr("Debug")}, this, layout);
    connect(style_, &QButtonGroup::idClicked, this, [this](int id) {
        change([id](VideoSettings& s) {
            s.style = id == 0 ? DisplayStyle::standard : DisplayStyle::debug;
        });
    });

    // Standard's options; Debug has none, so the section goes with it.
    edge_margin_ = new QWidget(this);
    auto* margin_layout = new QVBoxLayout(edge_margin_);
    margin_layout->setContentsMargins(0, 0, 0, 0);
    margin_layout->addWidget(divider(edge_margin_));
    margin_layout->addWidget(heading(tr("Edge margin"), edge_margin_));
    auto* rows = new QGridLayout;
    rows->setColumnStretch(1, 1);
    rows->addWidget(new QLabel(tr("Size"), edge_margin_), 0, 0);
    margin_size_ = new QSpinBox(edge_margin_);
    margin_size_->setObjectName(QStringLiteral("edgeMarginSize"));
    margin_size_->setRange(0, VideoSettings::MAX_EDGE_MARGIN_PERCENT);
    margin_size_->setSuffix(QStringLiteral("%"));
    margin_size_->setFocusPolicy(Qt::ClickFocus);
    connect(margin_size_, &QSpinBox::valueChanged, this, [this](int percent) {
        change([percent](VideoSettings& s) { s.edge_margin_percent = percent; });
    });
    rows->addWidget(margin_size_, 0, 2);
    margin_size_reset_ = reset_button(QStringLiteral("edgeMarginSizeReset"), [this] {
        change([](VideoSettings& s) { s.edge_margin_percent = VideoSettings{}.edge_margin_percent; });
    });
    rows->addWidget(margin_size_reset_, 0, 3);
    rows->addWidget(new QLabel(tr("Colour"), edge_margin_), 1, 0);
    margin_colour_ = colour_well(QStringLiteral("edgeMarginColour"), tr("Edge Margin Colour"),
                                 &VideoSettings::edge_margin_colour,
                                 VideoSettings{}.edge_margin_colour);
    rows->addWidget(margin_colour_, 1, 2, Qt::AlignRight);
    margin_colour_reset_ = reset_button(QStringLiteral("edgeMarginColourReset"), [this] {
        change([](VideoSettings& s) { s.edge_margin_colour = VideoSettings{}.edge_margin_colour; });
    });
    rows->addWidget(margin_colour_reset_, 1, 3);
    margin_layout->addLayout(rows);
    layout->addWidget(edge_margin_);

    layout->addWidget(divider(this));
    // Laid out as the margin's rows, so the wells and resets line up.
    auto* boxing_row = new QGridLayout;
    boxing_row->setColumnStretch(1, 1);
    boxing_row->addWidget(new QLabel(tr("Boxing Colour"), this), 0, 0);
    boxing_ = colour_well(QStringLiteral("boxingColour"), tr("Boxing Colour"),
                          &VideoSettings::boxing, VideoSettings{}.boxing);
    boxing_row->addWidget(boxing_, 0, 2, Qt::AlignRight);
    boxing_reset_ = reset_button(QStringLiteral("boxingColourReset"), [this] {
        change([](VideoSettings& s) { s.boxing = VideoSettings{}.boxing; });
    });
    boxing_row->addWidget(boxing_reset_, 0, 3);
    layout->addLayout(boxing_row);
    layout->addStretch(1);
    present();
}

void VideoSidebar::set_settings(const VideoSettings& settings) {
    settings_ = settings;
    present();
}

void VideoSidebar::change(const std::function<void(VideoSettings&)>& edit) {
    VideoSettings next = settings_;
    edit(next);
    if (next == settings_) {
        return;
    }
    settings_ = next;
    present();
    Q_EMIT changed(settings_);
}

QToolButton* VideoSidebar::colour_well(const QString& object_name, const QString& title,
                                       Rgb VideoSettings::*member, Rgb fallback) {
    auto* well = new QToolButton(this);
    well->setObjectName(object_name);
    well->setFocusPolicy(Qt::NoFocus);
    well->setIconSize(QSize(36, 14));
    well->setToolTip(title);
    connect(well, &QToolButton::clicked, this, [this, title, member] {
        if (const auto chosen = choose_colour_(to_qcolor(settings_.*member), title)) {
            const Rgb colour = to_rgb(*chosen);  // opaque: any alpha is dropped
            change([member, colour](VideoSettings& s) { s.*member = colour; });
        }
    });
    // As the macOS colour well's context menu.
    well->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(well, &QWidget::customContextMenuRequested, this,
            [this, well, member, fallback](const QPoint& at) {
                QMenu menu(well);
                QAction* reset = menu.addAction(tr("Reset to Default"));
                reset->setEnabled(!(settings_.*member == fallback));
                if (menu.exec(well->mapToGlobal(at)) == reset) {
                    change([member, fallback](VideoSettings& s) { s.*member = fallback; });
                }
            });
    return well;
}

QToolButton* VideoSidebar::reset_button(const QString& object_name,
                                        const std::function<void()>& reset) {
    auto* button = new QToolButton(this);
    button->setObjectName(object_name);
    button->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo"),
                                     style()->standardIcon(QStyle::SP_BrowserReload)));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tr("Reset to default"));
    connect(button, &QToolButton::clicked, this, reset);
    return button;
}

void VideoSidebar::present() {
    const VideoSettings defaults;
    pixels_->button(settings_.pixels == PixelShape::authentic ? 0 : 1)->setChecked(true);
    style_->button(settings_.style == DisplayStyle::standard ? 0 : 1)->setChecked(true);
    edge_margin_->setVisible(settings_.style == DisplayStyle::standard);
    {
        const QSignalBlocker quiet(margin_size_);
        margin_size_->setValue(settings_.edge_margin_percent);
    }
    margin_size_reset_->setEnabled(settings_.edge_margin_percent != defaults.edge_margin_percent);
    margin_colour_reset_->setEnabled(!(settings_.edge_margin_colour == defaults.edge_margin_colour));
    boxing_reset_->setEnabled(!(settings_.boxing == defaults.boxing));
    for (auto [well, colour] : {std::pair{margin_colour_, settings_.edge_margin_colour},
                                std::pair{boxing_, settings_.boxing}}) {
        QPixmap swatch(well->iconSize());
        swatch.fill(to_qcolor(colour));
        QPainter frame(&swatch);
        frame.setPen(palette().color(QPalette::Mid));
        frame.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
        frame.end();
        well->setIcon(QIcon(swatch));
    }
}

}  // namespace beebium::linux_client
