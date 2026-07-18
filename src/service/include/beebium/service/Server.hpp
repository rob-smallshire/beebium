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

#ifndef BEEBIUM_SERVICE_SERVER_HPP
#define BEEBIUM_SERVICE_SERVER_HPP

#include "beebium/service/VideoService.hpp"
#include "beebium/service/KeyboardService.hpp"
#include "beebium/service/DebuggerService.hpp"
#include "beebium/service/DeviceInspectionService.hpp"
#include "beebium/service/DiscService.hpp"
#include "beebium/service/IndicatorService.hpp"
#include "beebium/service/SystemService.hpp"
#include "beebium/service/AudioService.hpp"
#include "beebium/service/SidewaysService.hpp"
#include "beebium/service/EconetService.hpp"
#include "beebium/service/SerialService.hpp"
#include "beebium/service/TubeService.hpp"
#include "beebium/service/ConnectionTracker.hpp"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/service/ShutdownCoordinator.hpp"
#include "beebium/service/ShutdownPolicy.hpp"
#include <beebium/discovery/Advertiser.hpp>
#include "beebium/FrameBuffer.hpp"
#include "beebium/TeletextGrid.hpp"
#include "beebium/FrameRenderer.hpp"

#include <grpcpp/grpcpp.h>
#include <memory>
#include <span>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>

namespace beebium {
namespace service {

/// gRPC server hosting Beebium services
template<typename MachineType>
class Server {
public:
    /// Create server bound to the given address and port
    explicit Server(MachineType& machine, const std::string& address = "127.0.0.1",
                    uint16_t port = 50051);
    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Callback type for shutdown requests from clients.
    using ShutdownCallback = std::function<void()>;

    /// Start the server (non-blocking)
    /// @param provenance Launch provenance information for this server instance
    /// @param identity Machine identity (UUID and name) for this server instance
    /// @param enable_advertisement Start mDNS advertisement on startup
    /// @param policy_config Shutdown policy configuration
    /// @param shutdown_callback Callback to invoke when client requests shutdown
    /// @param extension_services Additional gRPC services from peripheral extensions
    void start(Provenance provenance, MachineIdentity identity,
               bool enable_advertisement = false,
               ShutdownPolicyConfig policy_config = {},
               ShutdownCallback shutdown_callback = nullptr,
               std::span<grpc::Service*> extension_services = {});

    /// Stop the server and wait for shutdown
    void stop();

    /// Check if server is running
    bool is_running() const;

    /// Get the address the server is bound to
    std::string address() const;

    /// Get the port the server is bound to
    uint16_t port() const;

    /// Notify all connected clients that shutdown is imminent.
    /// Call this from signal handler before stopping the server.
    /// @param grace_ms Grace period in milliseconds for clients to disconnect
    void notify_shutdown(uint32_t grace_ms = 5000);

    /// Access the TubeService (for host startup to set shared memory pointer).
    TubeServiceImpl<MachineType>* tube_service() { return impl_->tube_service.get(); }

    /// Access the SidewaysService. Returns nullptr until start() has been
    /// called, since services are constructed during start. Use
    /// set_motherboard_links() before start() to configure link state.
    SidewaysServiceImpl<MachineType>* sideways_service() {
        return impl_->sideways_service.get();
    }

    /// Configure the motherboard link state to apply when the sideways
    /// service is created during start(). Must be called before start();
    /// has no effect afterwards. Motherboard links are not runtime mutable.
    void set_motherboard_links(
        const typename MachineType::Memory::MotherboardLinks& links) {
        impl_->motherboard_links = links;
    }

    /// Access the host DebuggerService (for wiring cross-processor stop).
    DebuggerControlServiceImpl<MachineType>& debugger_service() {
        return *impl_->debugger_control_service;
    }

    /// Seed the configured speed multiplier before the pacing clock exists,
    /// so clients can query and set the speed as soon as the server listens.
    void set_initial_speed_multiplier(double multiplier) {
        if (impl_->system_service) {
            impl_->system_service->set_initial_speed_multiplier(multiplier);
        }
    }

    /// Set the pacing clock for gRPC stats monitoring.
    void set_pacing_clock(PacingClock* clock) {
        if (impl_->system_service) {
            impl_->system_service->set_pacing_clock(clock);
        }
    }

private:
    struct Impl {
        MachineType& machine;
        std::string address;
        uint16_t port;

        // Video rendering infrastructure
        FrameBuffer frame_buffer;
        FrameRenderer frame_renderer{&frame_buffer};

        // Connection tracking (must be declared before services that use it)
        ConnectionTracker connection_tracker;

        TeletextGrid teletext_grid;
        std::unique_ptr<VideoServiceImpl> video_service;
        std::unique_ptr<KeyboardServiceImpl> keyboard_service;
        std::unique_ptr<DebuggerControlServiceImpl<MachineType>> debugger_control_service;
        std::unique_ptr<DeviceInspectionServiceImpl<MachineType>> device_inspection_service;
        std::unique_ptr<DiscServiceImpl<MachineType>> disc_service;
        std::unique_ptr<IndicatorServiceImpl<MachineType>> indicator_service;
        std::unique_ptr<SystemServiceImpl<MachineType>> system_service;
        std::unique_ptr<AudioServiceImpl<MachineType>> audio_service;
        std::unique_ptr<SidewaysServiceImpl<MachineType>> sideways_service;
        // Motherboard link state captured before start(), applied to
        // sideways_service when it is constructed.
        typename MachineType::Memory::MotherboardLinks motherboard_links{};
        std::unique_ptr<EconetServiceImpl<MachineType>> econet_service;
        std::unique_ptr<SerialServiceImpl<MachineType>> serial_service;
        std::unique_ptr<TubeServiceImpl<MachineType>> tube_service;
        std::unique_ptr<grpc::Server> grpc_server;
        std::unique_ptr<discovery::Advertiser> advertiser;

        std::atomic<bool> running{false};
        std::thread render_thread;

        Impl(MachineType& m, const std::string& addr, uint16_t p)
            : machine(m), address(addr), port(p) {}

        // Background thread that consumes video_output queue and renders to frame_buffer
        void render_loop() {
            while (running) {
                if (machine.state().memory.video_output) {
                    // Process available pixel batches
                    size_t processed = frame_renderer.process(
                        machine.state().memory.video_output.value(), 10000);

                    if (processed == 0) {
                        // No work available, brief sleep to avoid busy-waiting
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                } else {
                    // Video output not enabled, wait longer
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
    };

    std::unique_ptr<Impl> impl_;
};

// Template implementation

template<typename MachineType>
Server<MachineType>::Server(MachineType& machine, const std::string& address, uint16_t port)
    : impl_(std::make_unique<Impl>(machine, address, port)) {
}

template<typename MachineType>
Server<MachineType>::~Server() {
    stop();
}

template<typename MachineType>
void Server<MachineType>::start(Provenance provenance, MachineIdentity identity,
                                bool enable_advertisement,
                                ShutdownPolicyConfig policy_config,
                                ShutdownCallback shutdown_callback,
                                std::span<grpc::Service*> extension_services) {
    if (impl_->running) {
        return;
    }

    // Create advertiser (platform-specific implementation)
    impl_->advertiser = discovery::create_advertiser();

    // Save identity info before moving (needed for advertisement later)
    std::string identity_name = identity.name;
    std::string identity_uuid = identity.uuid;
    std::string identity_model_type = identity.model_type;
    std::string provenance_type = provenance.type;

    // Create services
    // The SAA5050 fills the grid as it renders, so a client can read the MODE 7
    // screen as characters. Attached for the server's lifetime; the grid
    // outlives the machine's use of it because both are owned here.
    impl_->machine.state().memory.saa5050.set_teletext_grid(&impl_->teletext_grid);

    impl_->video_service = std::make_unique<VideoServiceImpl>(
        impl_->frame_buffer, impl_->teletext_grid);

    // Create break callbacks that call Machine methods
    BreakCallbacks break_callbacks{
        [this]() { impl_->machine.break_down(); },
        [this]() { impl_->machine.break_up(); },
        [this]() { return impl_->machine.is_in_reset(); }
    };

    impl_->keyboard_service = std::make_unique<KeyboardServiceImpl>(
        impl_->machine.state().memory.system_via_peripheral,
        std::move(break_callbacks));

    impl_->debugger_control_service = std::make_unique<DebuggerControlServiceImpl<MachineType>>(
        impl_->machine);

    impl_->device_inspection_service = std::make_unique<DeviceInspectionServiceImpl<MachineType>>(
        impl_->machine);

    impl_->disc_service = std::make_unique<DiscServiceImpl<MachineType>>(
        impl_->machine);

    impl_->indicator_service = std::make_unique<IndicatorServiceImpl<MachineType>>(
        impl_->machine);

    impl_->system_service = std::make_unique<SystemServiceImpl<MachineType>>(
        impl_->machine, std::move(provenance), std::move(identity),
        &impl_->connection_tracker, impl_->advertiser.get(), 0,
        static_cast<uint32_t>(MachineType::Memory::default_pacing_config().base_clock_hz),
        policy_config, nullptr, std::move(shutdown_callback));

    impl_->audio_service = std::make_unique<AudioServiceImpl<MachineType>>(
        impl_->machine);

    impl_->sideways_service = std::make_unique<SidewaysServiceImpl<MachineType>>(
        impl_->machine);
    impl_->sideways_service->set_motherboard_links(impl_->motherboard_links);

    impl_->econet_service = std::make_unique<EconetServiceImpl<MachineType>>(
        impl_->machine);

    impl_->serial_service = std::make_unique<SerialServiceImpl<MachineType>>(
        impl_->machine);

    impl_->tube_service = std::make_unique<TubeServiceImpl<MachineType>>(
        impl_->machine);

    // Build server address
    std::ostringstream addr_stream;
    addr_stream << impl_->address << ":" << impl_->port;
    std::string server_address = addr_stream.str();

    // Create and start gRPC server
    grpc::ServerBuilder builder;

    // Disable SO_REUSEPORT - we want exclusive port binding for each emulator instance
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);

    int selected_port = 0;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &selected_port);
    builder.RegisterService(impl_->video_service.get());
    builder.RegisterService(impl_->keyboard_service.get());
    builder.RegisterService(impl_->debugger_control_service.get());
    builder.RegisterService(impl_->device_inspection_service.get());
    builder.RegisterService(impl_->disc_service.get());
    builder.RegisterService(impl_->indicator_service.get());
    builder.RegisterService(impl_->system_service.get());
    builder.RegisterService(impl_->audio_service.get());
    builder.RegisterService(impl_->sideways_service.get());
    builder.RegisterService(impl_->econet_service.get());
    builder.RegisterService(impl_->serial_service.get());
    builder.RegisterService(impl_->tube_service.get());

    // Register extension-provided services
    for (auto* svc : extension_services) {
        builder.RegisterService(svc);
    }

    impl_->grpc_server = builder.BuildAndStart();

    // Check if server started successfully
    if (!impl_->grpc_server) {
        throw std::runtime_error("Failed to start gRPC server on " + server_address);
    }

    // Check if port binding succeeded (selected_port is 0 on failure)
    if (selected_port <= 0) {
        impl_->grpc_server->Shutdown();
        impl_->grpc_server.reset();
        throw std::runtime_error("Failed to bind to port " + std::to_string(impl_->port) +
                                 " (port may already be in use)");
    }

    // Update port with the actual bound port (important when port 0 was requested)
    impl_->port = static_cast<uint16_t>(selected_port);

    // Now that we know the actual port, update SystemService
    impl_->system_service->set_server_port(impl_->port);

    // Start mDNS advertisement if enabled
    if (enable_advertisement && impl_->advertiser) {
        discovery::ServiceInfo info;
        info.instance_name = identity_name;
        info.port = impl_->port;
        info.txt_records["uuid"] = identity_uuid;
        info.txt_records["role"] = "host";
        info.txt_records["model"] = identity_model_type;
        info.txt_records["provenance"] = provenance_type;

        using Memory = typename MachineType::Memory;
        if constexpr (HasEconetSocket<Memory>) {
            auto& econet = impl_->machine.state().memory.econet_socket;
            if (econet.enabled()) {
                info.txt_records["econet_station"] = std::to_string(econet.station_id());
                if (auto* aun = dynamic_cast<AunBackend*>(econet.backend())) {
                    info.txt_records["econet_net"] = std::to_string(aun->local_net());
                    info.txt_records["econet_aun_port"] = std::to_string(aun->local_port());
                }
            }
        }

        impl_->advertiser->start(info);
    }

    impl_->running = true;

    // Start render thread
    impl_->render_thread = std::thread(&Impl::render_loop, impl_.get());
}

template<typename MachineType>
void Server<MachineType>::stop() {
    if (!impl_->running) {
        return;
    }

    impl_->running = false;

    // Stop mDNS advertisement
    if (impl_->advertiser) {
        impl_->advertiser->stop();
    }

    // Stop render thread
    if (impl_->render_thread.joinable()) {
        impl_->render_thread.join();
    }

    if (impl_->grpc_server) {
        impl_->grpc_server->Shutdown();
        impl_->grpc_server.reset();
    }

    impl_->video_service.reset();
    impl_->keyboard_service.reset();
    impl_->debugger_control_service.reset();
    impl_->device_inspection_service.reset();
    impl_->disc_service.reset();
    impl_->indicator_service.reset();
    impl_->system_service.reset();
    impl_->audio_service.reset();
    impl_->sideways_service.reset();
    impl_->econet_service.reset();
    impl_->serial_service.reset();
    impl_->tube_service.reset();
}

template<typename MachineType>
bool Server<MachineType>::is_running() const {
    return impl_->running;
}

template<typename MachineType>
std::string Server<MachineType>::address() const {
    return impl_->address;
}

template<typename MachineType>
uint16_t Server<MachineType>::port() const {
    return impl_->port;
}

template<typename MachineType>
void Server<MachineType>::notify_shutdown(uint32_t grace_ms) {
    if (impl_->system_service) {
        impl_->system_service->notify_shutdown(grace_ms);
    }
}

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_SERVER_HPP
