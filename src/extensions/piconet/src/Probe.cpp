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

#include "beebium/econet/piconet/Probe.hpp"

#include "beebium/econet/piconet/Commands.hpp"
#include "beebium/econet/piconet/Constants.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace beebium::piconet {

std::optional<StatusEvent> probe_status(SerialPort& serial,
                                        std::chrono::milliseconds timeout) {
    if (!serial.is_open()) {
        return std::nullopt;
    }

    // Ask the device to identify itself. A short write failure means the
    // port is not a usable command channel -- treat as a negative result.
    const std::string command = format_status();
    auto command_bytes = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(command.data()), command.size()};
    auto wr = serial.write(command_bytes);
    if (wr.error || wr.bytes != command.size()) {
        return std::nullopt;
    }

    // Accumulate bytes and split on the event terminator ('\n'), tolerating
    // a trailing '\r' before it -- the same framing PiconetBackend's reader
    // loop uses. read() has its own short internal timeout and returns
    // would_block when idle, so we drive the deadline ourselves.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string line_buffer;
    std::array<std::uint8_t, 256> buf{};

    while (std::chrono::steady_clock::now() < deadline) {
        auto rr = serial.read({buf.data(), buf.size()});
        if (rr.error) {
            return std::nullopt;
        }
        if (rr.would_block) {
            continue;
        }
        line_buffer.append(reinterpret_cast<const char*>(buf.data()), rr.bytes);

        while (true) {
            auto newline = line_buffer.find(EVENT_TERMINATOR);
            if (newline == std::string::npos) {
                break;
            }
            std::string_view line(line_buffer.data(), newline);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            auto event = parse_event_line(line);
            line_buffer.erase(0, newline + 1);
            if (auto* status = std::get_if<StatusEvent>(&event)) {
                return *status;
            }
            // Any other event (RX_*/MONITOR/ERROR/unknown) from a Piconet
            // left listening is skipped -- keep waiting for the STATUS line
            // until the deadline.
        }
    }
    return std::nullopt;
}

}  // namespace beebium::piconet
