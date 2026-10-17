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

// The subcommands that build and run a machine: start, which serves it over
// gRPC, and capture-screenshot, which runs it headless to a PNG.

#pragma once

#include "beebium/server/MachineAssembly.hpp"
#include "beebium/server/ImageResample.hpp"
#include "beebium/server/ScreenshotCrop.hpp"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// Start subcommand - starts the emulator server (default)
template<typename MachineType>
class StartSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "start"; }
    std::string_view description() const override { return "Start the emulator server (default)"; }

    void help(const char* program_name) const override {
        print_usage<MachineType>(program_name);
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            print_usage<MachineType>(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand-specific arguments
        ServerConfig<MachineType> config;
        if (auto exit_code = parse_start_arguments<MachineType>(argc, argv, global.subcommand_argv_start, config)) {
            return *exit_code;
        }

        // Validate configuration
        if (auto error = validate_config<MachineType>(config)) {
            std::cerr << "Error: " << *error << "\n";
            return ExitCode::CONFIG;
        }

        // Apply provenance defaults (protocol specification)
        if (config.launch.provenance_type.empty()) {
            config.launch.provenance_type = platform::is_stdin_tty() ? "terminal" : "unknown";
        }
        if (config.launch.provenance_uuid.empty()) {
            config.launch.provenance_uuid = generate_uuid_v4();
        } else if (!is_valid_uuid(config.launch.provenance_uuid)) {
            std::cerr << "Error: --provenance-uuid must be a valid UUID\n";
            return ExitCode::USAGE;
        }
        auto timestamp = std::chrono::system_clock::now();
        int shutdown_exit_code = ExitCode::OK;

        try {
            // Set ROM directory if specified
            if (!config.launch.rom_dirpath.empty()) {
                RomPaths::set_rom_directory(config.launch.rom_dirpath);
            }

            // Assemble the machine. This is the single shared assembly every
            // subcommand uses, so `start` and `capture-screenshot` cannot diverge
            // in how the machine is built (see assemble_machine).
            auto assembly = assemble_machine<MachineType>(
                config, /*with_audio=*/true, /*defer_auto_station=*/true);
            if (!assembly.assembled) {
                return assembly.exit_code;
            }
            auto& am = *assembly.assembled;
            auto& machine = am.machine;
            auto& extension_registry = am.extension_registry;
            auto& transport_registry = am.transport_registry;
            beebium::CoprocessorExtension* coprocessor_ext = am.coprocessor;

            // Signal handling needs the machine in hand so a machine paused
            // before the emulation loop starts (--wait=api) can still be told to
            // stop. Installed after assembly and torn down while the machine
            // (owned by `assembly`, declared above the services below) is alive.
            ScopedShutdownHandler<MachineType> shutdown_handler(machine);

            // Create core discovery services for frontends to enumerate
            // extensions. PeripheralExtensionService lists peripheral
            // extensions; EconetTransportService lists econet transport
            // extensions and identifies which is active. ExtensionUiService
            // exposes the server-driven UI tree of any extension that has
            // an ExtensionUi -- it reads from both registries so a single
            // SubscribeView call can target a peripheral or a transport.
            beebium::service::PeripheralExtensionServiceImpl peripheral_extension_service(
                extension_registry, effective_default);
            beebium::service::EconetTransportServiceImpl econet_transport_service(
                transport_registry);
            beebium::service::ExtensionUiServiceImpl extension_ui_service(
                transport_registry, extension_registry);

            // The single channel through which clients drive extension RPC
            // services (the gRPC-free replacement for extension-hosted gRPC
            // services). See docs/discussion/extension-rpc-channel.md.
            beebium::service::ExtensionRpcServiceImpl extension_rpc_service(
                transport_registry, extension_registry);

            // Collect the discovery + channel services. Extension-provided
            // APIs (AunService, PiconetService, the serial/SCSI/RTC services,
            // ...) are NOT hosted as gRPC services by the extensions; clients
            // reach them through the single extension_rpc_service channel.
            std::vector<grpc::Service*> extension_services;
            extension_services.push_back(&peripheral_extension_service);
            extension_services.push_back(&econet_transport_service);
            extension_services.push_back(&extension_ui_service);
            extension_services.push_back(&extension_rpc_service);

            // The coprocessor debugger is the one genuine core gRPC service
            // contributed by an extension: the same DebuggerControl proto as the
            // host debugger, consumed by the typed debugger clients. The server,
            // not the extension, instantiates the debugger against the abstract
            // CpuDebugTarget and wraps it in the adapter, so no extension hosts a
            // gRPC service. The target describes its own CPU, so any family is
            // served through the one service. Both must outlive the server, hence
            // these enclosing-scope owners. (assemble_machine already found the
            // coprocessor and set its execution quiescer; here the server only
            // builds the gRPC service over it.)
            std::unique_ptr<beebium::service::DebuggerControlServiceImpl>
                coprocessor_debugger_impl;
            std::unique_ptr<beebium::CoprocessorDebuggerAdapter> coprocessor_debugger_adapter;
            if (coprocessor_ext) {
                if (auto* target = coprocessor_ext->debug_target()) {
                    coprocessor_debugger_impl =
                        std::make_unique<beebium::service::DebuggerControlServiceImpl>(*target);
                    coprocessor_debugger_adapter =
                        std::make_unique<beebium::CoprocessorDebuggerAdapter>(*coprocessor_debugger_impl);
                    extension_services.push_back(coprocessor_debugger_adapter.get());
                }
            }

            // Start gRPC server
            std::cout << "Starting gRPC server...\n";
            beebium::service::Server<MachineType> server(machine, "0.0.0.0", config.launch.port);

            // Push the configured motherboard link state into the server
            // so that the sideways service (constructed during start())
            // reports the actual topology to clients -- e.g. Model B+
            // with --motherboard-link s13=north shows IC71 at slots 0/1.
            server.set_motherboard_links(config.board.motherboard_links);

            beebium::service::Provenance provenance{
                config.launch.provenance_type,
                config.launch.provenance_uuid,
                config.launch.provenance_version,
                timestamp
            };

            // Apply identity defaults (the uuid is the one assemble_machine
            // resolved)
            if (config.machine.machine_name.empty()) {
                config.machine.machine_name = std::string(Memory::MACHINE_DISPLAY_NAME);
            }

            // The name is a template (#153); the server renders it, and
            // re-renders it as its placeholders' values change.
            beebium::service::MachineIdentity identity{
                config.launch.machine_uuid,
                config.machine.machine_name,
                std::string(Memory::MACHINE_TYPE),
                std::string(Memory::MACHINE_DISPLAY_NAME),
                config.machine.machine_name
            };

            // Print provenance details for debugging (before move)
            auto timestamp_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                provenance.timestamp.time_since_epoch()).count();
            std::cout << "Provenance: type='" << provenance.type
                      << "', uuid=" << provenance.instance_uuid
                      << ", version=" << (provenance.version.empty() ? "(none)" : "'" + provenance.version + "'")
                      << ", timestamp=" << timestamp_seconds
                      << std::endl;

            // Print identity details
            std::cout << "Identity: uuid=" << identity.uuid
                      << ", name_template='" << identity.name_template
                      << "', model_type='" << identity.model_type
                      << "', model_name='" << identity.model_name
                      << "'" << std::endl;

            // Create shutdown callback that stops the emulation loop when RPC shutdown is requested
            beebium::service::ShutdownPolicyConfig shutdown_policy_config{config.launch.allow_shutdown};
            auto shutdown_callback = [&machine]() {
                g_running = false;
                machine.request_shutdown();
                // Also interrupt pacing clock if it's waiting (may be nullptr during startup)
                if (g_request_pacing_stop) {
                    g_request_pacing_stop();
                }
            };
            // Publish the configured speed (from --speed) before the server
            // accepts connections, so GetPacingStats/SetSpeedMultiplier work
            // immediately -- the pacing clock itself is wired only once the
            // emulation loop starts, below.
            server.set_initial_speed_multiplier(
                config.machine.speed_multiplier_override.value_or(1.0));

            // Placeholders the loaded extensions offer for the machine's name
            // template. A plugin that breaks the registration rules loses its
            // placeholders, not the machine.
            for (auto* ext : extension_registry.extensions()) {
                register_extension_name_placeholders(server, *ext);
            }
            for (const auto& transport : transport_registry.extensions()) {
                register_extension_name_placeholders(server, *transport);
            }
            server.set_launch_preset_name(config.launch.preset_name);
            server.set_launch_coprocessor(am.coprocessor);
            if (config.launch.machine_ordinal) {
                server.set_launch_ordinal(*config.launch.machine_ordinal);
            }

            server.start(std::move(provenance), std::move(identity),
                        config.launch.advertise, shutdown_policy_config, std::move(shutdown_callback),
                        extension_services, &am.transport_registry);

            // Print actual bound port (important when port 0 was requested for dynamic allocation)
            // Flush immediately so clients parsing stdout can detect the port before we block
            std::cout << "Listening on port " << server.port() << std::endl;
            std::cout << Memory::MACHINE_DISPLAY_NAME << " ready. Press Ctrl+C to stop." << std::endl;

            // --station auto was deferred so the port above printed without
            // waiting on the (possibly multi-second) mDNS browse, which a
            // launcher's port-wait must not time out on. Resolve it now, with
            // the gRPC server already listening -- so the status stream shows
            // Econet not-yet-enabled meanwhile -- and enable Econet before the
            // machine is told to run (handle_wait_mode / the first Run RPC
            // below waits for this to finish).
            if (am.pending_auto_station.has_value()) {
                // Enter the --wait=api paused state BEFORE selecting, so a
                // client that connects the moment the port appears and calls
                // Run() finds the machine paused; Run() then waits (via
                // begin/end_startup_selection below) for the selection to
                // finish before resuming, rather than being rejected as
                // "already running" or lost (the macOS black-screen defect).
                if (config.launch.wait_mode == WaitMode::Api) {
                    enter_api_wait_state(machine);
                }
                server.debugger_service().begin_startup_selection();
                int resolved_station = config.machine.station_number;
                bool auto_assigned = false;
                auto exit_code = resolve_auto_station(
                    config, transport_registry, /*offline=*/false,
                    resolved_station, auto_assigned);
                if (!exit_code) {
                    exit_code = install_econet(machine, config, transport_registry,
                                               resolved_station, auto_assigned);
                }
                // Always release a waiting Run(), even on error, so the RPC
                // never hangs on a selection that failed.
                server.debugger_service().end_startup_selection();
                if (exit_code) {
                    return *exit_code;
                }
            }

            // Wire cross-processor debugger stop entirely server-side. Stop
            // detection lives in the two DebuggerControlServiceImpl instances
            // the server owns (the host's and the coprocessor's), so each one's
            // counterpart callback pauses the other processor through the
            // abstract interface.
            if (coprocessor_ext) {
                if (auto* cop = coprocessor_ext->coprocessor()) {
                    // Host breakpoint with stop_counterpart -> pause coprocessor.
                    server.debugger_service().set_counterpart_stop_callback(
                        [cop] { cop->pause(); });
                }
                // Coprocessor breakpoint with stop_counterpart -> pause host.
                // Only when a debugger was created for its family; without one
                // there is no impl to detect the breakpoint.
                if (coprocessor_debugger_impl) {
                    coprocessor_debugger_impl->set_counterpart_stop_callback(
                        [&machine] { machine.pause(); });
                }
            }

            // Handle wait mode. A shutdown signal during the wait means the
            // emulation loop is never entered: the machine was asked to stop
            // before it was ever asked to run. A deferred --station auto launch
            // already entered the Api wait state above (before selecting), so
            // don't enter it again here.
            WaitMode effective_wait_mode = config.launch.wait_mode;
            if (am.pending_auto_station.has_value() &&
                config.launch.wait_mode == WaitMode::Api) {
                effective_wait_mode = WaitMode::None;
            }
            if (handle_wait_mode(machine, effective_wait_mode)) {
                // Run main emulation loop (blocks until shutdown)
                run_emulation_loop(machine, server, config);
            }

            std::cout << "\nShutting down...\n";

            // Emulation has stopped (the loop ran on this thread), so this
            // cannot race the in-emulation flush. Persist any remaining writes.
            flush_disc_drives(machine);

            // A saved machine keeps what its battery-backed units hold, its
            // extensions' as well as its own.
            if (!am.machine_directory->transient()) {
                try {
                    save_battery_units(machine.battery_units(),
                                       am.machine_directory->dirpath() /
                                           machine_layout::NVRAM_DIRNAME);
                    for_each_extension_battery(am, [](const auto& units, const auto& dirpath) {
                        save_battery_units(units, dirpath);
                    });
                } catch (const std::exception& e) {
                    std::cerr << "Error: cannot keep the machine's battery-backed state: "
                              << e.what() << "\n";
                    shutdown_exit_code = ExitCode::IOERR;
                }
            }

            server.stop();
            extension_registry.shutdown();

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::SOFTWARE;
        }

        return shutdown_exit_code;
    }
};

// CaptureScreenshot subcommand - headless emulation to PNG
template<typename MachineType>
class CaptureScreenshotSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "capture-screenshot"; }
    std::string_view description() const override {
        return "Capture a screenshot from headless emulation";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " capture-screenshot --output <filepath> [options]\n"
                  << "\n"
                  << "Runs the emulator headlessly and captures a framebuffer screenshot as PNG.\n"
                  << "Accepts all 'start' options (--preset, --floppy, --fdc, etc.).\n"
                  << "\n"
                  << "Required:\n"
                  << "  --output <filepath>      Output PNG filepath\n"
                  << "\n"
                  << "Optional:\n"
                  << "  --duration <seconds>     Emulation duration before capture (default: from\n"
                  << "                           preset thumbnail_capture_delay_seconds, or 2.0)\n"
                  << "  --border <pixels>        Black border width around the image (default: 20)\n"
                  << "  --crop <mode>            none (default) or auto. auto crops to the\n"
                  << "                           screen content (e.g. a boot banner top-left in a\n"
                  << "                           mostly black frame) and enlarges it to the same\n"
                  << "                           image size, keeping the frame's aspect ratio and\n"
                  << "                           resampling with a filter (not pixel-replicated).\n"
                  << "\n"
                  << "All 'start' subcommand options are also accepted.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Pre-scan argv for capture-specific args (--output, --duration, --border),
        // then pass the rest to parse_start_arguments unchanged.
        std::string output_filepath;
        std::optional<double> cli_duration;
        int border_pixels = 20;
        bool crop_auto = false;  // --crop none (default) | auto

        // Accept the mode of a --crop flag in either form (`--crop auto` or
        // `--crop=auto`); returns false and reports a USAGE error for an unknown
        // value, naming the accepted ones.
        auto parse_crop_mode = [](std::string_view mode, bool& out) -> bool {
            if (mode == "none") { out = false; return true; }
            if (mode == "auto") { out = true; return true; }
            std::cerr << "Error: --crop must be 'none' or 'auto'\n";
            return false;
        };

        std::vector<char*> filtered_argv;
        for (int i = 0; i < argc; ++i) {
            if (i >= global.subcommand_argv_start) {
                std::string_view arg = argv[i];
                if (arg == "--output" && i + 1 < argc) {
                    output_filepath = argv[i + 1];
                    ++i;
                    continue;
                } else if (arg == "--duration" && i + 1 < argc) {
                    try {
                        cli_duration = std::stod(argv[i + 1]);
                    } catch (...) {
                        std::cerr << "Error: --duration must be a number\n";
                        return ExitCode::USAGE;
                    }
                    if (*cli_duration < 0) {
                        std::cerr << "Error: --duration must be non-negative\n";
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg == "--border" && i + 1 < argc) {
                    try {
                        border_pixels = parse_int(argv[i + 1], "--border");
                    } catch (const std::exception& e) {
                        std::cerr << "Error: " << e.what() << "\n";
                        return ExitCode::USAGE;
                    }
                    if (border_pixels < 0) {
                        std::cerr << "Error: --border must be non-negative\n";
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg == "--crop" && i + 1 < argc) {
                    if (!parse_crop_mode(argv[i + 1], crop_auto)) {
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg.rfind("--crop=", 0) == 0) {
                    if (!parse_crop_mode(arg.substr(7), crop_auto)) {
                        return ExitCode::USAGE;
                    }
                    continue;
                } else if (arg == "--help" || arg == "-h") {
                    help(argv[0]);
                    return ExitCode::OK;
                }
            }
            filtered_argv.push_back(argv[i]);
        }
        int filtered_argc = static_cast<int>(filtered_argv.size());

        if (output_filepath.empty()) {
            std::cerr << "Error: --output is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Parse machine configuration from remaining args
        ServerConfig<MachineType> config;
        if (auto exit_code = parse_start_arguments<MachineType>(
                filtered_argc, filtered_argv.data(),
                global.subcommand_argv_start, config)) {
            return *exit_code;
        }

        if (auto error = validate_config<MachineType>(config)) {
            std::cerr << "Error: " << *error << "\n";
            return ExitCode::CONFIG;
        }

        // Determine capture duration.
        // Priority: CLI --duration > preset thumbnail_capture_delay_seconds > 2.0
        double duration_seconds = 2.0;
        if (cli_duration) {
            duration_seconds = *cli_duration;
        } else if (config.launch.preset) {
            auto result = load_preset(*config.launch.preset);
            if (result && result.config->thumbnail_capture_delay_seconds) {
                duration_seconds = *result.config->thumbnail_capture_delay_seconds;
            }
        }

        try {
            if (!config.launch.rom_dirpath.empty()) {
                RomPaths::set_rom_directory(config.launch.rom_dirpath);
            }

            // Assemble the machine through the one shared assembly, exactly as
            // `start` does. This installs any --tube-* coprocessor and every
            // other extension the config carries -- the old screenshot path
            // built its own extension-less machine, so a Tube preset rendered
            // the host-only banner. Screenshots need video but not audio.
            auto assembly = assemble_machine<MachineType>(config, /*with_audio=*/false);
            if (!assembly.assembled) {
                return assembly.exit_code;
            }
            auto& am = *assembly.assembled;
            auto& machine = am.machine;

            // Create local framebuffer and renderer (no gRPC server needed)
            FrameBuffer frame_buffer;
            FrameRenderer renderer(&frame_buffer);
            renderer.set_field_cycles(&machine.video_binding().renderer.field_cycles());
            auto& video_output = *machine.state().memory.video_output;

            // Helper to drain the video output queue completely.
            // Each machine.run() step produces ~20,000 PixelBatches (one per 2 CPU
            // cycles), so we must drain the entire queue after each step rather than
            // relying on the default max_units of 1000.
            auto drain_video = [&]() {
                while (renderer.process(video_output, 100000) > 0) {}
            };

            constexpr uint64_t cycles_per_step = 40000;

            // Advance the machine one chunk through the SAME stepper the serving
            // loop uses -- disc-drive ticks, Econet speed gate and run() -- so
            // the screenshot capture cannot diverge from a running server. No
            // pacing (a screenshot renders as fast as it can), never parked, so
            // the on-parked hook is a no-op and the budget is a fixed chunk.
            auto step_chunk = [&] {
                step_emulation(machine, /*speed_multiplier=*/1.0,
                               /*on_parked=*/[] {},
                               [&]() -> uint64_t { return cycles_per_step; });
            };

            if (duration_seconds == 0.0) {
                // Duration 0: capture first complete frame
                uint64_t initial_version = frame_buffer.version();
                while (frame_buffer.version() == initial_version) {
                    step_chunk();
                    drain_video();
                }
            } else {
                uint64_t total_cycles = static_cast<uint64_t>(
                    duration_seconds * timing::CYCLES_PER_SECOND);
                uint64_t cycles_run = 0;
                while (cycles_run < total_cycles) {
                    step_chunk();
                    drain_video();
                    cycles_run += cycles_per_step;
                }
            }

            // The capture is complete; shut the extensions down cleanly (e.g.
            // remove a coprocessor from the Tube socket) while the machine is
            // still alive, as `start` does after its serving loop.
            am.extension_registry.shutdown();

            if (frame_buffer.version() == 0) {
                std::cerr << "Error: no frames produced during emulation\n";
                return ExitCode::SOFTWARE;
            }

            // Extract frame data: the metadata and pixels of one frame
            FrameMetadata metadata;
            std::vector<uint32_t> frame_copy(frame_buffer.capacity_pixels());
            frame_buffer.read_frame(metadata, frame_copy);
            uint32_t frame_width = metadata.width;
            uint32_t frame_height = metadata.height;
            size_t stride_pixels = frame_buffer.stride_pixels();

            // Scale from logical frame to display dimensions (nearest-neighbour)
            // with black border around the image
            uint32_t display_width = metadata.display_width;
            uint32_t display_height = metadata.display_height;
            uint32_t border = static_cast<uint32_t>(border_pixels);
            uint32_t output_width = display_width + 2 * border;
            uint32_t output_height = display_height + 2 * border;

            // Zero-initialised = black with alpha 0, then we set alpha for all pixels
            std::vector<uint8_t> rgba(output_width * output_height * 4, 0);

            // Set alpha to 0xFF for all pixels (border stays black RGB)
            for (size_t i = 3; i < rgba.size(); i += 4) {
                rgba[i] = 0xFF;
            }

            // Write one display-sized content pixel (BGRA32) into the bordered
            // region.
            auto put_pixel = [&](uint32_t x, uint32_t y, uint32_t bgra) {
                size_t dst_idx = ((y + border) * output_width + (x + border)) * 4;
                rgba[dst_idx + 0] = static_cast<uint8_t>((bgra >> 16) & 0xFF);  // R
                rgba[dst_idx + 1] = static_cast<uint8_t>((bgra >> 8) & 0xFF);   // G
                rgba[dst_idx + 2] = static_cast<uint8_t>(bgra & 0xFF);           // B
                rgba[dst_idx + 3] = static_cast<uint8_t>((bgra >> 24) & 0xFF);   // A
            };

            if (crop_auto) {
                // Find the content and resample that logical sub-rectangle up to
                // the SAME display size and border a full thumbnail has, with a
                // filtered (Lanczos-3) per-axis scale -- so the banner is enlarged
                // legibly, not pixel-replicated, and a thumbnail set stays uniform
                // in aspect and pixel size. See ScreenshotCrop.hpp / ImageResample.hpp.
                CropRect src_rect = compute_auto_crop(frame_copy.data(), frame_width,
                                                      frame_height, stride_pixels);
                std::vector<uint32_t> content = resample_bgra_lanczos3(
                    frame_copy.data(), stride_pixels, src_rect.x, src_rect.y,
                    src_rect.width, src_rect.height, display_width, display_height);
                for (uint32_t y = 0; y < display_height; ++y) {
                    for (uint32_t x = 0; x < display_width; ++x) {
                        put_pixel(x, y, content[static_cast<size_t>(y) * display_width + x]);
                    }
                }
            } else {
                // --crop none: the whole logical frame scaled nearest-neighbour to
                // the display -- byte-for-byte the original behaviour.
                for (uint32_t y = 0; y < display_height; ++y) {
                    uint32_t src_y = y * frame_height / display_height;
                    if (src_y >= frame_height) src_y = frame_height - 1;
                    for (uint32_t x = 0; x < display_width; ++x) {
                        uint32_t src_x = x * frame_width / display_width;
                        if (src_x >= frame_width) src_x = frame_width - 1;
                        put_pixel(x, y, frame_copy[src_y * stride_pixels + src_x]);
                    }
                }
            }

            int write_result = stbi_write_png(
                output_filepath.c_str(),
                static_cast<int>(output_width),
                static_cast<int>(output_height),
                4,
                rgba.data(),
                static_cast<int>(output_width * 4)
            );

            if (!write_result) {
                std::cerr << "Error: failed to write PNG: " << output_filepath << "\n";
                return ExitCode::IOERR;
            }

            std::cout << output_filepath << "\n";
            return ExitCode::OK;

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::SOFTWARE;
        }
    }
};

} // namespace beebium::server
