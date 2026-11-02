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

#include "app/MachineSession.hpp"

#include "app/ApplicationInfo.hpp"
#include "app/FilesystemSharing.hpp"

#include "beebium/host_identity/HostFingerprint.hpp"

#include "debugger.grpc.pb.h"
#include "keyboard.grpc.pb.h"
#include "system.grpc.pb.h"

#include <QMetaObject>

#include <chrono>
#include <cstdio>

namespace beebium::linux_client {

namespace {

constexpr std::chrono::milliseconds HANDSHAKE_TIMEOUT{5000};
constexpr std::chrono::milliseconds LIVENESS_TICK{250};

// The server's canonical character-to-key table, fetched once per
// connection (client specification 7.4). Mapping entries name BBC keys
// through it too, so without it the keyboard stays inert, and says why.
BbcKeyTable fetch_key_table(const std::shared_ptr<grpc::Channel>& channel,
                            CallCanceller& canceller) {
    auto keyboard = beebium::KeyboardService::NewStub(channel);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + HANDSHAKE_TIMEOUT);
    if (!canceller.begin(context)) {
        return {};
    }
    beebium::AllKeyMappingsResponse response;
    const grpc::Status status =
        keyboard->GetAllKeyMappings(&context, beebium::GetAllKeyMappingsRequest{}, &response);
    canceller.end();
    if (!status.ok()) {
        std::fprintf(stderr, "beebium: no key table from the server: %s\n",
                     status.error_message().c_str());
        return {};
    }
    return BbcKeyTable::from_proto(response);
}

constexpr std::chrono::milliseconds CALL_TIMEOUT{10000};

void set_deadline(grpc::ClientContext& context) {
    context.set_deadline(std::chrono::system_clock::now() + CALL_TIMEOUT);
}

CallOutcome outcome_of(const grpc::Status& status) {
    return {status.ok(), status.error_code(), QString::fromStdString(status.error_message())};
}

ConnectionFailure classify(const HandshakeResult& result) {
    if (result.reached) {
        return ConnectionFailure::incompatible;
    }
    switch (result.code) {
    case grpc::StatusCode::UNAVAILABLE:
    case grpc::StatusCode::DEADLINE_EXCEEDED:
        return ConnectionFailure::unreachable;
    default:
        return ConnectionFailure::error;
    }
}

}  // namespace

MachineSession::MachineSession(QObject* parent)
    : QObject(parent), audio_(new SessionAudio(this)), indicators_(new SessionIndicators(this)),
      discs_(new SessionDiscs(this)),
      typing_(new SessionTyping(this)) {
    liveness_ticker_.setInterval(LIVENESS_TICK);
    connect(&liveness_ticker_, &QTimer::timeout, this,
            [this] { tick_liveness(LivenessTracker::Clock::now()); });
    connect(audio_, &SessionAudio::sound_ready_changed, this, [this] {
        if (audio_->sound_ready()) {
            run_hold_.release();
        }
    });
}

MachineSession::~MachineSession() {
    disconnect_from_server();
}

void MachineSession::connect_to(const QString& endpoint) {
    disconnect_from_server();
    endpoint_ = endpoint;
    liveness_.reset();
    channel_ = grpc::CreateChannel(endpoint.toStdString(), grpc::InsecureChannelCredentials());

    const unsigned generation = ++generation_;
    handshake_canceller_ = std::make_shared<CallCanceller>();
    handshake_thread_ = std::thread([this, generation, channel = channel_,
                                     canceller = handshake_canceller_] {
        const HandshakeResult result = perform_handshake(
            channel, client_protocol_fingerprint(),
            beebium::host_identity::host_fingerprint(), HANDSHAKE_TIMEOUT, canceller.get());
        BbcKeyTable table;
        if (result.reached && is_compatible(result.protocol)) {
            table = fetch_key_table(channel, *canceller);
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, result, table = std::move(table)]() mutable {
                on_handshake(generation, result, std::move(table));
            },
            Qt::QueuedConnection);
    });
}

void MachineSession::reconnect() {
    if (!endpoint_.isEmpty()) {
        connect_to(endpoint_);
    }
}

void MachineSession::on_handshake(unsigned generation, const HandshakeResult& result,
                                  BbcKeyTable table) {
    if (generation != generation_) {
        return;
    }
    if (handshake_thread_.joinable()) {
        handshake_thread_.join();
    }
    if (!result.reached) {
        Q_EMIT connection_failed(classify(result), QString::fromStdString(result.error));
        return;
    }
    if (!is_compatible(result.protocol)) {
        // Refused rather than warned about: a client may choose either, but
        // must not ignore it (client specification 4.2).
        Q_EMIT connection_failed(ConnectionFailure::incompatible,
                               QString::fromStdString(describe_protocol_mismatch(
                                   result, client_protocol_fingerprint())));
        return;
    }

    connected_ = true;
    rpc_ = std::make_unique<RpcWorker>();
    machine_saved_ = result.machine_saved;
    machine_dirpath_ = QString::fromStdString(result.machine_dirpath);
    machine_running_ = result.machine_running;
    machine_name_ = QString::fromStdString(result.machine_name);
    machine_uuid_ = QString::fromStdString(result.machine_uuid);
    machine_description_ = QString::fromStdString(result.machine_description);
    model_name_ = QString::fromStdString(result.model_name);
    server_shares_filesystem_ =
        shares_filesystem(result.server_is_local, current_sandbox());
    std::fprintf(stderr, "beebium: the server %s this filesystem\n",
                 server_shares_filesystem_ ? "shares" : "does not share");
    guest_was_ready_.reset();

    // The status stream first: it is the connection's liveness (4.3).
    status_ = std::make_unique<ServerStatusStream>(
        channel_,
        ServerStatusStream::Callbacks{
            [this, generation](const beebium::ServerStatusEvent& event) {
                QMetaObject::invokeMethod(
                    this, [this, generation, event] { on_status_event(generation, event); },
                    Qt::QueuedConnection);
            },
            [this, generation](const StreamEnd& end) {
                QMetaObject::invokeMethod(
                    this, [this, generation, end] { on_status_end(generation, end); },
                    Qt::QueuedConnection);
            },
        });
    status_->start();
    liveness_ticker_.start();

    video_ = std::make_unique<VideoStream>(
        channel_, mailbox_,
        VideoStream::Callbacks{
            [this] {
                QMetaObject::invokeMethod(this, &MachineSession::frame_ready,
                                          Qt::QueuedConnection);
            },
            // A video stream's end is not a liveness signal; the status
            // stream decides (4.3).
            {},
        });
    video_->start();
    // Sound, torn down and set up again with the connection (8.3).
    audio_->start(channel_);
    indicators_->start(channel_);
    discs_->start(channel_);
    typing_->start(channel_);

    Q_EMIT connected_to_server();
    Q_EMIT identity_changed(machine_name_, machine_description_);
    Q_EMIT running_changed(machine_running_);
    if (run_on_connect_ && !machine_running_) {
        // Launched blocked until Run (5.5), and held until its sound can be
        // heard, boot beep and all (8.3).
        const auto held_from = std::chrono::steady_clock::now();
        run_hold_.arm(audio_->sound_ready(), [this, held_from] {
            const auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - held_from)
                                     .count();
            std::fprintf(stderr, "beebium: running the machine, %lld ms after connecting, %s\n",
                         static_cast<long long>(held_ms),
                         audio_->sound_ready() ? "its sound ready" : "its sound not ready");
            run();
        });
    }

    if (!table.empty()) {
        key_table_ = std::move(table);
        keys_ = std::make_unique<KeySender>(channel_);
        Q_EMIT keyboard_ready();
    }
}

void MachineSession::on_status_event(unsigned generation,
                                     const beebium::ServerStatusEvent& event) {
    if (generation != generation_) {
        return;
    }
    const Liveness before = liveness_.state();
    liveness_.on_event(LivenessTracker::Clock::now(),
                       event.status() == beebium::SERVER_STATUS_SHUTTING_DOWN);
    set_liveness(before);

    if (event.template_media_written() != template_media_written_) {
        template_media_written_ = event.template_media_written();
        Q_EMIT unsaved_changes_changed(template_media_written_);
    }

    if (event.status() == beebium::SERVER_STATUS_IDENTITY_CHANGED && event.has_identity()) {
        machine_name_ = QString::fromStdString(event.identity().name());
        machine_uuid_ = QString::fromStdString(event.identity().uuid());
        machine_description_ = QString::fromStdString(event.identity().description());
        Q_EMIT identity_changed(machine_name_, machine_description_);
    }
    if (event.status() == beebium::SERVER_STATUS_MACHINE_RESET) {
        Q_EMIT machine_reset();
    }
    // Every event carries the guest's readiness; its rising edge, or a guest
    // already ready when the stream begins, is the sync point (7.3).
    const bool ready = event.guest_ready();
    if (ready && guest_was_ready_ != true) {
        Q_EMIT guest_ready();
    }
    guest_was_ready_ = ready;
}

void MachineSession::on_status_end(unsigned generation, const StreamEnd& end) {
    if (generation != generation_) {
        return;
    }
    const Liveness before = liveness_.state();
    liveness_.on_stream_end(end.cancelled_by_client);
    liveness_ticker_.stop();
    set_liveness(before);
}

template <typename Result>
void MachineSession::post_call(
    std::function<Result(const std::shared_ptr<grpc::Channel>&, grpc::ClientContext&)> call,
    std::function<void(const Result&)> deliver) {
    if (!rpc_ || !channel_) {
        return;
    }
    const unsigned generation = generation_;
    rpc_->post([this, generation, channel = channel_, call = std::move(call),
                deliver = std::move(deliver)](grpc::ClientContext& context) {
        set_deadline(context);
        Result result = call(channel, context);
        QMetaObject::invokeMethod(
            this,
            [this, generation, result = std::move(result), deliver] {
                if (generation == generation_) {
                    deliver(result);
                }
            },
            Qt::QueuedConnection);
    });
}

void MachineSession::set_launched_server(const LaunchedServer& server) {
    launched_ = server;
    run_on_connect_ = true;
}

void MachineSession::run() {
    // A Run during a hold for the sound is that Run: the hold must not
    // send another.
    run_hold_.cancel();
    post_call<CallOutcome>(
        [](const std::shared_ptr<grpc::Channel>& channel, grpc::ClientContext& context) {
            auto stub = beebium::DebuggerControl::NewStub(channel);
            beebium::RunResponse response;
            CallOutcome outcome = outcome_of(stub->Run(&context, beebium::RunRequest{}, &response));
            if (outcome.ok && !response.success()) {
                outcome.ok = false;
                outcome.message = QString::fromStdString(response.error());
            }
            return outcome;
        },
        [this](const CallOutcome& outcome) {
            if (outcome.ok && !machine_running_) {
                machine_running_ = true;
                Q_EMIT running_changed(true);
            }
            Q_EMIT run_finished(outcome);
        });
}

void MachineSession::refresh_system_info() {
    struct Info {
        CallOutcome outcome;
        beebium::SystemInfo info;
    };
    post_call<Info>(
        [](const std::shared_ptr<grpc::Channel>& channel, grpc::ClientContext& context) {
            auto stub = beebium::SystemService::NewStub(channel);
            Info info;
            info.outcome =
                outcome_of(stub->GetSystemInfo(&context, beebium::GetSystemInfoRequest{}, &info.info));
            return info;
        },
        [this](const Info& info) {
            if (!info.outcome.ok) {
                return;
            }
            machine_saved_ = info.info.machine_saved();
            machine_dirpath_ = QString::fromStdString(info.info.machine_dirpath());
            Q_EMIT system_info_refreshed(info.info.connections().client_count());
        });
}

void MachineSession::save(const QString& destination_dirpath, bool replace_existing) {
    struct Saved {
        CallOutcome outcome;
        beebium::SaveMachineResponse response;
    };
    const std::string destination = destination_dirpath.toStdString();
    post_call<Saved>(
        [destination, replace_existing](const std::shared_ptr<grpc::Channel>& channel,
                                         grpc::ClientContext& context) {
            auto stub = beebium::SystemService::NewStub(channel);
            beebium::SaveMachineRequest request;
            request.set_destination_dirpath(destination);
            request.set_replace_existing(replace_existing);
            Saved saved;
            saved.outcome = outcome_of(stub->SaveMachine(&context, request, &saved.response));
            return saved;
        },
        [this](const Saved& saved) {
            if (saved.outcome.ok) {
                machine_saved_ = true;
                machine_dirpath_ = QString::fromStdString(saved.response.machine_dirpath());
                if (template_media_written_) {
                    template_media_written_ = false;
                    Q_EMIT unsaved_changes_changed(false);
                }
            }
            Q_EMIT save_finished(saved.outcome);
        });
}

void MachineSession::move(const QString& destination_dirpath) {
    struct Moved {
        CallOutcome outcome;
        beebium::MoveMachineResponse response;
    };
    const std::string destination = destination_dirpath.toStdString();
    post_call<Moved>(
        [destination](const std::shared_ptr<grpc::Channel>& channel, grpc::ClientContext& context) {
            auto stub = beebium::SystemService::NewStub(channel);
            beebium::MoveMachineRequest request;
            request.set_destination_dirpath(destination);
            Moved moved;
            moved.outcome = outcome_of(stub->MoveMachine(&context, request, &moved.response));
            return moved;
        },
        [this](const Moved& moved) {
            if (moved.outcome.ok) {
                machine_dirpath_ = QString::fromStdString(moved.response.machine_dirpath());
            }
            Q_EMIT move_finished(moved.outcome);
        });
}

void MachineSession::set_name(const QString& name) {
    const std::string text = name.toStdString();
    post_call<CallOutcome>(
        [text](const std::shared_ptr<grpc::Channel>& channel, grpc::ClientContext& context) {
            auto stub = beebium::SystemService::NewStub(channel);
            beebium::SetMachineNameRequest request;
            request.set_name(text);
            beebium::SetMachineNameResponse response;
            return outcome_of(stub->SetMachineName(&context, request, &response));
        },
        [this](const CallOutcome& outcome) {
            if (outcome.ok && machine_saved_) {
                // A saved machine's rename moved its directory, which the
                // answer does not say (5.4).
                refresh_system_info();
            }
            Q_EMIT name_set(outcome);
        });
}

void MachineSession::reset_battery() {
    struct Reset {
        CallOutcome outcome;
        QStringList units;
    };
    post_call<Reset>(
        [](const std::shared_ptr<grpc::Channel>& channel, grpc::ClientContext& context) {
            auto stub = beebium::SystemService::NewStub(channel);
            beebium::ResetBatteryResponse response;
            Reset reset;
            reset.outcome =
                outcome_of(stub->ResetBattery(&context, beebium::ResetBatteryRequest{}, &response));
            for (const auto& unit : response.reset_units()) {
                reset.units << QString::fromStdString(unit);
            }
            return reset;
        },
        [this](const Reset& reset) { Q_EMIT battery_reset(reset.outcome, reset.units); });
}

void MachineSession::request_shutdown() {
    // The server's graceful shutdown waits for our streams, so they go first
    // (4.5); the channel stays for the answer.
    liveness_ticker_.stop();
    ++generation_;
    stop_streams();
    const unsigned generation = generation_;
    const std::string authority = launched_ ? launched_->provenance_uuid.toStdString() : std::string();
    if (!rpc_ || !channel_) {
        Q_EMIT shutdown_answered({false, grpc::StatusCode::UNAVAILABLE, tr("not connected")});
        return;
    }
    rpc_->post([this, generation, channel = channel_, authority](grpc::ClientContext& context) {
        auto stub = beebium::SystemService::NewStub(channel);
        set_deadline(context);
        if (!authority.empty()) {
            context.AddMetadata("x-beebium-instance-uuid", authority);
        }
        beebium::ShutdownRequest request;
        request.set_mode(beebium::SHUTDOWN_GRACEFUL);
        beebium::ShutdownResponse response;
        CallOutcome outcome = outcome_of(stub->RequestShutdown(&context, request, &response));
        if (outcome.ok && !response.accepted()) {
            outcome.ok = false;
            outcome.message = QString::fromStdString(response.message());
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, outcome] {
                if (generation == generation_) {
                    Q_EMIT shutdown_answered(outcome);
                }
            },
            Qt::QueuedConnection);
    });
}

void MachineSession::release_from_launcher() {
    const std::string authority = launched_ ? launched_->provenance_uuid.toStdString() : std::string();
    if (!rpc_ || !channel_ || authority.empty()) {
        Q_EMIT released_from_launcher({false, grpc::StatusCode::UNAVAILABLE, tr("not connected")});
        return;
    }
    const unsigned generation = generation_;
    rpc_->post([this, generation, channel = channel_, authority](grpc::ClientContext& context) {
        auto stub = beebium::SystemService::NewStub(channel);
        set_deadline(context);
        context.AddMetadata("x-beebium-instance-uuid", authority);
        beebium::ReleaseFromLauncherResponse response;
        const CallOutcome outcome = outcome_of(
            stub->ReleaseFromLauncher(&context, beebium::ReleaseFromLauncherRequest{}, &response));
        QMetaObject::invokeMethod(
            this,
            [this, generation, outcome] {
                if (generation == generation_) {
                    Q_EMIT released_from_launcher(outcome);
                }
            },
            Qt::QueuedConnection);
    });
}

void MachineSession::tick_liveness(LivenessTracker::Clock::time_point now) {
    const Liveness before = liveness_.state();
    liveness_.on_tick(now);
    set_liveness(before);
}

void MachineSession::set_liveness(Liveness before) {
    if (liveness_.state() != before) {
        Q_EMIT liveness_changed(liveness_.state());
    }
}

void MachineSession::stop_streams() {
    run_hold_.cancel();
    if (keys_) {
        // Drains what is queued first, so a release-all made just before
        // disconnecting still reaches the machine.
        keys_->stop();
        keys_.reset();
    }
    key_table_ = BbcKeyTable();
    if (status_) {
        status_->stop();
        status_.reset();
    }
    if (video_) {
        video_->stop();
        video_.reset();
    }
    audio_->stop();
    indicators_->stop();
    discs_->stop();
    typing_->stop();
}

void MachineSession::disconnect_from_server() {
    ++generation_;
    liveness_ticker_.stop();
    stop_streams();
    if (rpc_) {
        // Prompt: the call in hand is cancelled and the rest dropped.
        rpc_->stop();
        rpc_.reset();
    }
    if (handshake_canceller_) {
        handshake_canceller_->cancel();
    }
    if (handshake_thread_.joinable()) {
        // Prompt: its calls were just cancelled.
        handshake_thread_.join();
    }
    channel_.reset();
    connected_ = false;
    server_shares_filesystem_ = false;
    template_media_written_ = false;
}

}  // namespace beebium::linux_client
