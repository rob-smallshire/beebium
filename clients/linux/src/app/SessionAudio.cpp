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

#include "app/SessionAudio.hpp"

#include "app/AudioOutput.hpp"
#include "audio/AudioRenderer.hpp"
#include "audio/FrameRing.hpp"
#include "audio/PreRoll.hpp"
#include "connection/AudioStream.hpp"

#include <QMetaObject>
#include <QtGlobal>

#include <cstdio>

namespace beebium::linux_client {

SessionAudio::SessionAudio(QObject* parent)
    : QObject(parent), output_(std::make_unique<AudioOutput>()) {
    connect(output_.get(), &AudioOutput::status_changed, this, &SessionAudio::problem_changed);
    if (qEnvironmentVariable("BEEBIUM_AUDIO_TRACE") == QLatin1String("1")) {
        trace_.setInterval(1000);
        connect(&trace_, &QTimer::timeout, this, &SessionAudio::trace);
        trace_.start();
    }
}

void SessionAudio::trace() {
    if (!ring_) {
        return;
    }
    std::fprintf(stderr,
                 "beebium: sound trace: %zu frames held, %llu dropped on overflow, %llu "
                 "before playback, %llu underruns, %d ms target, %llu chunks\n",
                 ring_->available(), static_cast<unsigned long long>(ring_->dropped_frames()),
                 static_cast<unsigned long long>(ring_->discarded_before_playback()),
                 static_cast<unsigned long long>(ring_->underrun_events()),
                 renderer_ ? renderer_->latency_milliseconds() : 0,
                 static_cast<unsigned long long>(stream_ ? stream_->chunks_received() : 0));
}

SessionAudio::~SessionAudio() {
    stop();
}

void SessionAudio::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    const unsigned generation = ++generation_;
    stream_ = std::make_unique<AudioStream>(
        std::move(channel),
        AudioStream::Callbacks{
            [this, generation](const AudioLayout& layout, std::shared_ptr<FrameRing> ring) {
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, layout, ring] { on_format(generation, layout, ring); },
                    Qt::QueuedConnection);
            },
            [this, generation](const StreamEnd& end) {
                if (end.cancelled_by_client || end.code == grpc::StatusCode::OK) {
                    return;  // our stop, or the server going: the connection says
                }
                const QString message = QString::fromStdString(end.message);
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, message] {
                        if (generation == generation_) {
                            stream_problem_ = tr("The sound stopped: %1").arg(message);
                            Q_EMIT problem_changed();
                        }
                    },
                    Qt::QueuedConnection);
            },
        });
    stream_->start();
}

void SessionAudio::on_format(unsigned generation, const AudioLayout& layout,
                             std::shared_ptr<FrameRing> ring) {
    if (generation != generation_) {
        return;
    }
    layout_ = layout;
    ring_ = ring;
    // The renderer reads the ring by reference: the two travel together to
    // the sink's thread, and live as long as either side holds them.
    struct Playback {
        Playback(std::shared_ptr<FrameRing> shared_ring, const AudioLayout& layout,
                 const MixerControls& controls)
            : ring(std::move(shared_ring)), renderer(layout, *ring, controls) {}
        std::shared_ptr<FrameRing> ring;
        AudioRenderer renderer;
    };
    auto playback = std::make_shared<Playback>(ring, layout_, controls_);
    renderer_ = std::shared_ptr<AudioRenderer>(playback, &playback->renderer);
    output_->start(renderer_, static_cast<int>(layout_.sample_rate));
    stream_problem_.clear();
    loaded_ = true;
    Q_EMIT loaded_changed(true);
    Q_EMIT problem_changed();
}

void SessionAudio::stop() {
    if (loaded_) {
        // For diagnosis: how this connection's sound went, overflow apart
        // from what the server never sent.
        const AudioHealth sound = health();
        const auto wire = static_cast<unsigned long long>(stream_ ? stream_->wire_dropped_samples() : 0);
        std::fprintf(stderr,
                     "beebium: sound this connection: %llu underruns, %llu dropped (%llu on "
                     "overflow, %llu never sent), %d ms buffered; %llu waited out the "
                     "sink starting\n",
                     static_cast<unsigned long long>(sound.underrun_events),
                     static_cast<unsigned long long>(sound.dropped_samples),
                     static_cast<unsigned long long>(sound.dropped_samples) - wire, wire,
                     sound.latency_ms,
                     static_cast<unsigned long long>(ring_ ? ring_->discarded_before_playback() : 0));
    }
    ++generation_;
    if (output_) {
        output_->stop();
    }
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    renderer_.reset();
    ring_.reset();
    if (loaded_) {
        loaded_ = false;
        Q_EMIT loaded_changed(false);
    }
}

AudioHealth SessionAudio::health() const {
    AudioHealth health;
    if (ring_) {
        health.dropped_samples = ring_->dropped_frames() +
                                 (stream_ ? stream_->wire_dropped_samples() : 0);
        health.underrun_events = ring_->underrun_events();
    }
    health.latency_ms = renderer_ ? renderer_->latency_milliseconds() : PreRoll::INITIAL_TARGET_MS;
    return health;
}

QString SessionAudio::problem() const {
    if (!stream_problem_.isEmpty()) {
        return stream_problem_;
    }
    return output_->status();
}

}  // namespace beebium::linux_client
