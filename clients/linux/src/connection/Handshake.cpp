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

#include "connection/Handshake.hpp"

#include "system.grpc.pb.h"
#include "video.grpc.pb.h"
#include "debugger.grpc.pb.h"

namespace beebium::linux_client {

namespace {

void set_deadline(grpc::ClientContext& context, std::chrono::milliseconds timeout) {
    context.set_deadline(std::chrono::system_clock::now() + timeout);
}

}  // namespace

HandshakeResult perform_handshake(const std::shared_ptr<grpc::Channel>& channel,
                                  std::string_view client_protocol_fingerprint,
                                  std::string_view client_host_fingerprint,
                                  std::chrono::milliseconds timeout) {
    HandshakeResult result;

    auto system = beebium::SystemService::NewStub(channel);
    beebium::SystemInfo info;
    {
        grpc::ClientContext context;
        set_deadline(context, timeout);
        const grpc::Status status =
            system->GetSystemInfo(&context, beebium::GetSystemInfoRequest{}, &info);
        if (!status.ok()) {
            result.code = status.error_code();
            result.error = status.error_message();
            return result;
        }
    }
    result.server_protocol_fingerprint = info.protocol_fingerprint();
    result.server_executable_path = info.executable_path();
    result.protocol = assess_protocol(info.protocol_fingerprint(), client_protocol_fingerprint);
    result.server_is_local = is_server_local(info.host_fingerprint(), client_host_fingerprint);
    result.machine_uuid = info.identity().uuid();
    result.machine_name = info.identity().name();
    result.machine_description = info.identity().description();
    result.model_name = info.identity().model_name();
    result.client_count = info.connections().client_count();
    result.machine_saved = info.machine_saved();
    result.machine_dirpath = info.machine_dirpath();

    auto video = beebium::VideoService::NewStub(channel);
    beebium::VideoConfig config;
    {
        grpc::ClientContext context;
        set_deadline(context, timeout);
        const grpc::Status status =
            video->GetConfig(&context, beebium::GetConfigRequest{}, &config);
        if (!status.ok()) {
            result.code = status.error_code();
            result.error = status.error_message();
            return result;
        }
    }
    result.video_width = config.width();
    result.video_height = config.height();
    result.video_framerate_hz = config.framerate_hz();

    // Whether it waits for Run: a server without a debugger answers nothing
    // here, and is then taken to be running.
    auto debugger = beebium::DebuggerControl::NewStub(channel);
    beebium::ExecutionState state;
    {
        grpc::ClientContext context;
        set_deadline(context, timeout);
        if (debugger->GetState(&context, beebium::Empty{}, &state).ok()) {
            result.machine_running = state.is_running();
        }
    }
    result.reached = true;
    return result;
}

std::string describe_protocol_mismatch(const HandshakeResult& result,
                                       std::string_view client_protocol_fingerprint) {
    std::string text;
    if (result.protocol == ProtocolVerdict::server_reports_none) {
        text = "The server does not report a protocol fingerprint, so it is older than "
               "this client and cannot be used with it.";
    } else {
        text = "The server speaks a different protocol from this client. Use a server and "
               "client from the same release.";
    }
    text += "\nServer: ";
    text += result.server_executable_path.empty() ? std::string("(path not reported)")
                                                  : result.server_executable_path;
    text += "\nServer protocol: ";
    text += result.server_protocol_fingerprint.empty() ? std::string("(none)")
                                                       : result.server_protocol_fingerprint;
    text += "\nClient protocol: ";
    text += client_protocol_fingerprint;
    return text;
}

}  // namespace beebium::linux_client
