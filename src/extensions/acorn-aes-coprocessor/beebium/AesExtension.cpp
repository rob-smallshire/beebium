// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// AesExtension.cpp, the plugin as Beebium sees it: settings, start and stop
// version 2.0.0, 20 November 2026.

// AesExtension (AesExtension.hpp), the plugin as Beebium sees it: reads the settings, says why a
// ROM was refused, and starts and stops the AES with the machine.

#include "AesExtension.hpp"

#include <beebium/extension/ExtensionContext.hpp>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

// Says what is wrong with the ROM path, once AesMemory has refused it, so that the user sees why
// the machine did not start. Beebium takes any text as a file path (its ExtensionArgParser.cpp),
// so a missing file is the plugin's to report. A folder and an unreadable file get one message,
// not told apart.
std::string rom_refusal(const std::string& path) {
    std::error_code error;
    if (std::filesystem::status(path, error).type() == std::filesystem::file_type::not_found) {
        return "AES: no file at " + path;
    }
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == AesMemory::kRomBytes) return "AES: could not read " + path + " as a file";
    return "AES: " + path + " is " + std::to_string(size) + " bytes; a ROM must be exactly 16,384";
}

}  // namespace

// The switches, from the plugin's settings: each one Beebium was given is set by name. A value the
// table refuses stops the start with a message naming it, rather than leaving the default.
AesOptions AesExtension::options_from_settings() const {
    AesOptions options;
    for (const OptionRow& row : option_table()) {
        const auto value = config_value(row.key);
        if (value && !set_option(options, row.key, std::string(*value))) {
            throw std::runtime_error("AES: " + row.key + " cannot be " + std::string(*value));
        }
    }
    return options;
}

// Starts the AES: reads the switches, makes the Tube chip, the board and the debugger's link,
// loads the ROM, and plugs the Tube chip and the board into Beebium's Tube socket.
void AesExtension::init(beebium::ExtensionContext& context) {
    socket_ = &context.get<beebium::TubeSocket>();
    const AesOptions options = options_from_settings();
    ula_ = std::make_unique<beebium::TubeUla>();
    tube_ = std::make_unique<AesTube>(options, *ula_);
    // The ROM is read from the file the rom setting names; none is built in.
    if (!tube_->load_rom()) throw std::runtime_error(rom_refusal(options.rom));
    debug_ = std::make_unique<AesDebug>(*tube_);
    socket_->install_backend(ula_.get());
    socket_->install_coprocessor(tube_.get());
    // The start-up line, so that a user can see what is running: the clock to two places,
    // 6.67 MHz (for display only), then AESulator's version (CMakeLists.txt passes it in).
    char clock[32];
    std::snprintf(clock, sizeof clock, "%.2f MHz", clock_hz() / 1e6);
    std::cout << "  " << cpu_name() << " (" << clock << ") coprocessor, AESulator " AES_VERSION "\n";
}

// Stops the AES: unplugs it from the socket, then frees the parts, the debugger's link first
// because it works through the board.
void AesExtension::shutdown() {
    if (!tube_) return;
    socket_->remove_coprocessor();
    socket_->install_backend(nullptr);
    debug_.reset();
    tube_.reset();
    ula_.reset();
}

// The clock Beebium shows for this coprocessor, a label only, derived from the board's timing
// label in AesTube.cpp.
uint64_t AesExtension::clock_hz() const {
    return beebium::nominal_clock_hz(AesTube::board_label());
}
