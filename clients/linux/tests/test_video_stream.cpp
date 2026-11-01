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

// VideoStream against an in-process VideoService, so the reader thread, the
// mailbox hand-off and the stream-end classification are exercised over real
// gRPC without an emulator.

#include "connection/VideoStream.hpp"

#include "video.grpc.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace beebium::linux_client;
using namespace std::chrono_literals;

namespace {

beebium::Frame frame_numbered(std::uint64_t number, std::uint32_t width = 16,
                              std::uint32_t height = 8) {
    beebium::Frame frame;
    frame.set_frame_number(number);
    frame.set_width(width);
    frame.set_height(height);
    frame.set_display_width(640);
    frame.set_display_height(256);
    frame.set_pixels(std::string(std::size_t{width} * height * 4, '\0'));
    return frame;
}

class FakeVideoService final : public beebium::VideoService::Service {
public:
    std::vector<beebium::Frame> frames;
    grpc::Status final_status = grpc::Status::OK;
    bool hold_open = false;

    grpc::Status SubscribeFrames(grpc::ServerContext* context,
                                 const beebium::SubscribeFramesRequest*,
                                 grpc::ServerWriter<beebium::Frame>* writer) override {
        for (const auto& frame : frames) {
            writer->Write(frame);
        }
        while (hold_open && !context->IsCancelled()) {
            std::this_thread::sleep_for(5ms);
        }
        return final_status;
    }
};

class InProcessServer {
public:
    explicit InProcessServer(grpc::Service& service) {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port_);
        builder.RegisterService(&service);
        server_ = builder.BuildAndStart();
    }
    ~InProcessServer() { server_->Shutdown(std::chrono::system_clock::now() + 1s); }

    std::shared_ptr<grpc::Channel> channel() const {
        return grpc::CreateChannel("127.0.0.1:" + std::to_string(port_),
                                   grpc::InsecureChannelCredentials());
    }

private:
    int port_ = 0;
    std::unique_ptr<grpc::Server> server_;
};

// Collects the callbacks the reader thread makes.
struct Observer {
    std::mutex mutex;
    std::condition_variable changed;
    int frame_ready_calls = 0;
    std::optional<StreamEnd> end;

    VideoStream::Callbacks callbacks() {
        return {
            [this] {
                std::lock_guard lock(mutex);
                ++frame_ready_calls;
                changed.notify_all();
            },
            [this](const StreamEnd& stream_end) {
                std::lock_guard lock(mutex);
                end = stream_end;
                changed.notify_all();
            },
        };
    }

    bool wait_for_end() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [this] { return end.has_value(); });
    }

    bool wait_for_frame_ready() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [this] { return frame_ready_calls > 0; });
    }
};

}  // namespace

TEST_CASE("Frames reach the mailbox and the latest is kept", "[connection][video]") {
    FakeVideoService service;
    for (std::uint64_t n = 1; n <= 5; ++n) {
        service.frames.push_back(frame_numbered(n));
    }
    InProcessServer server(service);
    FrameMailbox mailbox;
    Observer observer;

    VideoStream stream(server.channel(), mailbox, observer.callbacks());
    stream.start();
    REQUIRE(observer.wait_for_end());
    stream.stop();

    CHECK(stream.frames_received() == 5);
    CHECK(mailbox.published_count() == 5);
    auto latest = mailbox.take();
    REQUIRE(latest != nullptr);
    CHECK(latest->frame_number == 5);
    // Nothing took frames meanwhile, so only the first publish notified.
    CHECK(observer.frame_ready_calls == 1);
}

TEST_CASE("A stream the server ends cleanly is not reported as our cancellation",
          "[connection][video]") {
    FakeVideoService service;
    service.frames.push_back(frame_numbered(1));
    InProcessServer server(service);
    FrameMailbox mailbox;
    Observer observer;

    VideoStream stream(server.channel(), mailbox, observer.callbacks());
    stream.start();
    REQUIRE(observer.wait_for_end());

    CHECK(observer.end->code == grpc::StatusCode::OK);
    CHECK_FALSE(observer.end->cancelled_by_client);
}

TEST_CASE("A server error is reported with its code", "[connection][video]") {
    FakeVideoService service;
    service.final_status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "going away");
    InProcessServer server(service);
    FrameMailbox mailbox;
    Observer observer;

    VideoStream stream(server.channel(), mailbox, observer.callbacks());
    stream.start();
    REQUIRE(observer.wait_for_end());

    CHECK(observer.end->code == grpc::StatusCode::UNAVAILABLE);
    CHECK(observer.end->message == "going away");
    CHECK_FALSE(observer.end->cancelled_by_client);
}

TEST_CASE("Stopping an open stream reports our own cancellation", "[connection][video]") {
    FakeVideoService service;
    service.frames.push_back(frame_numbered(1));
    service.hold_open = true;
    InProcessServer server(service);
    FrameMailbox mailbox;
    Observer observer;

    VideoStream stream(server.channel(), mailbox, observer.callbacks());
    stream.start();
    REQUIRE(observer.wait_for_frame_ready());
    stream.stop();

    REQUIRE(observer.end.has_value());
    CHECK(observer.end->code == grpc::StatusCode::CANCELLED);
    CHECK(observer.end->cancelled_by_client);
}

TEST_CASE("A malformed frame is counted and never published", "[connection][video]") {
    FakeVideoService service;
    auto bad = frame_numbered(1);
    bad.mutable_pixels()->resize(3);
    service.frames.push_back(bad);
    service.frames.push_back(frame_numbered(2));
    InProcessServer server(service);
    FrameMailbox mailbox;
    Observer observer;

    VideoStream stream(server.channel(), mailbox, observer.callbacks());
    stream.start();
    REQUIRE(observer.wait_for_end());

    CHECK(stream.frames_refused() == 1);
    CHECK(stream.frames_received() == 1);
    auto latest = mailbox.take();
    REQUIRE(latest != nullptr);
    CHECK(latest->frame_number == 2);
}

TEST_CASE("Stopping a stream that never started is harmless", "[connection][video]") {
    FakeVideoService service;
    InProcessServer server(service);
    FrameMailbox mailbox;
    Observer observer;

    VideoStream stream(server.channel(), mailbox, observer.callbacks());
    stream.stop();
    stream.stop();
    CHECK_FALSE(observer.end.has_value());
}
