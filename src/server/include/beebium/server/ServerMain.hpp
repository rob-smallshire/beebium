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

#ifndef BEEBIUM_SERVER_SERVER_MAIN_HPP
#define BEEBIUM_SERVER_SERVER_MAIN_HPP

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/server/RomPaths.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <csignal>
#include <atomic>

namespace beebium::server {

namespace {

constexpr uint16_t DEFAULT_GRPC_PORT = 0xBEEB;  // 48875

std::atomic<bool> g_running{true};

void signal_handler(int /*signal*/) {
    g_running = false;
}

std::vector<uint8_t> load_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath.string());
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Cannot read file: " + filepath.string());
    }

    return data;
}

// Parse "slot:filepath" format, returns (slot, filepath)
std::pair<uint8_t, std::string> parse_rom_arg(const std::string& arg) {
    auto colon_pos = arg.find(':');
    if (colon_pos == std::string::npos) {
        throw std::runtime_error("Invalid --rom format: " + arg + " (expected slot:filepath)");
    }
    std::string slot_str = arg.substr(0, colon_pos);
    std::string filepath = arg.substr(colon_pos + 1);

    int slot = std::stoi(slot_str);
    if (slot < 0 || slot > 15) {
        throw std::runtime_error("Invalid slot number: " + slot_str + " (must be 0-15)");
    }

    return {static_cast<uint8_t>(slot), filepath};
}

} // anonymous namespace

template<typename MachineType>
void print_usage(const char* program_name) {
    using Memory = typename MachineType::Memory;
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Machine: " << Memory::MACHINE_DISPLAY_NAME << "\n"
              << "\n"
              << "Optional:\n"
              << "  --mos <filepath>         MOS ROM filepath (default: " << Memory::DEFAULT_MOS_ROM << ")\n"
              << "  --rom <slot>:<filepath>  Load ROM into sideways slot (0-15)\n"
              << "  --rom-dir <dirpath>      ROM directory (auto-detected if not specified)\n"
              << "  --port <port>            gRPC port (default: " << DEFAULT_GRPC_PORT << ")\n"
              << "  --info                   Show machine information and exit\n"
              << "  --help                   Show this help message\n"
              << "\n"
              << "Default sideways ROMs:\n"
              << "  Slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << ": "
              << Memory::DEFAULT_LANGUAGE_ROM << " (language ROM)\n"
              << "\n"
              << "Examples:\n"
              << "  " << program_name << "                           # Use all defaults\n"
              << "  " << program_name << " --rom 15:forth.rom        # Replace BASIC with Forth\n"
              << "  " << program_name << " --rom 14:dfs.rom          # Add DFS in slot 14\n";
}

template<typename MachineType>
void print_info(const char* program_name) {
    using Memory = typename MachineType::Memory;

    // JSON output for machine discovery
    std::cout << "{\n"
              << "  \"executable\": \"" << program_name << "\",\n"
              << "  \"machine_type\": \"" << Memory::MACHINE_TYPE << "\",\n"
              << "  \"display_name\": \"" << Memory::MACHINE_DISPLAY_NAME << "\",\n"
              << "  \"version\": \"" << BEEBIUM_VERSION << "\",\n"
              << "  \"default_mos_rom\": \"" << Memory::DEFAULT_MOS_ROM << "\",\n"
              << "  \"default_language_rom\": \"" << Memory::DEFAULT_LANGUAGE_ROM << "\",\n"
              << "  \"default_language_slot\": " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << "\n"
              << "}\n";
}

template<typename MachineType>
int server_main(int argc, char* argv[]) {
    using Memory = typename MachineType::Memory;

    std::string mos_filepath;
    std::string rom_dirpath;
    std::map<uint8_t, std::string> rom_slots;  // slot -> filepath
    uint16_t port = DEFAULT_GRPC_PORT;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage<MachineType>(argv[0]);
            return 0;
        } else if (arg == "--info") {
            print_info<MachineType>(argv[0]);
            return 0;
        } else if (arg == "--mos" && i + 1 < argc) {
            mos_filepath = argv[++i];
        } else if (arg == "--rom" && i + 1 < argc) {
            auto [slot, filepath] = parse_rom_arg(argv[++i]);
            rom_slots[slot] = filepath;
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            rom_dirpath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage<MachineType>(argv[0]);
            return 1;
        }
    }

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Set ROM directory if specified
        if (!rom_dirpath.empty()) {
            RomPaths::set_rom_directory(rom_dirpath);
        }

        // Use default MOS ROM if not specified
        if (mos_filepath.empty()) {
            mos_filepath = std::string(Memory::DEFAULT_MOS_ROM);
        }

        // Load default language ROM into default slot unless overridden
        if (rom_slots.find(Memory::DEFAULT_LANGUAGE_SLOT) == rom_slots.end()) {
            rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] = std::string(Memory::DEFAULT_LANGUAGE_ROM);
        }

        // Load MOS ROM
        auto mos_path = RomPaths::find_rom(mos_filepath);
        std::cout << "Loading MOS ROM: " << mos_path << "\n";
        auto mos_data = load_file(mos_path);
        if (mos_data.size() != 16384) {
            std::cerr << "Warning: MOS ROM is " << mos_data.size()
                      << " bytes, expected 16384\n";
        }

        // Create and initialize machine
        std::cout << "Initializing " << Memory::MACHINE_DISPLAY_NAME << "...\n";
        MachineType machine;

        // Load MOS ROM into machine
        std::copy(mos_data.begin(), mos_data.end(),
                  machine.state().memory.mos_rom.data());

        // Load sideways ROMs
        for (const auto& [slot, filepath] : rom_slots) {
            auto rom_path = RomPaths::find_rom(filepath);
            std::cout << "Loading ROM into slot " << static_cast<int>(slot) << ": " << rom_path << "\n";
            auto rom_data = load_file(rom_path);

            if (rom_data.size() != 16384) {
                std::cerr << "Warning: ROM is " << rom_data.size()
                          << " bytes, expected 16384\n";
            }

            // Load into appropriate slot
            // Currently supported slots: 0 (basic_rom), 1 (dfs_rom), 4 (sideways_ram)
            // Slot 15 maps to slot 0 (basic_rom) for language ROM compatibility
            uint8_t physical_slot = slot;
            if (slot == 15) {
                physical_slot = 0;  // Language ROM goes into basic_rom slot
            }

            switch (physical_slot) {
                case 0:
                    std::copy(rom_data.begin(), rom_data.end(),
                              machine.state().memory.basic_rom.data());
                    break;
                case 1:
                    std::copy(rom_data.begin(), rom_data.end(),
                              machine.state().memory.dfs_rom.data());
                    break;
                case 4:
                    std::copy(rom_data.begin(), rom_data.end(),
                              machine.state().memory.sideways_ram.data());
                    break;
                default:
                    std::cerr << "Warning: Slot " << static_cast<int>(slot)
                              << " is not currently populated in hardware, ROM ignored\n";
                    break;
            }
        }

        // Enable video output
        machine.state().memory.enable_video_output();

        // Reset machine
        machine.reset();

        // Start gRPC server
        std::cout << "Starting gRPC server on port " << port << "...\n";
        beebium::service::Server<MachineType> server(machine, "0.0.0.0", port);
        server.start();

        std::cout << Memory::MACHINE_DISPLAY_NAME << " running. Press Ctrl+C to stop.\n";

        // Main emulation loop
        constexpr uint64_t cycles_per_frame = 40000;  // ~50Hz at 2MHz
        while (g_running) {
            // Block if debugger has paused execution
            machine.wait_if_paused();

            machine.run(cycles_per_frame);

            // Process any frame rendering
            // (FrameRenderer will be called by VideoService when needed)
        }

        std::cout << "\nShutting down...\n";
        server.stop();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SERVER_MAIN_HPP
