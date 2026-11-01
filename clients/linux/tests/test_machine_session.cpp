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

// MachineSession against in-process fakes of the services it uses: the
// connection's events, liveness (client specification 4.3), identity (5.7),
// filesystem sharing (3.2) and the guest-ready sync point (7.3).

#include "app/ApplicationInfo.hpp"
#include "app/MachineSession.hpp"

#include "beebium/host_identity/HostFingerprint.hpp"

#include "keyboard.grpc.pb.h"
#include "system.grpc.pb.h"
#include "video.grpc.pb.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

using namespace beebium::linux_client;
using namespace std::chrono_literals;

namespace {

// A status stream the test writes event by event; nullopt ends it.
class ScriptedSystemService final : public beebium::SystemService::Service {
public:
    std::string protocol_fingerprint{client_protocol_fingerprint()};
    std::string host_fingerprint = beebium::host_identity::host_fingerprint();

    void push(std::optional<beebium::ServerStatusEvent> event) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(std::move(event));
        }
        changed_.notify_all();
    }

    grpc::Status GetSystemInfo(grpc::ServerContext*, const beebium::GetSystemInfoRequest*,
                               beebium::SystemInfo* info) override {
        info->set_protocol_fingerprint(protocol_fingerprint);
        info->set_host_fingerprint(host_fingerprint);
        info->mutable_identity()->set_name("Model B #1");
        info->mutable_identity()->set_description("Station 1.254");
        info->mutable_connections()->set_client_count(1);
        return grpc::Status::OK;
    }

    grpc::Status WatchServerStatus(grpc::ServerContext* context,
                                   const beebium::WatchServerStatusRequest*,
                                   grpc::ServerWriter<beebium::ServerStatusEvent>* writer) override {
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            auto next = std::move(script_.front());
            script_.pop_front();
            lock.unlock();
            if (!next) {
                return grpc::Status::OK;
            }
            writer->Write(*next);
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::optional<beebium::ServerStatusEvent>> script_;
};

class QuietVideoService final : public beebium::VideoService::Service {
    grpc::Status GetConfig(grpc::ServerContext*, const beebium::GetConfigRequest*,
                           beebium::VideoConfig* config) override {
        config->set_width(640);
        config->set_height(256);
        config->set_framerate_hz(50);
        return grpc::Status::OK;
    }
    grpc::Status SubscribeFrames(grpc::ServerContext* context, const beebium::SubscribeFramesRequest*,
                                 grpc::ServerWriter<beebium::Frame>*) override {
        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(10ms);
        }
        return grpc::Status::CANCELLED;
    }
};

class KeyTableService final : public beebium::KeyboardService::Service {
    grpc::Status GetAllKeyMappings(grpc::ServerContext*, const beebium::GetAllKeyMappingsRequest*,
                                   beebium::AllKeyMappingsResponse* response) override {
        auto* entry = response->add_mappings();
        entry->set_character("a");
        entry->set_ik_number(0x41);
        entry->set_name("a");
        return grpc::Status::OK;
    }
};

struct FakeServer {
    ScriptedSystemService system;
    QuietVideoService video;
    KeyTableService keyboard;
    int port = 0;
    std::unique_ptr<grpc::Server> server;

    FakeServer() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&system);
        builder.RegisterService(&video);
        builder.RegisterService(&keyboard);
        server = builder.BuildAndStart();
    }
    ~FakeServer() { server->Shutdown(std::chrono::system_clock::now() + 1s); }
    QString endpoint() const { return QStringLiteral("127.0.0.1:%1").arg(port); }
};

beebium::ServerStatusEvent event(beebium::ServerStatusType type, bool guest_ready = false) {
    beebium::ServerStatusEvent e;
    e.set_status(type);
    e.set_guest_ready(guest_ready);
    return e;
}

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 5000ms) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeout.count()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

// Records what a session reports.
struct Observer {
    int connected = 0;
    int guest_ready = 0;
    int resets = 0;
    std::vector<Liveness> liveness;
    QString name;
    QString description;
    std::optional<ConnectionFailure> failure;

    explicit Observer(MachineSession& session) {
        QObject::connect(&session, &MachineSession::connected_to_server, [this] { ++connected; });
        QObject::connect(&session, &MachineSession::guest_ready, [this] { ++guest_ready; });
        QObject::connect(&session, &MachineSession::machine_reset, [this] { ++resets; });
        QObject::connect(&session, &MachineSession::liveness_changed,
                         [this](Liveness l) { liveness.push_back(l); });
        QObject::connect(&session, &MachineSession::identity_changed,
                         [this](const QString& n, const QString& d) {
                             name = n;
                             description = d;
                         });
        QObject::connect(&session, &MachineSession::connection_failed,
                         [this](ConnectionFailure f, const QString&) { failure = f; });
    }
};

}  // namespace

TEST_CASE("Connecting reports the machine's identity and that it shares our filesystem",
          "[session]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    CHECK(observer.name == "Model B #1");
    CHECK(observer.description == "Station 1.254");
    CHECK(session.machine_name() == "Model B #1");
    CHECK(session.server_shares_filesystem());
    CHECK(session.liveness() == Liveness::active);
}

TEST_CASE("A server on another host does not share our filesystem", "[session]") {
    FakeServer server;
    server.system.host_fingerprint = "somewhere-else";
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    CHECK_FALSE(session.server_shares_filesystem());
}

TEST_CASE("Identity changes and resets arrive from the status stream", "[session]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));

    auto renamed = event(beebium::SERVER_STATUS_IDENTITY_CHANGED);
    renamed.mutable_identity()->set_name("Elite Server");
    renamed.mutable_identity()->set_description("");
    server.system.push(renamed);
    server.system.push(event(beebium::SERVER_STATUS_MACHINE_RESET));
    REQUIRE(wait_until([&] { return observer.resets == 1; }));
    CHECK(observer.name == "Elite Server");
    CHECK(session.machine_name() == "Elite Server");
}

TEST_CASE("Guest readiness syncs once per rising edge, including already ready",
          "[session][caps-lock]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));

    server.system.push(event(beebium::SERVER_STATUS_READY, true));    // ready on connect
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT, true));
    server.system.push(event(beebium::SERVER_STATUS_MACHINE_RESET, false));
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT, false));
    server.system.push(event(beebium::SERVER_STATUS_GUEST_READY, true));
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT, true));
    REQUIRE(wait_until([&] { return observer.guest_ready == 2 && observer.resets == 1; }));
    QCoreApplication::processEvents();
    CHECK(observer.guest_ready == 2);
}

TEST_CASE("An announced shutdown and the stream's end read as stopped", "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    server.system.push(event(beebium::SERVER_STATUS_SHUTTING_DOWN));
    server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return session.liveness() == Liveness::stopped; }));
    CHECK(observer.liveness.back() == Liveness::stopped);
}

TEST_CASE("The stream's end with no announcement reads as died", "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return session.liveness() == Liveness::died; }));
}

TEST_CASE("Silent heartbeats read as unreachable, and the next event recovers",
          "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    REQUIRE(wait_until([&] { return !observer.liveness.empty() || session.liveness() == Liveness::active; }));
    // Let the event land, then pretend three seconds pass without another.
    wait_until([] { return false; }, 100ms);
    session.tick_liveness(LivenessTracker::Clock::now() + 3s);
    CHECK(session.liveness() == Liveness::unreachable);
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT));
    REQUIRE(wait_until([&] { return session.liveness() == Liveness::active; }));
}

TEST_CASE("Our own disconnect leaves liveness as it was", "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    session.disconnect_from_server();
    wait_until([] { return false; }, 100ms);
    CHECK(session.liveness() == Liveness::active);
    CHECK(observer.liveness.empty());
    CHECK_FALSE(session.connected());
    CHECK_FALSE(session.server_shares_filesystem());
}

TEST_CASE("A server speaking another protocol is refused as incompatible", "[session]") {
    FakeServer server;
    server.system.protocol_fingerprint = "another-protocol";
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.failure.has_value(); }));
    CHECK(*observer.failure == ConnectionFailure::incompatible);
    CHECK(observer.connected == 0);
}

TEST_CASE("Nothing listening is unreachable", "[session]") {
    int port = 0;
    {
        FakeServer departed;
        port = departed.port;
    }
    MachineSession session;
    Observer observer(session);
    session.connect_to(QStringLiteral("127.0.0.1:%1").arg(port));
    REQUIRE(wait_until([&] { return observer.failure.has_value(); }, 10000ms));
    CHECK(*observer.failure == ConnectionFailure::unreachable);
}
