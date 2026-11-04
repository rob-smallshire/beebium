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
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

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

VideoSidebar::VideoSidebar(QWidget* parent) : QWidget(parent) {
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
    margin_size_reset_ = make_reset_button(this, QStringLiteral("edgeMarginSizeReset"));
    connect(margin_size_reset_, &QToolButton::clicked, this, [this] {
        change([](VideoSettings& s) { s.edge_margin_percent = VideoSettings{}.edge_margin_percent; });
    });
    rows->addWidget(margin_size_reset_, 0, 3);
    rows->addWidget(new QLabel(tr("Colour"), edge_margin_), 1, 0);
    margin_colour_ = new ColourChoice(QStringLiteral("edgeMarginColour"), tr("Edge Margin Colour"),
                                      VideoSettings{}.edge_margin_colour, this);
    connect(margin_colour_, &ColourChoice::chosen, this,
            [this](Rgb colour) { change([colour](VideoSettings& s) { s.edge_margin_colour = colour; }); });
    rows->addWidget(margin_colour_->well(), 1, 2, Qt::AlignRight);
    rows->addWidget(margin_colour_->reset(), 1, 3);
    margin_layout->addLayout(rows);
    layout->addWidget(edge_margin_);

    layout->addWidget(divider(this));
    // Laid out as the margin's rows, so the wells and resets line up.
    auto* boxing_row = new QGridLayout;
    boxing_row->setColumnStretch(1, 1);
    boxing_row->addWidget(new QLabel(tr("Boxing Colour"), this), 0, 0);
    boxing_ = new ColourChoice(QStringLiteral("boxingColour"), tr("Boxing Colour"),
                               VideoSettings{}.boxing, this);
    connect(boxing_, &ColourChoice::chosen, this,
            [this](Rgb colour) { change([colour](VideoSettings& s) { s.boxing = colour; }); });
    boxing_row->addWidget(boxing_->well(), 0, 2, Qt::AlignRight);
    boxing_row->addWidget(boxing_->reset(), 0, 3);
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

void VideoSidebar::set_colour_chooser(const ColourChooser& chooser) {
    margin_colour_->set_chooser(chooser);
    boxing_->set_chooser(chooser);
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
    margin_colour_->set_colour(settings_.edge_margin_colour);
    boxing_->set_colour(settings_.boxing);
}

}  // namespace beebium::linux_client
