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

#include "beebium/service/Server.hpp"
#include "beebium/service/VideoService.hpp"
#include "beebium/service/KeyboardService.hpp"
#include "beebium/service/DebuggerService.hpp"
#include "beebium/Machines.hpp"
#include "beebium/FrameBuffer.hpp"
#include "beebium/FrameRenderer.hpp"

#include <grpcpp/grpcpp.h>
#include <sstream>
#include <thread>

namespace beebium::service {

struct Server::Impl {
    ModelB& machine;
    std::string address;
    uint16_t port;

    // Video rendering infrastructure
    FrameBuffer frame_buffer;
    FrameRenderer frame_renderer{&frame_buffer};

    std::unique_ptr<VideoServiceImpl> video_service;
    std::unique_ptr<KeyboardServiceImpl> keyboard_service;
    std::unique_ptr<DebuggerControlServiceImpl> debugger_control_service;
    std::unique_ptr<Debugger6502ServiceImpl> debugger_6502_service;
    std::unique_ptr<grpc::Server> grpc_server;

    std::atomic<bool> running{false};
    std::thread render_thread;

    Impl(ModelB& m, const std::string& addr, uint16_t p)
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

Server::Server(ModelB& machine, const std::string& address, uint16_t port)
    : impl_(std::make_unique<Impl>(machine, address, port)) {
}

Server::~Server() {
    stop();
}

void Server::start() {
    if (impl_->running) {
        return;
    }

    // Create services
    impl_->video_service = std::make_unique<VideoServiceImpl>(impl_->frame_buffer);

    impl_->keyboard_service = std::make_unique<KeyboardServiceImpl>(
        impl_->machine.state().memory.system_via_peripheral);

    impl_->debugger_control_service = std::make_unique<DebuggerControlServiceImpl>(
        impl_->machine);

    impl_->debugger_6502_service = std::make_unique<Debugger6502ServiceImpl>(
        impl_->machine);

    // Build server address
    std::ostringstream addr_stream;
    addr_stream << impl_->address << ":" << impl_->port;
    std::string server_address = addr_stream.str();

    // Create and start gRPC server
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(impl_->video_service.get());
    builder.RegisterService(impl_->keyboard_service.get());
    builder.RegisterService(impl_->debugger_control_service.get());
    builder.RegisterService(impl_->debugger_6502_service.get());

    impl_->grpc_server = builder.BuildAndStart();
    impl_->running = true;

    // Start render thread
    impl_->render_thread = std::thread(&Impl::render_loop, impl_.get());
}

void Server::stop() {
    if (!impl_->running) {
        return;
    }

    impl_->running = false;

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
    impl_->debugger_6502_service.reset();
}

bool Server::is_running() const {
    return impl_->running;
}

std::string Server::address() const {
    return impl_->address;
}

uint16_t Server::port() const {
    return impl_->port;
}

} // namespace beebium::service
