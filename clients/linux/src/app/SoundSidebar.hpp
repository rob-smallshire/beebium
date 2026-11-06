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

#ifndef BEEBIUM_LINUX_APP_SOUND_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_SOUND_SIDEBAR_HPP

#include "app/SessionAudio.hpp"

#include <QTimer>
#include <QWidget>

#include <chrono>
#include <optional>
#include <vector>

class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace beebium::linux_client {

// "N underruns, M dropped": what the health line says, once there is
// anything to say (client specification 8.3).
QString health_text(const AudioHealth& health);

// When the health line shows (client specification 8.3): from a glitch, an
// underrun or a drop the counts had not seen, until five seconds pass with
// none newer. The counts themselves are the session's, never reset by
// hiding; counts that fall, as a new connection's do, are no glitch.
class GlitchWatch {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds QUIET{5000};

    // Note the health read at `now`; whether the line shows.
    bool observe(const AudioHealth& health, Clock::time_point now);

private:
    std::uint64_t underruns_ = 0;
    std::uint64_t dropped_ = 0;
    std::optional<Clock::time_point> last_glitch_;
};

// The Sound mode (client specification 8.2, 8.3), after the macOS client's
// mixer: the master and its mute; then one section per group the server
// reports, in the format's order and never hidden, each with its sub-master
// and mute, and for the group owning per-channel sources a slider per
// channel beneath it; then, for five seconds after each glitch, the health
// line. Sliders write the window's mixer controls, which the playback
// thread reads.
class SoundSidebar : public QWidget {
    Q_OBJECT

public:
    static constexpr int SLIDER_STEPS = 1000;
    static constexpr int HEALTH_POLL_MS = 250;

    explicit SoundSidebar(SessionAudio& audio, QWidget* parent = nullptr);

    // For tests: the controls by role.
    QSlider* master_slider() const { return master_slider_; }
    QToolButton* master_mute() const { return master_mute_; }
    QSlider* group_slider(std::size_t group) const;
    QToolButton* group_mute(std::size_t group) const;
    QSlider* channel_slider(std::size_t channel) const;
    std::size_t group_sections() const { return groups_.size(); }
    bool showing_health() const;
    QString health_line() const;
    QString status_text() const;

    // Read the health now, as the poll does.
    void refresh_health();

private:
    struct GroupSection {
        QWidget* section = nullptr;
        QSlider* slider = nullptr;
        QToolButton* mute = nullptr;
    };

    void rebuild();
    void update_greying();
    QToolButton* make_mute(bool muted);
    static void show_muted(QToolButton* button, bool muted);

    SessionAudio& audio_;
    QLabel* status_;
    QWidget* mixer_;
    QSlider* master_slider_;
    QToolButton* master_mute_;
    QVBoxLayout* groups_layout_;
    std::vector<GroupSection> groups_;
    std::vector<QSlider*> channels_;
    QWidget* health_;
    QLabel* health_label_;
    QTimer poll_;
    AudioHealth shown_health_;
    GlitchWatch glitches_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SOUND_SIDEBAR_HPP
