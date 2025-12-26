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

#include "beebium/Machines.hpp"

#include <memory>
#include <string>
#include <atomic>

namespace beebium {

namespace service {

class VideoServiceImpl;
class KeyboardServiceImpl;
class DebuggerControlServiceImpl;
class Debugger6502ServiceImpl;

/// gRPC server hosting Beebium services
class Server {
public:
    /// Create server bound to the given address and port
    explicit Server(ModelB& machine, const std::string& address = "127.0.0.1",
                    uint16_t port = 50051);
    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Start the server (non-blocking)
    void start();

    /// Stop the server and wait for shutdown
    void stop();

    /// Check if server is running
    bool is_running() const;

    /// Get the address the server is bound to
    std::string address() const;

    /// Get the port the server is bound to
    uint16_t port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_SERVER_HPP
