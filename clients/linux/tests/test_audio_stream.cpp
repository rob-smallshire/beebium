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

// The audio stream against an in-process server (client specification
// 8.1, 8.3): the format once, then chunks into the ring, wire drops counted
// from the sequence numbers.

#include "connection/AudioStream.hpp"

#include "FakeBeebiumServer.hpp"

#include <grpcpp/grpcpp.h>

#include <catch2/catch_test_macros.hpp>

#include <mutex>
#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

struct Listener {
    std::mutex mutex;
    std::optional<AudioLayout> layout;
    std::shared_ptr<FrameRing> ring;
    std::optional<StreamEnd> end;

    AudioStream::Callbacks callbacks() {
        return {[this](const AudioLayout& l, std::shared_ptr<FrameRing> r) {
                    std::lock_guard lock(mutex);
                    layout = l;
                    ring = std::move(r);
                },
                [this](const StreamEnd& e) {
                    std::lock_guard lock(mutex);
                    end = e;
                }};
    }
    bool has_ring() {
        std::lock_guard lock(mutex);
        return ring != nullptr;
    }
    bool ended() {
        std::lock_guard lock(mutex);
        return end.has_value();
    }
};

std::shared_ptr<grpc::Channel> channel_to(const FakeServer& server) {
    return grpc::CreateChannel(server.endpoint().toStdString(), grpc::InsecureChannelCredentials());
}

}  // namespace

TEST_CASE("The format is read once and the ring made for it before any chunk",
          "[audio][stream]") {
    FakeServer server;
    Listener listener;
    AudioStream stream(channel_to(server), listener.callbacks());
    stream.start();
    REQUIRE(wait_until([&] { return listener.has_ring(); }));
    CHECK(server.audio.format_calls == 1);
    CHECK(listener.layout->channels.size() == 4);
    CHECK(listener.ring->fields() == 2);
    CHECK(listener.ring->capacity() == 24000);  // 500 ms at 48 kHz
    stream.stop();
}

TEST_CASE("Chunks reach the ring whole, and a gap in the sequence is counted exactly",
          "[audio][stream]") {
    FakeServer server;
    Listener listener;
    AudioStream stream(channel_to(server), listener.callbacks());
    stream.start();
    REQUIRE(wait_until([&] { return listener.has_ring(); }));
    REQUIRE(wait_until([&] { return server.audio.subscriptions == 1; }));
    // Joining mid-stream: the first chunk's sequence is the baseline.
    server.audio.push(audio_chunk(5000, 1024, 0x00010002));
    server.audio.push(audio_chunk(6024, 1024));
    server.audio.push(audio_chunk(8000, 1024));  // 952 samples never sent
    REQUIRE(wait_until([&] { return stream.chunks_received() == 3; }));
    CHECK(stream.wire_dropped_samples() == 952);
    CHECK(listener.ring->available() == 3072);
    std::vector<std::uint32_t> first(2, 0);
    listener.ring->read(first.data(), 1);
    CHECK(first == std::vector<std::uint32_t>{0x00010002, 0x00010002});
    stream.stop();
}

TEST_CASE("A chunk shorter than it says is refused", "[audio][stream]") {
    FakeServer server;
    Listener listener;
    AudioStream stream(channel_to(server), listener.callbacks());
    stream.start();
    REQUIRE(wait_until([&] { return server.audio.subscriptions == 1; }));
    auto chunk = audio_chunk(0, 16);
    chunk.set_sample_count(32);
    server.audio.push(chunk);
    REQUIRE(wait_until([&] { return stream.malformed_chunks() == 1; }));
    CHECK(listener.ring->available() == 0);
    stream.stop();
}

TEST_CASE("The server ending the stream is not taken for our own stop", "[audio][stream]") {
    FakeServer server;
    Listener listener;
    AudioStream stream(channel_to(server), listener.callbacks());
    stream.start();
    REQUIRE(wait_until([&] { return server.audio.subscriptions == 1; }));
    server.audio.push(std::nullopt);
    REQUIRE(wait_until([&] { return listener.ended(); }));
    CHECK_FALSE(listener.end->cancelled_by_client);
    stream.stop();
}

TEST_CASE("Stopping the stream reports our own cancellation", "[audio][stream]") {
    FakeServer server;
    Listener listener;
    AudioStream stream(channel_to(server), listener.callbacks());
    stream.start();
    REQUIRE(wait_until([&] { return server.audio.subscriptions == 1; }));
    stream.stop();
    REQUIRE(listener.ended());
    CHECK(listener.end->cancelled_by_client);
}
