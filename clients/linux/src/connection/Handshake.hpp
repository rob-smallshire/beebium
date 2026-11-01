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

#ifndef BEEBIUM_LINUX_CONNECTION_HANDSHAKE_HPP
#define BEEBIUM_LINUX_CONNECTION_HANDSHAKE_HPP

#include "connection/Compatibility.hpp"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace beebium::linux_client {

// What a client learns on connecting, before it opens any stream.
struct HandshakeResult {
    bool reached = false;          // both calls answered
    grpc::StatusCode code = grpc::StatusCode::OK;
    std::string error;             // when not reached

    ProtocolVerdict protocol = ProtocolVerdict::server_reports_none;
    std::string server_protocol_fingerprint;
    std::string server_executable_path;
    bool server_is_local = false;  // client specification 3.2

    // The machine, as SystemInfo names it (client specification 5.7), and the
    // clients watching it (4.5).
    std::string machine_uuid;
    std::string machine_name;
    std::string machine_description;
    std::string model_name;
    std::int32_t client_count = 0;

    std::uint32_t video_width = 0;
    std::uint32_t video_height = 0;
    std::uint32_t video_framerate_hz = 0;
};

// SystemService.GetSystemInfo, compared against our protocol and host
// fingerprints (client specification 4.2, 3.2), then VideoService.GetConfig
// once (6.1). Blocking: call it off the GUI thread. Each call is bounded by
// `timeout`, so an unreachable host cannot hang the caller.
HandshakeResult perform_handshake(const std::shared_ptr<grpc::Channel>& channel,
                                  std::string_view client_protocol_fingerprint,
                                  std::string_view client_host_fingerprint,
                                  std::chrono::milliseconds timeout);

// The message a user is shown for a protocol verdict other than compatible,
// naming both fingerprints and the server binary (client specification 4.2).
std::string describe_protocol_mismatch(const HandshakeResult& result,
                                       std::string_view client_protocol_fingerprint);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_HANDSHAKE_HPP
