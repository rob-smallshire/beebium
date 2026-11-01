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

#ifndef BEEBIUM_LINUX_APP_SESSION_AUDIO_HPP
#define BEEBIUM_LINUX_APP_SESSION_AUDIO_HPP

#include "audio/AudioLayout.hpp"
#include "audio/MixerControls.hpp"

#include <QObject>
#include <QTimer>
#include <QString>

#include <cstdint>
#include <memory>

namespace grpc {
class Channel;
}

namespace beebium::linux_client {

class AudioOutput;
class AudioRenderer;
class AudioStream;
class FrameRing;

// The playback ring's health this connection (client specification 8.3):
// what never played because the ring was full or the server never sent it,
// and the times playback ran dry.
struct AudioHealth {
    std::uint64_t dropped_samples = 0;
    std::uint64_t underrun_events = 0;
    int latency_ms = 0;

    bool glitched() const { return dropped_samples > 0 || underrun_events > 0; }
    bool operator==(const AudioHealth&) const = default;
};

// A machine window's sound: the audio of its connection, torn down and set
// up again with it, so a new connection starts with the pre-roll at its
// floor and the counters at zero; and the mixer's controls, which belong to
// the window and outlast reconnects (8.2, 8.3).
class SessionAudio : public QObject {
    Q_OBJECT

public:
    explicit SessionAudio(QObject* parent = nullptr);
    ~SessionAudio() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    // Whether the format is in and the mixer can be shown.
    bool loaded() const { return loaded_; }
    const AudioLayout& layout() const { return layout_; }
    MixerControls& controls() { return controls_; }
    AudioHealth health() const;
    // Why there is no sound, if there is none: empty while it plays.
    QString problem() const;
    // Why the stream itself failed, if it did: the format could not be read,
    // or the stream broke.
    QString stream_problem() const { return stream_problem_; }

Q_SIGNALS:
    // The format arrived (loaded) or the connection went (not).
    void loaded_changed(bool loaded);
    void problem_changed();

private:
    void on_format(unsigned generation, const AudioLayout& layout,
                   std::shared_ptr<FrameRing> ring);

    MixerControls controls_;
    std::unique_ptr<AudioOutput> output_;
    std::unique_ptr<AudioStream> stream_;
    std::shared_ptr<FrameRing> ring_;
    std::shared_ptr<AudioRenderer> renderer_;
    AudioLayout layout_;
    bool loaded_ = false;
    QString stream_problem_;
    unsigned generation_ = 0;
    // BEEBIUM_AUDIO_TRACE=1: the ring's fill and counters each second.
    QTimer trace_;
    std::shared_ptr<grpc::Channel> trace_channel_;
    long long traced_ms_ = 0;
    std::uint64_t traced_samples_ = 0;
    void trace();
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_AUDIO_HPP
