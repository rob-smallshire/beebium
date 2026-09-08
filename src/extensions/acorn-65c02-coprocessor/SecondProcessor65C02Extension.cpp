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

#include "SecondProcessor65C02Extension.hpp"

#include "beebium/extension/ExtensionContext.hpp"

#include <filesystem>
#include <iostream>
#include <span>

namespace beebium {

void SecondProcessor65C02Extension::init(ExtensionContext& ctx)
{
    // Obtain the Tube connector from the host.
    tube_socket_ = &ctx.get<TubeSocket>();

    // Load the Tube client ROM.
    std::array<uint8_t, 2048> rom{};
    if (!load_client_rom(rom)) {
        throw std::runtime_error(
            "SecondProcessor65C02Extension: failed to load Tube client ROM");
    }

    // Create components. The clock ratio (3/2 for the 65C02, 2/1 for the
    // 65C102) lives with the runner as its CoprocessorClock, not with the socket.
    tube_ula_ = std::make_unique<TubeUla>();
    runner_ = std::make_unique<CoprocessorRunner>(*tube_ula_, rom, clock_ratio_);
    runner_->reset();

    // Install the TubeUla as the host-side backend.
    tube_socket_->install_backend(tube_ula_.get());

    // Install the runner as the coprocessor, driven in host time from
    // Machine::step().
    tube_socket_->install_coprocessor(runner_.get());

    // The debugger is the server's concern: it reads debug_target() (the
    // runner, a CpuDebugTarget), instantiates DebuggerControlServiceImpl
    // against the abstract interface and registers the CoprocessorDebuggerControl
    // service. The extension hosts no gRPC service itself.

    std::cout << "  " << cpu_label_ << " coprocessor (single-threaded)\n";
}

void SecondProcessor65C02Extension::shutdown()
{
    if (!runner_)
        return;

    // Remove the coprocessor from host ticking.
    if (tube_socket_) {
        tube_socket_->remove_coprocessor();
        tube_socket_->install_backend(nullptr);
    }

    runner_.reset();
    tube_ula_.reset();
}

bool SecondProcessor65C02Extension::load_client_rom(std::array<uint8_t, 2048>& rom) const
{
    // The firmware belongs to the plugin: it is declared in the manifest and
    // ships in the plugin's own roms/ directory, resolved by the extension API
    // against the manifest directory. No server header, no shared roms/ lookup.
    // An explicit `rom` config value overrides with a user-supplied client ROM.
    // Both paths accept the mapped 2 KB image or a 4 KB EPROM dump with a blank
    // lower half (see read_rom_image / Extension::load_rom).
    try {
        auto rom_config = config_value("rom");
        if (rom_config) {
            read_rom_image(std::filesystem::path(*rom_config),
                           std::span<std::uint8_t>(rom));
        } else {
            load_rom("client", std::span<std::uint8_t>(rom));
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading Tube client ROM: " << e.what() << "\n";
        return false;
    }
}

}  // namespace beebium
