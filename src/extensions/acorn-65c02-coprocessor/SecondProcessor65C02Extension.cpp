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

    // Load the Tube client ROM (the full 4 KB 2732 device image).
    std::array<uint8_t, 4096> rom{};
    if (!load_client_rom(rom)) {
        throw std::runtime_error(
            "SecondProcessor65C02Extension: failed to load Tube client ROM");
    }

    // Create components. The board timing (crystal-tick ratio, cycle costs and
    // DRAM refresh; issue #70) lives with the runner, not with the socket.
    tube_ula_ = std::make_unique<TubeUla>();
    runner_ = std::make_unique<CoprocessorRunner>(*tube_ula_, rom, board_timing_);
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

bool SecondProcessor65C02Extension::load_client_rom(std::array<uint8_t, 4096>& rom) const
{
    // The firmware belongs to the plugin: it is declared in the manifest and
    // ships in the plugin's own roms/ directory, resolved by the extension API
    // against the manifest directory. No server header, no shared roms/ lookup.
    // An explicit `rom` config value overrides with a user-supplied client ROM.
    // The device is a 4 KB 2732; both paths require exactly 4096 bytes -- the
    // full device image -- with no assumption about either half's contents (the
    // ReCo6502 client has code in the lower half; the Acorn dumps have &FF).
    try {
        auto rom_config = config_value("rom");
        if (rom_config) {
            // Report a wrong-size override in the device's own terms: the 2 KB
            // images other emulators ship are only the upper half of the 2732.
            const std::filesystem::path path(*rom_config);
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec) {
                throw std::runtime_error("Tube client ROM not found at " + path.string());
            }
            if (size != rom.size()) {
                throw std::runtime_error(
                    "Tube client ROM must be the full " + std::to_string(rom.size())
                    + "-byte 2732 image (F000-FFFF); got " + std::to_string(size)
                    + " bytes. The 2 kB images shipped by other emulators are the "
                      "upper half only.");
            }
            read_rom_image(path, std::span<std::uint8_t>(rom));
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
