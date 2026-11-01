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

#ifndef BEEBIUM_LINUX_CONNECTION_AUDIO_STREAM_HPP
#define BEEBIUM_LINUX_CONNECTION_AUDIO_STREAM_HPP

#include "audio/AudioLayout.hpp"
#include "audio/FrameRing.hpp"
#include "audio/WireDrops.hpp"
#include "connection/StreamReader.hpp"

#include "audio.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace beebium::linux_client {

// The audio of one connection (client specification 8.1, 8.3), on a thread
// of its own: AudioService.GetAudioFormat once, then SubscribeAudio, each
// chunk's frames written whole into a ring sized for the format. The ring
// exists before the first chunk arrives, so nothing is lost or misaligned
// while the format is fetched. The producer never blocks: what the ring
// cannot hold is dropped and counted.
//
// format_ready and finished run on the reader thread.
class AudioStream {
public:
    // 500 ms of frames, the room the pre-roll's 480 ms cap needs.
    static std::size_t ring_capacity(std::uint32_t sample_rate);

    struct Callbacks {
        std::function<void(const AudioLayout& layout, std::shared_ptr<FrameRing> ring)>
            format_ready;
        std::function<void(const StreamEnd&)> finished;
    };

    AudioStream(std::shared_ptr<grpc::Channel> channel, Callbacks callbacks);

    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    void start() { reader_.start(); }
    // Cancel the call and join the reader. Idempotent; safe before start().
    void stop() { reader_.stop(); }

    std::uint64_t chunks_received() const { return chunks_.load(); }
    std::uint64_t samples_received() const { return samples_.load(); }
    std::uint64_t wire_dropped_samples() const { return wire_dropped_.load(); }
    std::uint64_t malformed_chunks() const { return malformed_.load(); }

private:
    std::unique_ptr<grpc::ClientReader<beebium::AudioChunk>> open(grpc::ClientContext& context);
    void on_chunk(beebium::AudioChunk& chunk);
    void on_finished(const StreamEnd& end);

    std::shared_ptr<beebium::AudioService::Stub> stub_;
    Callbacks callbacks_;
    // Reader thread only, once the format is in.
    std::shared_ptr<FrameRing> ring_;
    std::size_t fields_ = 0;
    std::vector<std::uint32_t> words_;
    WireDrops drops_;
    grpc::Status format_status_;
    std::atomic<std::uint64_t> chunks_{0};
    std::atomic<std::uint64_t> samples_{0};
    std::atomic<std::uint64_t> wire_dropped_{0};
    std::atomic<std::uint64_t> malformed_{0};
    StreamReader<beebium::AudioChunk> reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_AUDIO_STREAM_HPP
