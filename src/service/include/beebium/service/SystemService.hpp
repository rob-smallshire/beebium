// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP
#define BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP

#include "system.grpc.pb.h"
#include "beebium/PacingClock.hpp"
#include "beebium/PlatformUtils.hpp"
#include "beebium/service/ConnectionTracker.hpp"
#include "beebium/service/HostFingerprint.hpp"
#include "beebium/service/ProtocolFingerprint.hpp"
#include "beebium/service/ShutdownPolicy.hpp"
#include "beebium/service/ShutdownCoordinator.hpp"
#include <beebium/discovery/Advertiser.hpp>
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/AunBackend.hpp"
#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace beebium::service {

/// Launch provenance information
/// Identifies who/what launched the emulator core and when
struct Provenance {
    std::string type;
    std::string instance_uuid;
    std::string version;
    std::chrono::system_clock::time_point timestamp;
};

/// Machine identity information
/// Stable UUID and mutable name for identification and labeling
struct MachineIdentity {
    std::string uuid;       // RFC 4122 v4 UUID, stable for machine lifetime
    std::string name;       // User-assignable label, mutable
    std::string model_type; // e.g., "ModelB" (immutable)
    std::string model_name; // e.g., "BBC Model B" (immutable)
};

/// gRPC service implementation for SystemService
/// Provides machine configuration and identity information
template<typename MachineType>
class SystemServiceImpl final : public SystemService::Service {
public:
    /// Callback invoked when shutdown should proceed (after coordination).
    using ShutdownCallback = std::function<void()>;

    SystemServiceImpl(MachineType& machine, Provenance provenance, MachineIdentity identity,
                      ConnectionTracker* connection_tracker,
                      discovery::Advertiser* advertiser = nullptr,
                      uint16_t server_port = 0,
                      uint32_t clock_speed_hz = 0,
                      ShutdownPolicyConfig policy_config = {},
                      ShutdownCoordinator* shutdown_coordinator = nullptr,
                      ShutdownCallback shutdown_callback = nullptr);
    ~SystemServiceImpl() override = default;

    // Non-copyable
    SystemServiceImpl(const SystemServiceImpl&) = delete;
    SystemServiceImpl& operator=(const SystemServiceImpl&) = delete;

    /// Seed the configured speed multiplier (from the --speed CLI flag)
    /// before the pacing clock exists. The clock is created and wired only
    /// once the emulation loop starts, but clients may query or set the speed
    /// as soon as the gRPC server is listening; this keeps the configured
    /// value available throughout, independent of the clock's lifetime.
    void set_initial_speed_multiplier(double multiplier) {
        configured_speed_multiplier_.store(multiplier, std::memory_order_relaxed);
    }

    /// Set the pacing clock for stats monitoring. Call after construction.
    /// Adopts the configured multiplier so any speed set before the clock
    /// existed (e.g. during the brief startup window) takes effect.
    void set_pacing_clock(PacingClock* clock) {
        pacing_clock_ = clock;
        if (clock) {
            clock->set_speed_multiplier(
                configured_speed_multiplier_.load(std::memory_order_relaxed));
        }
    }

    grpc::Status GetSystemInfo(
        grpc::ServerContext* context,
        const GetSystemInfoRequest* request,
        SystemInfo* response) override;

    grpc::Status SetMachineName(
        grpc::ServerContext* context,
        const SetMachineNameRequest* request,
        SetMachineNameResponse* response) override;

    grpc::Status WatchServerStatus(
        grpc::ServerContext* context,
        const WatchServerStatusRequest* request,
        grpc::ServerWriter<ServerStatusEvent>* writer) override;

    grpc::Status RequestShutdown(
        grpc::ServerContext* context,
        const ShutdownRequest* request,
        ShutdownResponse* response) override;

    grpc::Status GetAdvertisementState(
        grpc::ServerContext* context,
        const GetAdvertisementStateRequest* request,
        GetAdvertisementStateResponse* response) override;

    grpc::Status SetAdvertisement(
        grpc::ServerContext* context,
        const SetAdvertisementRequest* request,
        SetAdvertisementResponse* response) override;

    grpc::Status GetPacingStats(
        grpc::ServerContext* context,
        const GetPacingStatsRequest* request,
        beebium::PacingStats* response) override;

    grpc::Status WatchPacingStats(
        grpc::ServerContext* context,
        const WatchPacingStatsRequest* request,
        grpc::ServerWriter<beebium::PacingStats>* writer) override;

    grpc::Status SetSpeedMultiplier(
        grpc::ServerContext* context,
        const SetSpeedMultiplierRequest* request,
        SetSpeedMultiplierResponse* response) override;

    /// Notify all watchers that shutdown is imminent.
    /// Called from signal handler or shutdown path.
    /// Thread-safe: can be called from any thread.
    void notify_shutdown(uint32_t grace_ms = 5000);

    /// Set the server port after it's determined.
    /// Must be called before advertisement can work correctly.
    void set_server_port(uint16_t port);

private:
    /// Notify watchers that identity has changed.
    void notify_identity_changed();

    /// Populate a protobuf MachineIdentity message from the current identity.
    /// Caller must hold identity_mutex_ (unless called from construction).
    void populate_identity_proto(beebium::MachineIdentity* proto) const;

    /// Extract instance UUID from gRPC metadata.
    static std::string extract_instance_uuid(grpc::ServerContext* context);

    /// Execute shutdown sequence in background thread.
    void execute_shutdown(ShutdownMode mode, uint32_t grace_ms);

    MachineType& machine_;
    Provenance provenance_;
    MachineIdentity identity_;
    ConnectionTracker* connection_tracker_;
    discovery::Advertiser* advertiser_;
    ShutdownPolicyEvaluator policy_evaluator_;
    ShutdownCoordinator* shutdown_coordinator_;
    ShutdownCallback shutdown_callback_;
    uint16_t server_port_;
    uint32_t clock_speed_hz_;
    PacingClock* pacing_clock_ = nullptr;
    // The requested speed multiplier. Owned by the service so it survives the
    // pacing clock's lifetime (the clock is a stack local in the emulation
    // loop, created after the gRPC server starts listening).
    std::atomic<double> configured_speed_multiplier_{1.0};

    // Synchronization for identity changes and shutdown notification
    mutable std::mutex watchers_mutex_;
    std::condition_variable watchers_cv_;
    std::atomic<bool> shutdown_signaled_{false};
    std::atomic<uint32_t> shutdown_grace_ms_{5000};
    std::atomic<bool> identity_changed_{false};
};

//////////////////////////////////////////////////////////////////////////////
// SystemServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

template<typename MachineType>
SystemServiceImpl<MachineType>::SystemServiceImpl(
    MachineType& machine, Provenance provenance, MachineIdentity identity,
    ConnectionTracker* connection_tracker,
    discovery::Advertiser* advertiser,
    uint16_t server_port,
    uint32_t clock_speed_hz,
    ShutdownPolicyConfig policy_config,
    ShutdownCoordinator* shutdown_coordinator,
    ShutdownCallback shutdown_callback)
    : machine_(machine)
    , provenance_(provenance)
    , identity_(std::move(identity))
    , connection_tracker_(connection_tracker)
    , advertiser_(advertiser)
    , policy_evaluator_(provenance.instance_uuid, policy_config)
    , shutdown_coordinator_(shutdown_coordinator)
    , shutdown_callback_(std::move(shutdown_callback))
    , server_port_(server_port)
    , clock_speed_hz_(clock_speed_hz) {
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::populate_identity_proto(
    beebium::MachineIdentity* proto) const {
    proto->set_uuid(identity_.uuid);
    proto->set_name(identity_.name);
    proto->set_model_type(identity_.model_type);
    proto->set_model_name(identity_.model_name);
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::GetSystemInfo(
    grpc::ServerContext* /*context*/,
    const GetSystemInfoRequest* /*request*/,
    SystemInfo* response) {

    // Set provenance information
    auto* prov = response->mutable_provenance();
    prov->set_type(provenance_.type);
    prov->set_instance_uuid(provenance_.instance_uuid);
    prov->set_version(provenance_.version);
    // Convert time_point to Unix timestamp (seconds since epoch)
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        provenance_.timestamp.time_since_epoch()).count();
    prov->set_timestamp(seconds);

    // Set identity information (thread-safe access to mutable name)
    auto* id = response->mutable_identity();
    {
        std::lock_guard<std::mutex> lock(watchers_mutex_);
        populate_identity_proto(id);
    }

    // Set connection information
    auto* conn = response->mutable_connections();
    conn->set_client_count(connection_tracker_ ? connection_tracker_->client_count() : 0);

    // Set clock frequency
    response->set_clock_speed_hz(clock_speed_hz_);

    // Report the wire-protocol fingerprint the server was built against, so a
    // connecting client can refuse to proceed on a contract mismatch, and the
    // path of this executable so the client can say which binary that was.
    response->set_protocol_fingerprint(std::string(PROTOCOL_FINGERPRINT));
    response->set_host_fingerprint(host_fingerprint());

    if (auto exe_path = beebium::platform::executable_path()) {
        response->set_executable_path(exe_path->string());
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::SetMachineName(
    grpc::ServerContext* /*context*/,
    const SetMachineNameRequest* request,
    SetMachineNameResponse* response) {

    // Validate name is not empty
    if (request->name().empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Name cannot be empty");
    }

    // Update the name
    {
        std::lock_guard<std::mutex> lock(watchers_mutex_);
        identity_.name = request->name();
        populate_identity_proto(response->mutable_identity());
    }

    // Notify watchers of the change
    notify_identity_changed();

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::WatchServerStatus(
    grpc::ServerContext* context,
    const WatchServerStatusRequest* /*request*/,
    grpc::ServerWriter<ServerStatusEvent>* writer) {

    // Track this connection (RAII guard decrements on any exit path)
    std::unique_ptr<ConnectionGuard> guard;
    if (connection_tracker_) {
        guard = std::make_unique<ConnectionGuard>(*connection_tracker_);
    }

    // Send READY event immediately upon subscription
    ServerStatusEvent ready_event;
    ready_event.set_status(SERVER_STATUS_READY);
    ready_event.set_message("Server ready");
    if (!writer->Write(ready_event)) {
        // Client disconnected before we could send READY
        return grpc::Status::OK;
    }

    // Emit a liveness heartbeat on an otherwise-idle stream at a brisk cadence,
    // so a client can detect a silently-unreachable server by their absence
    // within a couple of seconds (its timeout is a few of these intervals).
    const auto heartbeat_interval = std::chrono::milliseconds(500);
    auto last_heartbeat = std::chrono::steady_clock::now();

    // Wait for events in a loop
    // Use wait_for with timeout to periodically check for client cancellation,
    // since context->IsCancelled() doesn't notify the condition variable.
    while (!context->IsCancelled()) {
        std::unique_lock<std::mutex> lock(watchers_mutex_);
        watchers_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, context] {
            return shutdown_signaled_.load() ||
                   identity_changed_.load() ||
                   context->IsCancelled();
        });

        if (context->IsCancelled()) {
            break;
        }

        // Periodic liveness heartbeat.
        auto now = std::chrono::steady_clock::now();
        if (now - last_heartbeat >= heartbeat_interval) {
            last_heartbeat = now;
            ServerStatusEvent heartbeat;
            heartbeat.set_status(SERVER_STATUS_HEARTBEAT);
            if (!writer->Write(heartbeat)) {
                break;
            }
        }

        // Handle identity change event
        if (identity_changed_.exchange(false)) {
            ServerStatusEvent event;
            event.set_status(SERVER_STATUS_IDENTITY_CHANGED);
            event.set_message("Machine identity changed");
            populate_identity_proto(event.mutable_identity());
            if (!writer->Write(event)) {
                break;
            }
        }

        // Handle shutdown event (terminal - exit the loop after sending)
        if (shutdown_signaled_.load()) {
            ServerStatusEvent event;
            event.set_status(SERVER_STATUS_SHUTTING_DOWN);
            event.set_message("Server shutting down");
            event.set_shutdown_grace_ms(shutdown_grace_ms_.load());
            writer->Write(event);
            break;
        }
    }

    return grpc::Status::OK;
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::notify_shutdown(uint32_t grace_ms) {
    shutdown_grace_ms_.store(grace_ms);
    shutdown_signaled_.store(true);
    watchers_cv_.notify_all();
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::notify_identity_changed() {
    identity_changed_.store(true);
    watchers_cv_.notify_all();
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::set_server_port(uint16_t port) {
    server_port_ = port;
}

template<typename MachineType>
std::string SystemServiceImpl<MachineType>::extract_instance_uuid(grpc::ServerContext* context) {
    auto metadata = context->client_metadata();
    auto it = metadata.find("x-beebium-instance-uuid");
    if (it != metadata.end()) {
        return std::string(it->second.data(), it->second.size());
    }
    return "";  // Empty = no match possible
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::RequestShutdown(
    grpc::ServerContext* context,
    const ShutdownRequest* request,
    ShutdownResponse* response) {

    // Check if already shutting down
    if (shutdown_signaled_.load()) {
        response->set_accepted(true);
        response->set_message("Shutdown already in progress");
        return grpc::Status::OK;
    }

    // Extract requester's instance UUID from metadata
    std::string requester_uuid = extract_instance_uuid(context);

    // Get current client count
    int client_count = connection_tracker_ ? connection_tracker_->client_count() : 0;

    // Evaluate policy
    auto [accepted, message] = policy_evaluator_.evaluate(requester_uuid, client_count);
    response->set_accepted(accepted);
    response->set_message(message);

    if (accepted) {
        uint32_t grace_ms = request->grace_period_ms();
        if (grace_ms == 0) {
            grace_ms = 5000;  // Default grace period
        }

        // Execute shutdown in background thread
        std::thread([this, mode = request->mode(), grace_ms] {
            execute_shutdown(mode, grace_ms);
        }).detach();
    }

    return grpc::Status::OK;
}

template<typename MachineType>
void SystemServiceImpl<MachineType>::execute_shutdown(ShutdownMode mode, uint32_t grace_ms) {
    // Notify watchers that shutdown is starting
    notify_shutdown(grace_ms);

    if (mode == SHUTDOWN_IMMEDIATE) {
        // Skip coordination, proceed directly
        if (shutdown_callback_) {
            shutdown_callback_();
        }
        return;
    }

    // Graceful: coordinate subsystem shutdown first
    if (shutdown_coordinator_ && shutdown_coordinator_->has_conditions()) {
        // TODO: Report progress via WatchServerStatus
        auto timed_out = shutdown_coordinator_->coordinate_shutdown();

        // Log any timeouts (we proceed anyway)
        (void)timed_out;  // Suppress unused warning for now
    }

    // Proceed with actual shutdown
    if (shutdown_callback_) {
        shutdown_callback_();
    }
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::GetAdvertisementState(
    grpc::ServerContext* /*context*/,
    const GetAdvertisementStateRequest* /*request*/,
    GetAdvertisementStateResponse* response) {

    auto* state = response->mutable_state();

    if (advertiser_) {
        auto adv_state = advertiser_->state();
        state->set_available(adv_state.available);
        state->set_enabled(adv_state.advertising);
        state->set_advertised_name(adv_state.actual_name);
    } else {
        // No advertiser configured
        state->set_available(false);
        state->set_enabled(false);
        state->set_advertised_name("");
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::SetAdvertisement(
    grpc::ServerContext* /*context*/,
    const SetAdvertisementRequest* request,
    SetAdvertisementResponse* response) {

    auto* state = response->mutable_state();

    if (!advertiser_) {
        // No advertiser configured
        state->set_available(false);
        state->set_enabled(false);
        state->set_advertised_name("");
        return grpc::Status::OK;
    }

    if (request->enabled()) {
        // Start advertising
        // Build ServiceInfo from current identity
        discovery::ServiceInfo info;
        {
            std::lock_guard<std::mutex> lock(watchers_mutex_);
            info.instance_name = identity_.name;
        }
        info.port = server_port_;
        info.txt_records["uuid"] = identity_.uuid;
        info.txt_records["model"] = identity_.model_type;
        info.txt_records["provenance"] = provenance_.type;

        using Memory = typename MachineType::Memory;
        if constexpr (HasEconetSocket<Memory>) {
            auto& econet = machine_.state().memory.econet_socket;
            if (econet.enabled()) {
                info.txt_records["econet_station"] = std::to_string(econet.station_id());
                if (auto* aun = dynamic_cast<AunBackend*>(econet.backend())) {
                    info.txt_records["econet_net"] = std::to_string(aun->local_net());
                    info.txt_records["econet_aun_port"] = std::to_string(aun->local_port());
                }
            }
        }

        advertiser_->start(info);
    } else {
        // Stop advertising
        advertiser_->stop();
    }

    // Return current state
    auto adv_state = advertiser_->state();
    state->set_available(adv_state.available);
    state->set_enabled(adv_state.advertising);
    state->set_advertised_name(adv_state.actual_name);

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::GetPacingStats(
    grpc::ServerContext* /*context*/,
    const GetPacingStatsRequest* /*request*/,
    beebium::PacingStats* response) {

    if (!pacing_clock_) {
        // The clock is wired only once the emulation loop starts. Until then
        // report the configured multiplier; the remaining fields are runtime
        // measurements that need a running clock, so they stay zero.
        response->set_speed_multiplier(
            configured_speed_multiplier_.load(std::memory_order_relaxed));
        return grpc::Status::OK;
    }
    auto stats = pacing_clock_->timing_stats();
    response->set_ticks_executed(stats.ticks_executed);
    response->set_ticks_io_skipped(stats.ticks_io_woken);
    response->set_controller_drift(stats.controller_deficit);
    response->set_controller_integral(stats.controller_integral);
    response->set_speed_multiplier(pacing_clock_->speed_multiplier());
    response->set_achieved_speed_multiplier(
        pacing_clock_->achieved_speed_multiplier());
    response->set_estimated_max_speed_multiplier(
        pacing_clock_->estimated_max_speed_multiplier());
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::WatchPacingStats(
    grpc::ServerContext* context,
    const WatchPacingStatsRequest* request,
    grpc::ServerWriter<beebium::PacingStats>* writer) {

    if (!pacing_clock_) {
        return {grpc::StatusCode::UNAVAILABLE, "Pacing clock not configured"};
    }

    auto interval = std::chrono::milliseconds(
        request->interval_ms() > 0 ? request->interval_ms() : 1000);

    while (!context->IsCancelled()) {
        std::this_thread::sleep_for(interval);

        auto stats = pacing_clock_->timing_stats();
        beebium::PacingStats msg;
        msg.set_ticks_executed(stats.ticks_executed);
        msg.set_ticks_io_skipped(stats.ticks_io_woken);
        msg.set_controller_drift(stats.controller_deficit);
        msg.set_controller_integral(stats.controller_integral);
        msg.set_speed_multiplier(pacing_clock_->speed_multiplier());
        msg.set_achieved_speed_multiplier(
            pacing_clock_->achieved_speed_multiplier());
        msg.set_estimated_max_speed_multiplier(
            pacing_clock_->estimated_max_speed_multiplier());

        if (!writer->Write(msg)) break;
    }
    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status SystemServiceImpl<MachineType>::SetSpeedMultiplier(
    grpc::ServerContext* /*context*/,
    const SetSpeedMultiplierRequest* request,
    SetSpeedMultiplierResponse* response) {

    const double speed_multiplier = request->speed_multiplier();
    if (!std::isfinite(speed_multiplier)) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "Speed multiplier must be finite"};
    }
    if (speed_multiplier < 0.0) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "Speed multiplier must be greater than or equal to zero"};
    }

    // Record the request so it survives the clock's lifetime and is adopted
    // when the clock is wired (set_pacing_clock). The clock may exist already
    // (machine running) or not yet (brief startup window).
    configured_speed_multiplier_.store(speed_multiplier, std::memory_order_relaxed);
    if (pacing_clock_) {
        pacing_clock_->set_speed_multiplier(speed_multiplier);
        response->set_speed_multiplier(pacing_clock_->speed_multiplier());
    } else {
        response->set_speed_multiplier(speed_multiplier);
    }
    return grpc::Status::OK;
}

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SYSTEM_SERVICE_HPP
