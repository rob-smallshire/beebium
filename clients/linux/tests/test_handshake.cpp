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

// The connect handshake against in-process SystemService and VideoService
// (client specification 4.2, 3.2, 6.1).

#include "connection/Handshake.hpp"

#include "system.grpc.pb.h"
#include "video.grpc.pb.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>

using namespace beebium::linux_client;
using namespace std::chrono_literals;

namespace {

class FakeSystemService final : public beebium::SystemService::Service {
public:
    std::string protocol_fingerprint = "client-protocol";
    std::string host_fingerprint = "this-host";

    grpc::Status GetSystemInfo(grpc::ServerContext*, const beebium::GetSystemInfoRequest*,
                               beebium::SystemInfo* info) override {
        info->set_protocol_fingerprint(protocol_fingerprint);
        info->set_host_fingerprint(host_fingerprint);
        info->set_executable_path("/opt/beebium/bin/beebium-model-b");
        return grpc::Status::OK;
    }
};

class FakeVideoService final : public beebium::VideoService::Service {
public:
    std::atomic<int> config_calls{0};

    grpc::Status GetConfig(grpc::ServerContext*, const beebium::GetConfigRequest*,
                           beebium::VideoConfig* config) override {
        ++config_calls;
        config->set_width(640);
        config->set_height(256);
        config->set_framerate_hz(50);
        return grpc::Status::OK;
    }
};

struct Servers {
    FakeSystemService system;
    FakeVideoService video;
    int port = 0;
    std::unique_ptr<grpc::Server> server;

    void start() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&system);
        builder.RegisterService(&video);
        server = builder.BuildAndStart();
    }
    ~Servers() {
        if (server) {
            server->Shutdown(std::chrono::system_clock::now() + 1s);
        }
    }
    std::shared_ptr<grpc::Channel> channel() const {
        return grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                   grpc::InsecureChannelCredentials());
    }
};

}  // namespace

TEST_CASE("A matching server is compatible and its video config is fetched once",
          "[connection][handshake]") {
    Servers servers;
    servers.start();
    const auto result =
        perform_handshake(servers.channel(), "client-protocol", "this-host", 5000ms);
    REQUIRE(result.reached);
    CHECK(result.protocol == ProtocolVerdict::compatible);
    CHECK(result.server_is_local);
    CHECK(result.video_width == 640);
    CHECK(result.video_height == 256);
    CHECK(result.video_framerate_hz == 50);
    CHECK(servers.video.config_calls == 1);
}

TEST_CASE("A server on another host is not local", "[connection][handshake]") {
    Servers servers;
    servers.system.host_fingerprint = "another-host";
    servers.start();
    const auto result =
        perform_handshake(servers.channel(), "client-protocol", "this-host", 5000ms);
    REQUIRE(result.reached);
    CHECK_FALSE(result.server_is_local);
}

TEST_CASE("A mismatch is reported with both fingerprints and the server path",
          "[connection][handshake]") {
    Servers servers;
    servers.system.protocol_fingerprint = "older-protocol";
    servers.start();
    const auto result =
        perform_handshake(servers.channel(), "client-protocol", "this-host", 5000ms);
    REQUIRE(result.reached);
    CHECK(result.protocol == ProtocolVerdict::mismatch);
    const std::string message = describe_protocol_mismatch(result, "client-protocol");
    CHECK(message.find("older-protocol") != std::string::npos);
    CHECK(message.find("client-protocol") != std::string::npos);
    CHECK(message.find("/opt/beebium/bin/beebium-model-b") != std::string::npos);
}

TEST_CASE("An unreachable server fails within the timeout", "[connection][handshake]") {
    // Nothing listens on this port: a server took one, and has gone.
    int port = 0;
    {
        Servers departed;
        departed.start();
        REQUIRE(departed.server != nullptr);
        port = departed.port;
    }
    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                       grpc::InsecureChannelCredentials());
    const auto started = std::chrono::steady_clock::now();
    const auto result = perform_handshake(channel, "client-protocol", "this-host", 500ms);
    CHECK_FALSE(result.reached);
    CHECK(result.code != grpc::StatusCode::OK);
    CHECK(std::chrono::steady_clock::now() - started < 3s);
}
