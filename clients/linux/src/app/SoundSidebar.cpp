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

#include "app/SoundSidebar.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace beebium::linux_client {

namespace {

QSlider* make_slider(QWidget* parent, float value) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(0, SoundSidebar::SLIDER_STEPS);
    slider->setValue(static_cast<int>(std::lround(value * SoundSidebar::SLIDER_STEPS)));
    slider->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    return slider;
}

float slider_value(int position) {
    return static_cast<float>(position) / static_cast<float>(SoundSidebar::SLIDER_STEPS);
}

QFrame* divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

}  // namespace

QString health_text(const AudioHealth& health) {
    return QObject::tr("%1 underruns, %2 dropped")
        .arg(health.underrun_events)
        .arg(health.dropped_samples);
}

SoundSidebar::SoundSidebar(SessionAudio& audio, QWidget* parent)
    : QWidget(parent), audio_(audio) {
    setObjectName(QStringLiteral("soundSidebar"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 8, 12, 8);

    status_ = new QLabel(content);
    status_->setWordWrap(true);
    status_->setAlignment(Qt::AlignCenter);
    status_->setEnabled(false);  // the secondary text colour
    layout->addWidget(status_);

    mixer_ = new QWidget(content);
    auto* mixer_layout = new QVBoxLayout(mixer_);
    mixer_layout->setContentsMargins(0, 0, 0, 0);
    auto* master_row = new QHBoxLayout;
    auto* master_label = new QLabel(tr("Master"), mixer_);
    QFont bold = master_label->font();
    bold.setBold(true);
    master_label->setFont(bold);
    master_row->addWidget(master_label, 1);
    master_mute_ = make_mute(audio_.controls().muted.load());
    master_row->addWidget(master_mute_);
    mixer_layout->addLayout(master_row);
    master_slider_ = make_slider(mixer_, audio_.controls().master.load());
    master_slider_->setObjectName(QStringLiteral("masterVolume"));
    mixer_layout->addWidget(master_slider_);
    mixer_layout->addWidget(divider(mixer_));
    groups_layout_ = new QVBoxLayout;
    mixer_layout->addLayout(groups_layout_);
    layout->addWidget(mixer_);

    health_ = new QWidget(content);
    auto* health_layout = new QHBoxLayout(health_);
    health_layout->setContentsMargins(0, 8, 0, 0);
    auto* warning = new QLabel(health_);
    warning->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(16, 16));
    health_label_ = new QLabel(health_);
    health_label_->setEnabled(false);
    health_layout->addWidget(warning);
    health_layout->addWidget(health_label_, 1);
    health_->setToolTip(tr("Audio glitches this session: underruns (host too busy to keep the "
                           "buffer filled) and samples dropped on overflow. Zero means the "
                           "machine kept up."));
    health_->hide();
    layout->addWidget(health_);
    layout->addStretch(1);

    connect(master_slider_, &QSlider::valueChanged, this, [this](int position) {
        audio_.controls().master.store(slider_value(position));
    });
    connect(master_mute_, &QToolButton::clicked, this, [this] {
        const bool muted = !audio_.controls().muted.load();
        audio_.controls().muted.store(muted);
        show_muted(master_mute_, muted);
        update_greying();
    });
    connect(&audio_, &SessionAudio::loaded_changed, this, &SoundSidebar::rebuild);
    connect(&audio_, &SessionAudio::problem_changed, this, &SoundSidebar::rebuild);
    // The counters accumulate on the playback thread whether this is shown
    // or not; the poll only publishes them, and only while shown.
    poll_.setInterval(HEALTH_POLL_MS);
    connect(&poll_, &QTimer::timeout, this, &SoundSidebar::refresh_health);
    rebuild();
}

QToolButton* SoundSidebar::make_mute(bool muted) {
    auto* button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    show_muted(button, muted);
    return button;
}

void SoundSidebar::show_muted(QToolButton* button, bool muted) {
    button->setIcon(QIcon::fromTheme(muted ? QStringLiteral("audio-volume-muted")
                                           : QStringLiteral("audio-volume-high"),
                                     button->style()->standardIcon(
                                         muted ? QStyle::SP_MediaVolumeMuted
                                               : QStyle::SP_MediaVolume)));
    button->setToolTip(muted ? tr("Unmute") : tr("Mute"));
    button->setProperty("muted", muted);
}

void SoundSidebar::rebuild() {
    for (auto& group : groups_) {
        delete group.section;
    }
    groups_.clear();
    channels_.clear();

    const QString problem = audio_.problem();
    if (!audio_.loaded()) {
        const QString failed = audio_.stream_problem();
        status_->setText(failed.isEmpty() ? tr("Connecting...") : failed);
        status_->show();
        mixer_->hide();
        return;
    }
    // Loaded but silent (no output device, or this build has none): the
    // mixer is still shown, under a line saying why nothing is heard.
    status_->setText(problem);
    status_->setVisible(!problem.isEmpty());
    mixer_->show();

    const AudioLayout& layout = audio_.layout();
    MixerControls& controls = audio_.controls();
    channels_.assign(layout.channels.size(), nullptr);
    for (std::size_t g = 0; g < layout.groups.size(); ++g) {
        const auto& group = layout.groups[g];
        GroupSection section;
        section.section = new QWidget(mixer_);
        auto* section_layout = new QVBoxLayout(section.section);
        section_layout->setContentsMargins(0, 8, 0, 0);
        auto* header = new QHBoxLayout;
        auto* name = new QLabel(QString::fromStdString(group.name), section.section);
        name->setToolTip(QString::fromStdString(group.description));
        QFont medium = name->font();
        medium.setWeight(QFont::Medium);
        name->setFont(medium);
        name->setEnabled(false);  // secondary, as the macOS header
        header->addWidget(name, 1);
        section.mute = make_mute(controls.group_muted[g].load());
        section.mute->setParent(section.section);
        section.mute->setToolTip(section.mute->property("muted").toBool() ? tr("Unmute group")
                                                                          : tr("Mute group"));
        header->addWidget(section.mute);
        section_layout->addLayout(header);
        section.slider = make_slider(section.section, controls.group_volume[g].load());
        section_layout->addWidget(section.slider);
        connect(section.slider, &QSlider::valueChanged, this, [this, g](int position) {
            audio_.controls().group_volume[g].store(slider_value(position));
        });
        connect(section.mute, &QToolButton::clicked, this, [this, g] {
            const bool muted = !audio_.controls().group_muted[g].load();
            audio_.controls().group_muted[g].store(muted);
            show_muted(groups_[g].mute, muted);
            groups_[g].mute->setToolTip(muted ? tr("Unmute group") : tr("Mute group"));
            update_greying();
        });
        // Per-channel sliders beneath the sub-master: the SN76489's.
        for (std::size_t c : group.channels) {
            auto* row = new QHBoxLayout;
            auto* channel_name =
                new QLabel(QString::fromStdString(layout.channels[c].name), section.section);
            channel_name->setMinimumWidth(50);
            channel_name->setEnabled(false);
            auto* slider = make_slider(section.section, controls.channel_volume[c].load());
            auto* percent = new QLabel(section.section);
            percent->setMinimumWidth(36);
            percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            percent->setEnabled(false);
            const auto show_percent = [percent](int position) {
                percent->setText(QStringLiteral("%1%").arg(position * 100 / SLIDER_STEPS));
            };
            show_percent(slider->value());
            connect(slider, &QSlider::valueChanged, this, [this, c, show_percent](int position) {
                audio_.controls().channel_volume[c].store(slider_value(position));
                show_percent(position);
            });
            row->addWidget(channel_name);
            row->addWidget(slider, 1);
            row->addWidget(percent);
            section_layout->addLayout(row);
            channels_[c] = slider;
        }
        groups_layout_->addWidget(section.section);
        groups_.push_back(section);
    }
    update_greying();
    refresh_health();
}

void SoundSidebar::update_greying() {
    // A slider greys while its group or the master is muted.
    const bool master_muted = audio_.controls().muted.load();
    master_slider_->setEnabled(!master_muted);
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        const bool muted = master_muted || audio_.controls().group_muted[g].load();
        groups_[g].slider->setEnabled(!muted);
    }
    for (QSlider* channel : channels_) {
        if (channel) {
            channel->setEnabled(!master_muted);
        }
    }
}

void SoundSidebar::refresh_health() {
    const AudioHealth health = audio_.health();
    if (health == shown_health_ && !health_->isHidden() == health.glitched()) {
        return;  // only a change is redrawn
    }
    shown_health_ = health;
    health_label_->setText(health_text(health));
    health_->setVisible(health.glitched());
}

void SoundSidebar::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh_health();
    poll_.start();
}

void SoundSidebar::hideEvent(QHideEvent* event) {
    poll_.stop();
    QWidget::hideEvent(event);
}

QSlider* SoundSidebar::group_slider(std::size_t group) const {
    return group < groups_.size() ? groups_[group].slider : nullptr;
}

QToolButton* SoundSidebar::group_mute(std::size_t group) const {
    return group < groups_.size() ? groups_[group].mute : nullptr;
}

QSlider* SoundSidebar::channel_slider(std::size_t channel) const {
    return channel < channels_.size() ? channels_[channel] : nullptr;
}

bool SoundSidebar::showing_health() const {
    return !health_->isHidden();
}

QString SoundSidebar::health_line() const {
    return health_label_->text();
}

QString SoundSidebar::status_text() const {
    return status_->isHidden() ? QString() : status_->text();
}

}  // namespace beebium::linux_client
