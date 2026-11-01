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

// An in-process stand-in for a Beebium server: the services a machine
// window's session uses, with a status stream the test writes event by event.

#ifndef BEEBIUM_LINUX_TESTS_FAKE_BEEBIUM_SERVER_HPP
#define BEEBIUM_LINUX_TESTS_FAKE_BEEBIUM_SERVER_HPP

#include "app/ApplicationInfo.hpp"

#include "beebium/host_identity/HostFingerprint.hpp"

#include "keyboard.grpc.pb.h"
#include "system.grpc.pb.h"
#include "video.grpc.pb.h"
#include "debugger.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace beebium::linux_client::testing {

using namespace std::chrono_literals;

// A status stream the test writes event by event; nullopt ends it.
class ScriptedSystemService final : public beebium::SystemService::Service {
public:
    std::string protocol_fingerprint{client_protocol_fingerprint()};
    std::string host_fingerprint = beebium::host_identity::host_fingerprint();

    // What the machine-control calls saw, and how they answer.
    std::mutex calls_mutex;
    std::string last_save_destination;
    bool last_save_replace = false;
    std::string last_name;
    std::string shutdown_authority;  // x-beebium-instance-uuid
    int shutdown_requests = 0;
    bool save_fails = false;
    std::atomic<int> client_count{1};
    // GetSystemInfo blocks, as a stalled server's would, while this is set,
    // and answers again when it is cleared or the call is cancelled.
    std::atomic<bool> stall_system_info{false};
    std::atomic<int> stalled_calls{0};

    void push(std::optional<beebium::ServerStatusEvent> event) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(std::move(event));
        }
        changed_.notify_all();
    }

    grpc::Status GetSystemInfo(grpc::ServerContext* context, const beebium::GetSystemInfoRequest*,
                               beebium::SystemInfo* info) override {
        info->set_protocol_fingerprint(protocol_fingerprint);
        info->set_host_fingerprint(host_fingerprint);
        info->mutable_identity()->set_name("Model B #1");
        info->mutable_identity()->set_description("Station 1.254");
        info->mutable_connections()->set_client_count(client_count);
        if (stall_system_info) {
            ++stalled_calls;
            while (stall_system_info && !context->IsCancelled()) {
                std::this_thread::sleep_for(5ms);
            }
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status SaveMachine(grpc::ServerContext*, const beebium::SaveMachineRequest* request,
                             beebium::SaveMachineResponse* response) override {
        std::lock_guard lock(calls_mutex);
        last_save_destination = request->destination_dirpath();
        last_save_replace = request->replace_existing();
        if (save_fails) {
            return {grpc::StatusCode::ALREADY_EXISTS, "destination exists"};
        }
        response->set_machine_dirpath(request->destination_dirpath().empty()
                                          ? std::string("/m/Saved.beebium-machine")
                                          : request->destination_dirpath());
        return grpc::Status::OK;
    }

    grpc::Status SetMachineName(grpc::ServerContext*, const beebium::SetMachineNameRequest* request,
                                beebium::SetMachineNameResponse*) override {
        std::lock_guard lock(calls_mutex);
        last_name = request->name();
        return grpc::Status::OK;
    }

    grpc::Status ResetBattery(grpc::ServerContext*, const beebium::ResetBatteryRequest*,
                              beebium::ResetBatteryResponse* response) override {
        response->add_reset_units("integra-b-rtc");
        return grpc::Status::OK;
    }

    grpc::Status RequestShutdown(grpc::ServerContext* context, const beebium::ShutdownRequest*,
                                 beebium::ShutdownResponse* response) override {
        std::lock_guard lock(calls_mutex);
        ++shutdown_requests;
        const auto found = context->client_metadata().find("x-beebium-instance-uuid");
        if (found != context->client_metadata().end()) {
            shutdown_authority = std::string(found->second.data(), found->second.size());
        }
        response->set_accepted(true);
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

// A machine that starts blocked, as a launched server does, until Run.
class BlockedDebuggerService final : public beebium::DebuggerControl::Service {
public:
    std::atomic<bool> running{false};
    std::atomic<int> runs{0};

    grpc::Status GetState(grpc::ServerContext*, const beebium::Empty*,
                          beebium::ExecutionState* state) override {
        state->set_is_running(running);
        return grpc::Status::OK;
    }
    grpc::Status Run(grpc::ServerContext*, const beebium::RunRequest*,
                     beebium::RunResponse* response) override {
        ++runs;
        running = true;
        response->set_success(true);
        return grpc::Status::OK;
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
    BlockedDebuggerService debugger;
    int port = 0;
    std::unique_ptr<grpc::Server> server;

    FakeServer() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&system);
        builder.RegisterService(&video);
        builder.RegisterService(&keyboard);
        builder.RegisterService(&debugger);
        server = builder.BuildAndStart();
    }
    ~FakeServer() { server->Shutdown(std::chrono::system_clock::now() + 1s); }
    QString endpoint() const { return QStringLiteral("127.0.0.1:%1").arg(port); }
};

inline beebium::ServerStatusEvent event(beebium::ServerStatusType type,
                                        bool guest_ready = false) {
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

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_FAKE_BEEBIUM_SERVER_HPP
