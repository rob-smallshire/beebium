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

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <csignal>
#include <atomic>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int /*signal*/) {
    g_running = false;
}

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " --mos <filepath> [options]\n"
              << "\n"
              << "Required:\n"
              << "  --mos <filepath>    MOS ROM filepath\n"
              << "\n"
              << "Optional:\n"
              << "  --basic <filepath>  BASIC ROM filepath\n"
              << "  --port <port>       gRPC port (default: 50051)\n"
              << "  --help              Show this help message\n";
}

std::vector<uint8_t> load_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Cannot read file: " + filepath);
    }

    return data;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::string mos_filepath;
    std::string basic_filepath;
    uint16_t port = 50051;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--mos" && i + 1 < argc) {
            mos_filepath = argv[++i];
        } else if (arg == "--basic" && i + 1 < argc) {
            basic_filepath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (mos_filepath.empty()) {
        std::cerr << "Error: --mos is required\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // Set up signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Load ROMs
        std::cout << "Loading MOS ROM: " << mos_filepath << "\n";
        auto mos_data = load_file(mos_filepath);
        if (mos_data.size() != 16384) {
            std::cerr << "Warning: MOS ROM is " << mos_data.size()
                      << " bytes, expected 16384\n";
        }

        std::vector<uint8_t> basic_data;
        if (!basic_filepath.empty()) {
            std::cout << "Loading BASIC ROM: " << basic_filepath << "\n";
            basic_data = load_file(basic_filepath);
        }

        // Create and initialize machine
        std::cout << "Initializing BBC Model B...\n";
        beebium::ModelB machine;

        // Load MOS ROM
        std::copy(mos_data.begin(), mos_data.end(),
                  machine.state().memory.mos_rom.data());

        // Load BASIC ROM if provided
        if (!basic_data.empty()) {
            std::copy(basic_data.begin(), basic_data.end(),
                      machine.state().memory.basic_rom.data());
        }

        // Enable video output
        machine.state().memory.enable_video_output();

        // Reset machine
        machine.reset();

        // Start gRPC server
        std::cout << "Starting gRPC server on port " << port << "...\n";
        beebium::service::Server server(machine, "0.0.0.0", port);
        server.start();

        std::cout << "beebium-server running. Press Ctrl+C to stop.\n";

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
