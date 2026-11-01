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

#ifndef BEEBIUM_LINUX_APP_AUDIO_OUTPUT_HPP
#define BEEBIUM_LINUX_APP_AUDIO_OUTPUT_HPP

#include <QObject>
#include <QString>

#include <memory>

class QThread;

namespace beebium::linux_client {

class AudioRenderer;
class SinkWorker;

// Plays a renderer through the host's default audio output: a QAudioSink
// (PipeWire or PulseAudio underneath) fed from the renderer as fast as
// the device consumes, at the
// format's sample rate, as float stereo where the device takes it, else
// 16-bit (client specification 8.3). The sink lives on a thread of its own,
// so an output that blocks, as one may after the host wakes, never holds up
// the GUI; start and stop only queue work to it.
//
// Built without Qt Multimedia, or with BEEBIUM_AUDIO_OUTPUT=none (the
// tests), there is no sink, and status() says so.
class AudioOutput : public QObject {
    Q_OBJECT

public:
    explicit AudioOutput(QObject* parent = nullptr);
    ~AudioOutput() override;

    // Whether this build and environment can play sound at all.
    static bool available();

    void start(std::shared_ptr<AudioRenderer> renderer, int sample_rate);
    void stop();

    // What the output is doing, for the Sound sidebar: empty while playing.
    QString status() const { return status_; }
    // Whether the device has taken sound since start(): a sink can take a
    // second or two to start.
    bool consuming() const { return consuming_; }

Q_SIGNALS:
    void status_changed(const QString& status);
    void consuming_changed(bool consuming);

private:
    void set_status(const QString& status);

    QThread* thread_ = nullptr;
    SinkWorker* worker_ = nullptr;
    QString status_;
    bool started_ = false;
    bool consuming_ = false;
    unsigned generation_ = 0;  // of start(), to ignore an earlier sink's news
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_AUDIO_OUTPUT_HPP
