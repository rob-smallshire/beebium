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

#include "system.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <QMetaObject>
#include <QtGlobal>

#include <chrono>
#include <cstdio>

namespace beebium::linux_client {

SessionAudio::SessionAudio(QObject* parent)
    : QObject(parent), output_(std::make_unique<AudioOutput>()) {
    connect(output_.get(), &AudioOutput::status_changed, this, &SessionAudio::problem_changed);
    connect(this, &SessionAudio::problem_changed, this, &SessionAudio::sound_ready_changed);
    connect(output_.get(), &AudioOutput::consuming_changed, this,
            &SessionAudio::sound_ready_changed);
    first_play_.setInterval(50);
    connect(&first_play_, &QTimer::timeout, this, &SessionAudio::log_first_play);
    if (qEnvironmentVariable("BEEBIUM_AUDIO_TRACE") == QLatin1String("1")) {
        trace_.setInterval(1000);
        connect(&trace_, &QTimer::timeout, this, &SessionAudio::trace);
        trace_.start();
    }
}

void SessionAudio::trace() {
    if (!ring_ || !stream_) {
        return;
    }
    // Monotonic milliseconds, to turn the counts into rates.
    const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    const std::uint64_t samples = stream_->samples_received();
    const double rate = traced_ms_ > 0 && now_ms > traced_ms_
                            ? static_cast<double>(samples - traced_samples_) * 1000.0 /
                                  static_cast<double>(now_ms - traced_ms_)
                            : 0.0;
    traced_ms_ = now_ms;
    traced_samples_ = samples;
    // The server's own account of its speed, beside the rate it delivers:
    // falling behind real time apart from producing the wrong rate. A short
    // blocking call, made only while tracing.
    double achieved = -1.0;
    unsigned long long skipped = 0;
    if (trace_channel_) {
        auto stub = beebium::SystemService::NewStub(trace_channel_);
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(200));
        beebium::PacingStats pacing;
        if (stub->GetPacingStats(&context, {}, &pacing).ok()) {
            achieved = pacing.achieved_speed_multiplier();
            skipped = pacing.ticks_skipped();
        }
    }
    std::fprintf(stderr,
                 "beebium: sound trace at %lld ms: %zu frames held, %llu dropped on overflow, "
                 "%llu before playback, %llu underruns, %d ms target, %llu samples, %.0f "
                 "samples/s, server speed %.4f, %llu ticks skipped\n",
                 now_ms, ring_->available(),
                 static_cast<unsigned long long>(ring_->dropped_frames()),
                 static_cast<unsigned long long>(ring_->discarded_before_playback()),
                 static_cast<unsigned long long>(ring_->underrun_events()),
                 renderer_ ? renderer_->latency_milliseconds() : 0,
                 static_cast<unsigned long long>(samples), rate, achieved, skipped);
}

SessionAudio::~SessionAudio() {
    stop();
}

void SessionAudio::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    trace_channel_ = channel;
    traced_ms_ = 0;
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
    format_at_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    first_play_at_ms_ = 0;
    first_play_.start();
    stream_problem_.clear();
    loaded_ = true;
    Q_EMIT loaded_changed(true);
    Q_EMIT problem_changed();
}

void SessionAudio::log_first_play() {
    if (!renderer_ || !stream_ || !ring_) {
        first_play_.stop();
        return;
    }
    const std::int64_t played = renderer_->first_played_index();
    if (played < 0) {
        return;  // the sink has not asked for sound yet
    }
    const qint64 now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
    if (first_play_at_ms_ == 0) {
        first_play_at_ms_ = now_ms;
        std::fprintf(stderr, "beebium: sound began %lld ms after the format, from frame %lld\n",
                     static_cast<long long>(now_ms - format_at_ms_),
                     static_cast<long long>(played));
    }
    // Then, once the machine has made a sound and playback has reached it,
    // what became of it.
    const std::int64_t sound = stream_->first_sound_ring_index();
    const bool reached = sound >= 0 && static_cast<std::int64_t>(ring_->read_position()) > sound;
    const bool given_up = now_ms - first_play_at_ms_ > 10000;
    if (sound == AudioStream::NO_SOUND_YET && !given_up) {
        return;
    }
    if (sound >= 0 && !reached && !given_up) {
        return;
    }
    first_play_.stop();
    const char* fate = sound == AudioStream::NO_SOUND_YET ? "none in 10 s"
                       : sound == AudioStream::SOUND_REFUSED
                           ? "lost, refused while the sink was starting"
                       : sound < played ? "lost, trimmed at the first play"
                       : reached        ? "played"
                                        : "not reached in 10 s";
    std::fprintf(stderr,
                 "beebium: the stream's first sound, frame %lld, %lld ms after sound began: %s; "
                 "%llu underruns so far\n",
                 static_cast<long long>(sound), static_cast<long long>(now_ms - first_play_at_ms_),
                 fate, static_cast<unsigned long long>(ring_->underrun_events()));
}

void SessionAudio::stop() {
    first_play_.stop();
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

bool SessionAudio::sound_ready() const {
    return !AudioOutput::available() || !output_->status().isEmpty() || output_->consuming() ||
           !stream_problem_.isEmpty();
}

QString SessionAudio::problem() const {
    if (!stream_problem_.isEmpty()) {
        return stream_problem_;
    }
    return output_->status();
}

}  // namespace beebium::linux_client
