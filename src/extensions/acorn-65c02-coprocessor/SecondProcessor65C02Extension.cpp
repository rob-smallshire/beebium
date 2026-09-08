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
#include "beebium/server/RomPaths.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace beebium {

void SecondProcessor65C02Extension::init(ExtensionContext& ctx)
{
    // Obtain the Tube connector from the host.
    tube_socket_ = &ctx.get<TubeSocket>();

    // Load the Tube client ROM.
    std::array<uint8_t, 2048> rom{};
    if (!load_rom(rom)) {
        throw std::runtime_error(
            "SecondProcessor65C02Extension: failed to load Tube client ROM");
    }

    // Create components. The clock ratio (3 MHz parasite / 2 MHz host) lives
    // with the runner as its CoprocessorClock, not with the socket.
    tube_ula_ = std::make_unique<TubeUla>();
    runner_ = std::make_unique<ParasiteRunner>(*tube_ula_, rom, ClockRatio{3, 2});
    runner_->reset();

    // Install the TubeUla as the host-side backend.
    tube_socket_->install_backend(tube_ula_.get());

    // Install the runner as the coprocessor, driven in host time from
    // Machine::step().
    tube_socket_->install_coprocessor(runner_.get());

    // The debugger is the server's concern: it reads debug_target() (the
    // runner, a Cpu6502DebugTarget), instantiates DebuggerControlServiceImpl
    // against the abstract interface and registers the ParasiteDebuggerControl
    // service. The extension hosts no gRPC service itself.

    std::cout << "  65C02 coprocessor (3 MHz, single-threaded)\n";
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

bool SecondProcessor65C02Extension::load_rom(std::array<uint8_t, 2048>& rom) const
{
    static constexpr const char* ROM_FILENAME = "acorn-tube-6502_1_10.rom";

    // Check explicit config first.
    auto rom_config = config_value("rom");
    if (rom_config) {
        std::ifstream file(std::filesystem::path(*rom_config), std::ios::binary);
        if (!file.good()) {
            std::cerr << "Error: cannot open ROM file: " << *rom_config << "\n";
            return false;
        }
        file.read(reinterpret_cast<char*>(rom.data()), 2048);
        return file.gcount() == 2048;
    }

    // Otherwise resolve by name through server::RomPaths::find_rom(), whose
    // search order does NOT depend on where this extension lives (built-in or
    // plugin): (1) an explicit rom directory set on RomPaths, (2) $BEEBIUM_ROM_DIR,
    // (3) a roms/ directory at or above the server executable, (4) the installed
    // ../share/beebium/roms/, (5) the compile-time BEEBIUM_DEFAULT_ROM_DIR. The
    // Tube client ROM reaches those locations independently of this plugin: the
    // server copies it into the build roms/ directory and installs the whole rom
    // set into share/beebium/roms, and this extension's CMakeLists also copies it
    // into the build roms/ directory. So the lookup succeeds whether the 65C02
    // runs as a built-in or is loaded as a plugin from extensions/.
    auto rom_filepath = server::RomPaths::find_rom(ROM_FILENAME);
    std::ifstream file(rom_filepath, std::ios::binary);
    if (!file.good()) {
        std::cerr << "Error: Tube client ROM not found: " << ROM_FILENAME << "\n";
        return false;
    }
    file.read(reinterpret_cast<char*>(rom.data()), 2048);
    return file.gcount() == 2048;
}

}  // namespace beebium
