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

#include "connection/AudioStream.hpp"

#include "audio/PreRoll.hpp"

#include "audio.grpc.pb.h"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <optional>

namespace beebium::linux_client {

std::size_t AudioStream::ring_capacity(std::uint32_t sample_rate) {
    return PreRoll::frames(PreRoll::CAPACITY_MS, static_cast<int>(sample_rate));
}

AudioStream::AudioStream(std::shared_ptr<grpc::Channel> channel, Callbacks callbacks)
    : stub_(beebium::AudioService::NewStub(channel)),
      callbacks_(std::move(callbacks)),
      reader_("bbm-audio",
              [this](grpc::ClientContext& context) { return open(context); },
              {[this](beebium::AudioChunk& chunk) { on_chunk(chunk); },
               [this](const StreamEnd& end) { on_finished(end); }}) {}

std::unique_ptr<grpc::ClientReader<beebium::AudioChunk>> AudioStream::open(
    grpc::ClientContext& context) {
    // The format first, once: it is fixed for the server's life (8.1).
    grpc::ClientContext format_context;
    format_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    beebium::AudioFormat format;
    format_status_ = stub_->GetAudioFormat(&format_context, {}, &format);
    if (format_status_.ok()) {
        const AudioLayout layout = audio_layout(format);
        fields_ = std::max<std::size_t>(layout.field_count, 1);
        ring_ = std::make_shared<FrameRing>(ring_capacity(layout.sample_rate), fields_);
        if (callbacks_.format_ready) {
            callbacks_.format_ready(layout, ring_);
        }
    } else {
        // Without a format nothing can be played: end the stream at once,
        // reporting the format's failure rather than a cancellation.
        context.TryCancel();
    }
    beebium::SubscribeAudioRequest request;
    request.set_chunk_size(1024);
    return stub_->SubscribeAudio(&context, request);
}

void AudioStream::on_chunk(beebium::AudioChunk& chunk) {
    if (!ring_) {
        return;
    }
    const std::size_t count = chunk.sample_count();
    const std::size_t bytes = count * fields_ * sizeof(std::uint32_t);
    if (count == 0 || chunk.samples().size() < bytes) {
        malformed_.fetch_add(1);
        return;
    }
    chunks_.fetch_add(1);
    samples_.fetch_add(count);
    drops_.chunk(chunk.sequence(), count);
    wire_dropped_.store(drops_.dropped());
    words_.resize(count * fields_);
    std::memcpy(words_.data(), chunk.samples().data(), bytes);  // little-endian fields
    // The first frame that differs from the one before it is the first sound.
    std::optional<std::size_t> sound;
    if (first_sound_.load() == NO_SOUND_YET) {
        for (std::size_t i = 0; i < count && !sound; ++i) {
            const std::uint32_t* frame = &words_[i * fields_];
            if (!last_frame_.empty() && !std::equal(frame, frame + fields_, last_frame_.begin())) {
                sound = i;
            }
            last_frame_.assign(frame, frame + fields_);
        }
    }
    const std::uint64_t position = ring_->write_position();
    const std::size_t kept = ring_->write(words_.data(), count);
    if (sound) {
        first_sound_.store(*sound < kept ? static_cast<std::int64_t>(position + *sound) : SOUND_REFUSED);
    }
}

void AudioStream::on_finished(const StreamEnd& end) {
    if (!callbacks_.finished) {
        return;
    }
    if (!format_status_.ok()) {
        StreamEnd failed;
        failed.code = format_status_.error_code();
        failed.message = "the audio format could not be read: " + format_status_.error_message();
        failed.cancelled_by_client = end.cancelled_by_client;
        callbacks_.finished(failed);
        return;
    }
    callbacks_.finished(end);
}

}  // namespace beebium::linux_client
