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

// Building and running a machine from a ServerConfig: ROMs, disc controller
// and images, Econet and its transport, extensions and the coprocessor, the
// startup options, and the emulation loop. Shared by `start` and
// `capture-screenshot`, so the two cannot differ in how a machine is built.

#pragma once

#include "beebium/server/StartOptions.hpp"
#include "beebium/server/SidewaysValidation.hpp"
#include "beebium/server/CoprocessorDebuggerAdapter.hpp"
#include "beebium/server/DiscPaths.hpp"
#include "beebium/server/MachineDirectory.hpp"
#include "beebium/server/RomPaths.hpp"
#include "beebium/extension/CoprocessorExtension.hpp"
#include "beebium/extension/CpuDebugTarget.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/OneMHzBusPort.hpp"
#include "beebium/Machines.hpp"
#include "beebium/PacingClock.hpp"
#include "beebium/SleepQuantum.hpp"
#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/disc/DiscDrive.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include "beebium/disc/DiscUrl.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/tube/TubeConcepts.hpp"
#include "beebium/service/EconetTransportService.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/service/ExtensionUiService.hpp"
#include "beebium/service/PeripheralExtensionService.hpp"
#include "beebium/service/Server.hpp"

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

// Load MOS ROM and sideways ROMs into machine memory.
// Applies defaults to config where not specified.
// Throws on file I/O errors.
template<typename MachineType>
void load_roms(MachineType& machine, ServerConfig<MachineType>& config) {
    // Apply motherboard link state to memory wiring before any ROM is
    // loaded. On Model B+ this drops the IC71 binding from the slot pair
    // S13 has routed away from, so loaded BASIC only appears at the
    // currently-active pair. Other variants don't supply this hook.
    if constexpr (requires (typename MachineType::Memory& m,
                            const typename MachineType::Memory::MotherboardLinks& l) {
                      m.apply_motherboard_links(l);
                  }) {
        machine.state().memory.apply_motherboard_links(config.board.motherboard_links);
    }

    apply_rom_defaults(config);
    const auto container_dirpath = config.launch.reference_container_dirpath();

    // Load MOS ROM
    auto mos_path = RomPaths::find_rom(config.machine.mos_filepath, container_dirpath);
    std::cout << "Loading MOS ROM: " << mos_path << "\n";
    auto mos_data = load_file(mos_path);
    if (mos_data.size() != 16384) {
        std::cerr << "Warning: MOS ROM is " << mos_data.size()
                  << " bytes, expected 16384\n";
    }

    // Load MOS ROM into machine
    std::copy(mos_data.begin(), mos_data.end(),
              machine.state().memory.mos_rom.data());

    // Process all slot configurations using the unified API. The
    // validation step above (validate_sideways_configs) already rejected
    // unsupported (slot, type) pairs against the topology, so we can
    // pass each request straight through.
    for (const auto& [slot, marker_or_filepath] : config.machine.rom_slots) {
        if (marker_or_filepath == EMPTY_SLOT_MARKER) {
            std::cout << "Slot " << static_cast<int>(slot) << ": empty\n";
            machine.state().memory.configure_slot_as_empty(slot);
            continue;
        }

        if (marker_or_filepath == RAM_SLOT_MARKER) {
            std::cout << "Slot " << static_cast<int>(slot) << ": RAM\n";
            machine.state().memory.configure_slot_as_ram(slot);
            continue;
        }

        // Regular ROM loading - use unified API
        auto rom_path = RomPaths::find_rom(marker_or_filepath, container_dirpath);
        std::cout << "Loading ROM into slot " << static_cast<int>(slot) << ": " << rom_path << "\n";
        auto rom_data = load_file(rom_path);

        if (rom_data.empty() || rom_data.size() > 16384
            || !std::has_single_bit(rom_data.size())) {
            std::cerr << "Warning: ROM is " << rom_data.size()
                      << " bytes, expected a power-of-two size up to 16384\n";
        }

        // Unified API handles slot type configuration and loading together.
        // Record the resolved filepath as image_name so gRPC clients (the
        // Memory sidebar in particular) can report and act on it - Copy Path
        // and Reveal in Finder both need the absolute path.
        machine.state().memory.load_sideways_rom(
            slot, rom_data.data(), rom_data.size(), rom_path.string());
    }

    // Handle RAM slots with pre-loaded images
    // These are processed after rom_slots to ensure the slot is already configured as RAM
    for (const auto& sideways_config : config.machine.sideways_configs) {
        if (sideways_config.type == SidewaysSlotType::Ram && !sideways_config.image_filepath.empty()) {
            auto ram_path = RomPaths::find_rom(sideways_config.image_filepath, container_dirpath);
            auto ram_data = load_file(ram_path);
            std::cout << "Pre-loading RAM slot " << static_cast<int>(sideways_config.slot)
                      << " from " << ram_path << "\n";
            // Use load_sideways_data to load without changing slot type.
            // Record the resolved filepath as image_name (see load_sideways_rom).
            machine.state().memory.load_sideways_data(
                sideways_config.slot, ram_data.data(), ram_data.size(),
                ram_path.string());
        }
    }

    // Engage the RAM write-protect switch on any slot whose --sideways spec
    // asked for it, now that every slot's type is settled. validate_config
    // already ensured the machine has the control and the slots have a switch.
    if constexpr (requires(typename MachineType::Memory m) {
                      m.set_slot_write_protected(uint8_t{0}, bool{});
                  }) {
        auto& memory = machine.state().memory;
        for (const auto& sw : config.machine.sideways_configs) {
            if (!sw.write_protected) continue;
            if (memory.sideways.bank_type(sw.slot) == SlotType::Ram) {
                std::cout << "Slot " << static_cast<int>(sw.slot)
                          << ": write-protected\n";
                memory.set_slot_write_protected(sw.slot, true);
            } else {
                std::cerr << "Warning: --sideways write-protect: slot "
                          << static_cast<int>(sw.slot)
                          << " is not RAM; ignoring\n";
            }
        }
    }

    // Engage the group-addressed protection switches from --write-protect
    // <group> / --hide <group>, now that every slot's type is settled. These
    // reach the board-wide / hide switches the per-slot flag above cannot.
    if constexpr (requires(typename MachineType::Memory m, std::string_view id) {
                      m.set_protection(id, ProtectionKind::WriteProtect, bool{});
                  }) {
        auto& memory = machine.state().memory;
        for (const auto& [group_id, kind] : config.board.launch_protections) {
            if (memory.set_protection(group_id, kind, true)) {
                std::cout << (kind == ProtectionKind::Hide ? "Hide" : "Write-protect")
                          << " group '" << group_id << "': engaged\n";
            } else {
                throw std::runtime_error(
                    std::string("--")
                    + (kind == ProtectionKind::Hide ? "hide" : "write-protect")
                    + ": no protection group '" + group_id
                    + "' with that control on this machine variant");
            }
        }
    } else if (!config.board.launch_protections.empty()) {
        throw std::runtime_error(
            "--write-protect/--hide: this machine variant has no protection "
            "controls");
    }

    // The board's built-in real-time clock: time source and start time.
    if constexpr (requires { MachineType::Memory::BOARD_RTC_OPTION; }) {
        if (config.board.board_rtc) {
            auto& rtc = machine.state().memory.rtc;
            const auto& spec = *config.board.board_rtc;
            const int64_t host_now =
                Mc146818Rtc::host_local_civil_now().count() / 1'000'000;
            std::optional<int64_t> start = spec.time_civil_seconds;
            if (spec.offset) start = apply_rtc_offset(host_now, *spec.offset);
            if (spec.clock == BoardRtcClock::Emulated) {
                rtc.use_emulated_clock(start.value_or(host_now));
                std::cout << "Real-time clock: emulated time\n";
            } else if (start) {
                rtc.set_time(*start);
                std::cout << "Real-time clock: host time, shifted\n";
            }
        }
    }
}

// Install disc controller for machines with sockets.
// Returns exit code on error, nullopt on success.
template<typename MachineType>
std::optional<int> install_disc_controller(MachineType& machine, const ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    if constexpr (HasDiscControllerSocket<Memory>) {
        if (!config.machine.fdc_type.empty()) {
            if (config.machine.fdc_type == "none") {
                std::cout << "Disc controller: none (socket empty)\n";
                // Do nothing - leave socket empty
            } else if (auto controller = DiscControllerRegistry::create(config.machine.fdc_type)) {
                auto* info = DiscControllerRegistry::find(config.machine.fdc_type);
                std::cout << "Installing disc controller: " << info->display_name
                          << " (" << info->fdc_chip << ")\n";
                machine.state().memory.install_disc_controller(std::move(controller), config.machine.fdc_type);
            } else {
                std::cerr << "Error: Unknown disc controller type: " << config.machine.fdc_type << "\n"
                          << "Use the list-fdcs command to see available disc controllers.\n";
                return 1;
            }
        }
        // If --fdc not specified, socket remains empty (no disc controller)
    } else if (!config.machine.fdc_type.empty()) {
        // Model B+ has built-in controller, --fdc option is not applicable
        std::cerr << "Warning: --fdc option ignored (machine has built-in disc controller)\n";
    }

    return std::nullopt;
}

// Resolve a --station auto request to a concrete number using the transport.
// A fixed station passes through unchanged. `offline` (build-time boots:
// capture-screenshot, and any future non-`start` path that boots from a preset)
// skips the browse and announce entirely and takes the range's first number, so
// preset generation and thumbnails stay deterministic and never touch the LAN
// (the determinism rule: tests, capture and preset generation never touch
// shared or network state). Returns an exit code on error (auto with no
// transport, several transports, or a transport that cannot choose), else
// nullopt, writing the chosen number and whether it was auto-assigned. The
// `start` path calls this AFTER the gRPC server is listening and its port is
// printed, so a slow browse never delays the port line a launcher waits for.
template<typename MachineType>
std::optional<int> resolve_auto_station(
    const ServerConfig<MachineType>& config,
    beebium::EconetTransportRegistry& transport_registry, bool offline,
    int& resolved_station, bool& auto_assigned) {
    resolved_station = config.machine.station_number;
    auto_assigned = false;
    if (!config.machine.station_auto_range.has_value()) {
        return std::nullopt;  // a fixed station (or none)
    }
    if (transport_registry.empty()) {
        std::cerr << "Error: --station auto needs an Econet transport; pass --aun ...\n";
        return 1;
    }
    if (transport_registry.size() > 1) {
        std::cerr << "Error: BBC machines support at most one Econet transport (got "
                  << transport_registry.size() << ").\n";
        return 1;
    }
    auto_assigned = true;
    if (offline) {
        resolved_station = config.machine.station_auto_range->lo;  // deterministic
        return std::nullopt;
    }
    auto& transport = *transport_registry.extensions().front();
    auto outcome = transport.select_auto_station(*config.machine.station_auto_range);
    using Status = beebium::EconetTransportExtension::AutoStationOutcome::Status;
    if (outcome.status == Status::Unsupported) {
        std::cerr << "Error: " << outcome.report << "\n";
        return 1;
    }
    // Exhaustion and a still-contested fall-back carry a report; print it (the
    // design reports the range-exhausted case on stderr, not fail the launch).
    if (!outcome.report.empty()) {
        std::cerr << outcome.report << "\n";
    }
    resolved_station = outcome.station;
    return std::nullopt;
}

// Install Econet hardware for an already-resolved station number (-1 = Econet
// not fitted), on machines with an Econet socket. transport_registry holds the
// econet-transport extension instances (--aun, --piconet, or the preset's
// econet.transport); with one, its create_backend supplies the backend, and
// with none the hardware is fitted with no network. `auto_assigned` only tunes
// the log line. --station auto is resolved by resolve_auto_station beforehand,
// so this does no network I/O beyond the transport's own bring-up. Returns an
// exit code on error, nullopt on success.
template<typename MachineType>
std::optional<int> install_econet(MachineType& machine,
                                   const ServerConfig<MachineType>& config,
                                   beebium::EconetTransportRegistry& transport_registry,
                                   int resolved_station, bool auto_assigned) {
    using Memory = typename MachineType::Memory;

    if constexpr (HasEconetSocket<Memory>) {
        if (resolved_station >= 1) {
            auto station = static_cast<uint8_t>(resolved_station);
            // Appended to the "Econet station N" lines so the log shows when a
            // number was chosen automatically rather than given.
            const char* auto_note = auto_assigned ? " (auto-assigned)" : "";

            // Prefer an econet-transport extension if one was configured.
            // BBC machine policy: at most one transport. (Future Acorn
            // Econet Bridge machine type would relax this.)
            if (!transport_registry.empty()) {
                if (transport_registry.size() > 1) {
                    std::cerr << "Error: BBC machines support at most one Econet "
                                 "transport (got " << transport_registry.size() << ").\n";
                    return 1;
                }
                auto& transport = *transport_registry.extensions().front();
                auto backend = transport.create_backend(station);
                if (backend) {
                    // A transport may hand back a backend that is present but
                    // not yet connected (e.g. Piconet discovery found no
                    // device -- it still returns a disconnected backend so
                    // the UI can Retry it live). Report the state truthfully:
                    // "via transport" only when actually connected, otherwise
                    // the same "configured but unavailable -- no network"
                    // narrative the null-backend path below emits.
                    const bool connected = backend->is_connected();
                    if (connected) {
                        std::cout << "Econet station " << resolved_station
                                  << auto_note << " via transport extension '"
                                  << transport.name() << "'\n";
                    } else {
                        std::cout << "Econet station " << resolved_station
                                  << auto_note << " (transport '" << transport.name()
                                  << "' configured but unavailable -- no network)\n";
                    }
                    machine.state().memory.econet_socket.enable(
                        station, std::move(backend),
                        true,  // FourWayHandshake active
                        transport.requires_real_time_pacing());
                } else {
                    // Transport configured but unavailable (e.g. port=none,
                    // bind failed): install a disconnected TestBackend so
                    // the ADLC reports "no clock" cleanly.
                    auto stub = std::make_unique<beebium::TestBackend>();
                    stub->set_connected(false);
                    machine.state().memory.econet_socket.enable(
                        station, std::move(stub), true,
                        transport.requires_real_time_pacing());
                    std::cout << "Econet station " << resolved_station
                              << auto_note << " (transport '" << transport.name()
                              << "' configured but unavailable -- no network)\n";
                }
                return std::nullopt;
            }

            // No transport configured: Econet hardware fitted but no
            // network connection. The ADLC reports "no clock" so the
            // NFS ROM behaves as if the cable is unplugged.
            auto backend = std::make_unique<beebium::TestBackend>();
            backend->set_connected(false);
            machine.state().memory.econet_socket.enable(
                station, std::move(backend),
                true);  // aun_mode = true (FourWayHandshake wraps disconnected backend)

            std::cout << "Econet station " << resolved_station << auto_note
                      << " (no network -- pass --aun port=N or --piconet device_path=... to enable a transport)\n";
        }
    } else if (config.machine.station_number >= 1 || config.machine.station_auto_range.has_value()) {
        std::cerr << "Warning: --station option ignored (machine has no Econet socket)\n";
    }

    return std::nullopt;
}

// Load a single disc image into a floppy drive: a file URL, or a file
// reference written in `container_dirpath`, resolved as a hard disc's is
// (DiscPaths::resolve_disc_image, with `destination` for a copy).
// Returns an exit code on error, or std::nullopt on success.
inline std::optional<int> load_single_disc(DiscDrive& drive, int drive_num,
                                            const std::string& filepath_or_url,
                                            const std::filesystem::path& container_dirpath,
                                            const DiscPaths::CopyDestination& destination) {
    std::filesystem::path filepath(filepath_or_url);
    const bool is_url = filepath_or_url.find("://") != std::string::npos;
    // A local file URL names its file by path; any other URL is the loader's.
    const auto local_url = is_url ? beebium::DiscUrl::parse(filepath_or_url) : std::nullopt;
    if (!is_url || (local_url && local_url->is_local())) {
        try {
            filepath = DiscPaths::resolve_disc_image(
                local_url ? local_url->to_filepath().string() : filepath_or_url,
                container_dirpath, destination);
        } catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::NOINPUT;
        }
        // Check existence before trying to canonicalize
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec)) {
            std::cerr << "Error: disc image not found: " << filepath_or_url << "\n";
            return ExitCode::NOINPUT;
        }
        filepath = std::filesystem::canonical(filepath, ec);
        if (ec) {
            std::cerr << "Error: cannot resolve disc image path: " << filepath_or_url
                      << " (" << ec.message() << ")\n";
            return ExitCode::NOINPUT;
        }
    }

    // The drive records the image by URL; a path becomes the file:// URL
    // DiscUrl spells (file:///C:/... for a Windows drive path).
    const auto source_url =
        is_url ? filepath_or_url : beebium::DiscUrl::from_filepath(filepath).url();
    std::cout << "Loading disc into floppy " << drive_num << ": " << filepath.string() << "\n";

    auto result = load_disc_from_url_or_filepath(filepath.string());
    if (!result) {
        std::cerr << "Error: " << result.error << "\n";
        return ExitCode::DATAERR;
    }

    if (result.disc->is_write_protected()) {
        std::cout << "  (write-protected)\n";
    }
    std::cout << "  format: " << result.disc->format_name() << "\n";

    drive.insert(std::move(result.disc), source_url);
    return std::nullopt;
}

// Flush any pending disc writes to the host image files.
//
// In normal operation each drive flushes itself when the head steps off a
// dirty track and on the controller's short write-inactivity timer. This is
// the shutdown backstop: if the server is asked to stop within that brief
// window after a write, flush whatever remains so no saved data is lost on
// exit.
template<typename MachineType>
void flush_disc_drives(MachineType& machine) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        machine.state().memory.disc_drive_0.flush_if_dirty();
        machine.state().memory.disc_drive_1.flush_if_dirty();
    }
}

// Advance any pending safe eject on the floppy drives.
//
// This belongs to the emulation loop because the emulation thread owns the
// drives: completing an eject frees the disc the disc controller reads pulses
// from, so it must not happen underneath a running machine. It is also the
// only thread guaranteed to exist -- an eject driven from a streaming RPC
// handler stops progressing the moment nobody is listening.
template<typename MachineType>
void tick_disc_drives(MachineType& machine) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        machine.state().memory.disc_drive_0.tick_eject();
        machine.state().memory.disc_drive_1.tick_eject();
    }
}

// Load disc images into floppy drives.
// Returns an exit code on error, or std::nullopt on success.
template<typename MachineType>
std::optional<int> load_disc_images(MachineType& machine, const ServerConfig<MachineType>& config,
                                    const DiscPaths::CopyDestination& destination) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        if (!config.machine.floppy_filepaths[0].empty()) {
            if (auto exit_code = load_single_disc(
                    machine.state().memory.disc_drive_0, 0, config.machine.floppy_filepaths[0],
                    config.launch.reference_container_dirpath(), destination)) {
                return exit_code;
            }
        }

        if (!config.machine.floppy_filepaths[1].empty()) {
            if (auto exit_code = load_single_disc(
                    machine.state().memory.disc_drive_1, 1, config.machine.floppy_filepaths[1],
                    config.launch.reference_container_dirpath(), destination)) {
                return exit_code;
            }
        }
    }
    return std::nullopt;
}

// Apply startup options (keyboard links) to machine.
// Must be called before machine.reset().
template<typename MachineType>
void apply_startup_options(MachineType& machine, const ServerConfig<MachineType>& config) {
    if (config.machine.raw_links >= 0) {
        // Raw byte overrides everything
        machine.state().memory.set_startup_options(static_cast<uint8_t>(config.machine.raw_links));
        std::cout << "Startup links: 0x" << std::hex << config.machine.raw_links << std::dec << "\n";
    } else {
        // Apply semantic options (modifies specific bits, preserves others)
        if (config.machine.screen_mode_set) {
            machine.state().memory.set_screen_mode(static_cast<uint8_t>(config.machine.screen_mode));
            std::cout << "Startup screen mode: " << config.machine.screen_mode << "\n";
        }
        if (config.machine.auto_boot_set) {
            machine.state().memory.set_auto_boot(config.machine.auto_boot);
            std::cout << "Auto-boot: " << (config.machine.auto_boot ? "enabled" : "disabled") << "\n";
        }
    }
}

// Enter the --wait=api paused-at-first-instruction state: complete the reset
// sequence (7 cycles) so PC holds the real reset vector (read from $FFFC/$FFFD
// during cycles 4-6), then pause. The emulation loop then parks here until a
// Run() RPC resumes. Factored out so the `start` path can enter this state
// BEFORE a deferred --station auto selection, so a client that connects the
// moment the port appears and calls Run() finds the machine paused (and Run
// waits for the selection) rather than racing it. See issue #67.
template<typename MachineType>
void enter_api_wait_state(MachineType& machine) {
    machine.run(7);
    machine.pause();
    std::cout << "Paused at first instruction (PC=$"
              << std::hex << std::uppercase
              << machine.state().cpu.pc.w
              << std::dec << "). Waiting for Run() RPC...\n";
}

// Handle wait mode for controlled startup.
// Must be called after machine.reset() and before the main emulation loop.
// Returns false if a shutdown signal arrived while waiting, in which case the
// caller must not start the emulation loop.
template<typename MachineType>
bool handle_wait_mode(MachineType& machine, WaitMode wait_mode) {
    switch (wait_mode) {
        case WaitMode::Cli:
            // Wait for user to press RETURN before starting emulation
            // "Now press RETURN." is a reference to Roger McGough's electronic poem
            // from the original BBC Micro Welcome cassette.
            std::cout << "Now press RETURN." << std::flush;
            if (!platform::wait_for_line_or_abandon(g_running)) {
                std::cout << "\n";
                return false;
            }
            std::cout << "\n";
            break;

        case WaitMode::Api:
            enter_api_wait_state(machine);
            break;

        case WaitMode::None:
            // Start immediately
            break;
    }
    return g_running.load();
}

// What one call to step_emulation did, for the caller's pacing/stats.
struct StepResult {
    bool parked = false;   // parked on a debugger pause this iteration
    bool ran = false;      // a cycle chunk actually executed
    std::chrono::steady_clock::duration run_duration{};  // time in machine.run()
};

// One iteration of the emulation thread's per-cycle work, shared by EVERY
// subcommand that runs the machine (the serving loop and the screenshot
// capture loop). This is the single place the machine is advanced, so no
// subcommand can acquire a divergent run behaviour (see
// docs/emulation-thread-ownership.md).
//
// It parks while the debugger has paused (ticking the drives as on_wake), then
// -- unless shutting down or a quiescer slipped in -- ticks the drives, updates
// the Econet speed gate and runs one cycle chunk under the busy scope. The
// caller owns pacing, stats and shutdown.
//
//   on_parked : invoked immediately after a debugger-pause park returns, before
//               the budget is taken (the serving loop rebases the pacing clock
//               here, which must happen before wait_for_tick). Pass a no-op
//               (`[]{}`) when there is nothing to do (the screenshot loop).
//   budget    : returns the cycle count for this chunk. The serving loop's
//               budget waits for the next pacing tick inside it -- a sleep that
//               must stay outside the busy scope and out of the run() timing --
//               and the screenshot loop's simply returns a fixed chunk.
template<typename MachineType, typename OnParked, typename Budget>
StepResult step_emulation(MachineType& machine, double speed_multiplier,
                          OnParked&& on_parked, Budget&& budget) {
    using Memory = typename MachineType::Memory;
    StepResult result;

    // A pending safe eject completes on this thread; ticking it as on_wake keeps
    // it progressing even while the machine is parked (a standing-still drive is
    // exactly when a disc may leave).
    result.parked = machine.wait_if_paused([&] { tick_disc_drives(machine); });
    if (result.parked) {
        on_parked();
    }
    if (machine.shutdown_requested()) {
        return result;
    }

    // Decide the cycle budget before entering the busy scope: a pacing
    // wait_for_tick() sleeps here and must not hold the emulation-busy guard.
    const uint64_t cycles = budget();

    // Everything below touches state the emulation thread owns. The busy scope
    // makes a quiescing caller wait for all of it, not just run(); if a pause
    // landed in the gap since wait_if_paused returned, the scope is inactive and
    // we skip the work so the caller re-parks rather than racing the quiescer.
    typename MachineType::EmulationBusyScope busy(machine);
    if (!busy.active()) {
        return result;
    }

    tick_disc_drives(machine);

    // Keep the Econet socket informed of the current speed so a transport that
    // requires real time (Piconet) is gated whenever speed != 1x.
    if constexpr (HasEconetSocket<Memory>) {
        machine.memory().econet_socket.set_emulation_speed(speed_multiplier);
    }

    auto run_start = std::chrono::steady_clock::now();
    machine.run(cycles);
    result.run_duration = std::chrono::steady_clock::now() - run_start;
    result.ran = true;
    return result;
}

// Register the name placeholders `ext` offers, if any (#153). A plugin whose
// placeholders break the registration rules (a key outside its domains, a
// domain another provider owns) is reported and its placeholders left out;
// the machine still launches.
template<typename MachineType>
void register_extension_name_placeholders(beebium::service::Server<MachineType>& server,
                                          const beebium::Extension& ext) {
    try {
        server.name_placeholders().add_extension(ext);
    } catch (const beebium::NamePlaceholderError& e) {
        std::cerr << "Warning: " << e.what() << "; its name placeholders are unavailable\n";
    }
}

// Run the main emulation loop with pacing.
// This function blocks until g_running becomes false (signal handler sets it).
// Sets up shutdown callbacks for clean signal handling.
template<typename MachineType>
void run_emulation_loop(MachineType& machine,
                        beebium::service::Server<MachineType>& server,
                        const ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    // Check for BEEBIUM_NO_PACING environment variable for debugging
    bool use_pacing = !platform::get_env("BEEBIUM_NO_PACING").has_value();

    // Create platform-specific sleep and measure its quantum
    PlatformSleep sleeper;
    auto quantum = beebium::measure_sleep_quantum(sleeper);

    auto pacing_config = Memory::default_pacing_config();
    if (config.machine.speed_multiplier_override.has_value()) {
        pacing_config.speed_multiplier = *config.machine.speed_multiplier_override;
    }

    // Create and start pacing clock with measured quantum
    PacingClock pacing_clock(pacing_config, quantum, std::move(sleeper));

    if (use_pacing) {
        pacing_clock.start();
        std::cout << "Pacing: quantum " << quantum.count() / 1000 << " us"
                  << " (" << 1'000'000'000 / quantum.count() << " Hz)"
                  << ", nominal " << pacing_clock.config().base_clock_hz / 1'000'000 << " MHz"
                  << "\n";
    } else {
        std::cout << "Pacing: DISABLED (BEEBIUM_NO_PACING set)\n";
    }

    // Wire pacing clock to gRPC SystemService for stats monitoring
    if (use_pacing) {
        server.set_pacing_clock(&pacing_clock);
    }

    // Set up shutdown callbacks so the signal handler can interrupt blocked
    // waits and notify clients of impending shutdown. Both capture objects
    // owned by this frame, so they are cleared before it returns (below); the
    // mutex makes that clearing wait out any invocation already running on the
    // shutdown dispatch thread.
    {
        std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
        g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
        g_request_pacing_stop = [&pacing_clock]() { pacing_clock.request_stop(); };
        g_notify_clients_shutdown = [&server]() { server.notify_shutdown(5000); };
    }
    // Clear them again however this function is left.
    struct CallbackScope {
        ~CallbackScope() {
            std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
            g_request_pacing_stop = nullptr;
            g_notify_clients_shutdown = nullptr;
        }
    } callback_scope;

    // Main emulation loop
    constexpr uint64_t cycles_per_frame = 40000;  // For non-paced mode

    // The gRPC pacing throughput is published far more frequently than the
    // human-readable stderr log, so clients (e.g. a speed slider) see a
    // commanded speed change reflected within ~1s rather than waiting up to the
    // 5s log interval. The two windows are tracked independently.
    constexpr auto publish_interval = std::chrono::milliseconds(500);
    constexpr auto log_interval = std::chrono::seconds(5);
    auto loop_start = std::chrono::steady_clock::now();
    auto last_publish_time = loop_start;
    auto last_log_time = loop_start;
    uint64_t last_publish_cycle_count = machine.cycle_count();
    uint64_t last_log_cycle_count = machine.cycle_count();
    std::chrono::steady_clock::duration publish_run_duration{};
    std::chrono::steady_clock::duration log_run_duration{};
    // Reset VSYNC edge counter for frequency measurement
    machine.memory().system_via_peripheral.consume_vsync_rising_edges();
    while (g_running) {
        // The pacing timer ticks thousands of times a second; while the
        // debugger holds the machine paused (including --wait=api before the
        // first Run()) nothing consumes those ticks, so park the timer too and
        // let the process sit idle. It is restarted, re-anchored to the current
        // cycle count, once the machine runs again -- on the park release below
        // or, if a Run() lands before the machine parks, here.
        if (use_pacing) {
            if (machine.is_paused()) {
                pacing_clock.pause();
            } else if (pacing_clock.is_paused()) {
                pacing_clock.rebase(machine.cycle_count());
                pacing_clock.resume();
            }
        }

        // Advance the machine one chunk through the shared stepper. Pacing is
        // this loop's own concern:
        //  - on_parked rebases the pacing clock when the debugger pause released
        //    (the wall time spent paused must not be charged as owed emulation,
        //    or the machine races to "catch up"; a hard reset zeroes the cycle
        //    count, so the baseline must follow it or the deficit underflows and
        //    paces the machine down to a crawl). It runs before wait_for_tick.
        //  - the budget waits for the next pacing tick (a sleep kept out of the
        //    busy scope and the run() timing) and returns that tick's cycles.
        StepResult step = step_emulation(
            machine,
            use_pacing ? pacing_clock.speed_multiplier() : 1.0,
            [&] {
                if (use_pacing) {
                    pacing_clock.rebase(machine.cycle_count());
                    pacing_clock.resume();
                }
            },
            [&]() -> uint64_t {
                if (!use_pacing) return cycles_per_frame;
                pacing_clock.wait_for_tick();
                return pacing_clock.cycles_for_next_tick();
            });

        // Check if shutdown was requested during the wait/park.
        if (machine.shutdown_requested()) {
            break;
        }

        if (step.ran) {
            publish_run_duration += step.run_duration;
            log_run_duration += step.run_duration;
            if (use_pacing) {
                pacing_clock.report_cycles(machine.cycle_count());
            }
        }

        // Periodic pacing stats
        if (use_pacing) {
            auto now = std::chrono::steady_clock::now();
            double target_hz = static_cast<double>(Memory::default_pacing_config().base_clock_hz);

            // Publish a throughput sample to the gRPC pacing stats every ~1s so
            // a commanded speed change is reflected promptly.
            if (now - last_publish_time >= publish_interval) {
                uint64_t current_cycles = machine.cycle_count();
                double secs = std::chrono::duration<double>(now - last_publish_time).count();
                double cycles_delta = static_cast<double>(current_cycles - last_publish_cycle_count);
                double achieved_multiplier = secs > 0.0 ? cycles_delta / secs / target_hz : 0.0;
                double active_fraction = secs > 0.0
                    ? std::chrono::duration<double>(publish_run_duration).count() / secs
                    : 0.0;
                pacing_clock.publish_throughput(
                    achieved_multiplier,
                    estimate_max_speed_multiplier(achieved_multiplier, active_fraction));
                last_publish_time = now;
                last_publish_cycle_count = current_cycles;
                publish_run_duration = {};
            }

            // Human-readable pacing log (every 5s). Written only to an
            // interactive terminal, and only if that will not block (see
            // stderr_is_terminal / stderr_accepts_nonblocking_write): a front end
            // that piped stderr and stopped draining would otherwise fill the
            // pipe and freeze the emulation thread in write(). The vsync counter
            // is consumed and the interval advanced whether or not the line goes
            // out, so a dropped line never skews later stats.
            if (now - last_log_time >= log_interval) {
                auto stats = pacing_clock.timing_stats();
                uint64_t current_cycles = machine.cycle_count();
                uint64_t cycles_delta = current_cycles - last_log_cycle_count;
                double elapsed_secs = std::chrono::duration<double>(now - last_log_time).count();
                double actual_hz = static_cast<double>(cycles_delta) / elapsed_secs;
                uint32_t vsync_edges = machine.memory().system_via_peripheral.consume_vsync_rising_edges();
                double vsync_hz = static_cast<double>(vsync_edges) / elapsed_secs;
                double run_secs = std::chrono::duration<double>(log_run_duration).count();
                double run_pct = 100.0 * run_secs / elapsed_secs;
                std::ostringstream line;
                line << "Pacing: "
                     << std::fixed << std::setprecision(3)
                     << (actual_hz / 1e6) << " MHz"
                     << " (target " << (target_hz / 1e6) << " MHz, "
                     << std::setprecision(1)
                     << (100.0 * actual_hz / target_hz) << "%)"
                     << " | vsync " << std::setprecision(1) << vsync_hz << " Hz"
                     << " | io " << stats.ticks_io_woken
                     << " | deficit " << std::setprecision(0) << stats.controller_deficit
                     << " | run " << std::setprecision(1) << run_pct << "%"
                     << "\n";
                if (stderr_is_terminal() && stderr_accepts_nonblocking_write()) {
                    std::cerr << line.str();
                }
                last_log_time = now;
                last_log_cycle_count = current_cycles;
                log_run_duration = {};
            }
        }
    }

    if (use_pacing) {
        pacing_clock.stop();
    }
}

// ============================================================================
// Machine assembly (shared by every subcommand)
// ============================================================================

// A fully assembled, ready-to-run machine and everything whose lifetime it
// depends on. Produced by assemble_machine() and consumed by both `start`
// (which serves it over gRPC) and `capture-screenshot` (which runs it headless
// to a PNG). It is the ONLY place a MachineType is constructed for running, so
// the two subcommands cannot diverge in how the machine is built.
//
// Member order fixes destruction order (reverse). The machine is destroyed
// last; the registries and context, which hold references into the machine's
// memory, are torn down first. In particular the transport registry must die
// before the EconetSocket backend it references -- a transport's mDNS
// collaborators call back into that backend -- so it is declared after the
// machine. Moving it above the machine would be a use-after-free.
template<typename MachineType>
struct AssembledMachine {
    // The directory the machine owns, its media copies in it. Declared first,
    // so it is removed last, once the machine and its extensions have closed
    // those files (Windows will not remove an open file).
    std::optional<MachineDirectory> machine_directory;
    MachineType machine;
    beebium::EconetTransportRegistry transport_registry;
    beebium::ExtensionRegistry extension_registry;
    // Holds raw pointers into machine memory; only meaningful for the life of
    // the machine, hence declared after it. Emplaced during assembly.
    std::optional<beebium::ExtensionContext> extension_context;
    // The coprocessor extension resolved from the registry, or null. Non-owning
    // (extension_registry owns it).
    beebium::CoprocessorExtension* coprocessor = nullptr;

    // Set when --station auto was DEFERRED past assembly (the `start` path): the
    // range to choose from once the gRPC server is listening and its port is
    // printed, so a slow mDNS browse never delays the port line. Econet is not
    // enabled until then. Empty for a fixed station or an offline resolve.
    std::optional<beebium::econet::StationRange> pending_auto_station;

    AssembledMachine() = default;
    AssembledMachine(const AssembledMachine&) = delete;
    AssembledMachine& operator=(const AssembledMachine&) = delete;
};

// Result of assemble_machine: the machine on success, or an exit code to
// return when assembly failed (assembled == nullptr).
template<typename MachineType>
struct AssemblyOutcome {
    std::unique_ptr<AssembledMachine<MachineType>> assembled;
    int exit_code = ExitCode::OK;
};

// Build the machine from `config`: ROMs, disc controller, Econet (with its
// transport registry), disc images, output enablement, startup options,
// extension registration + resolve_and_init, the server-supplied quiescer
// wiring (bus quiescer for every dispatcher, execution quiescer for the
// coprocessor debug target -- see docs/emulation-thread-ownership.md), and
// reset. gRPC services, counterpart-stop wiring and pacing belong to serving,
// not assembly, and are done by the caller.
//
// `with_audio` enables audio output in addition to video (video is always on);
// the screenshot path passes false. Machine assembly runs on the calling
// thread before the emulation loop starts.
template<typename MachineType>
AssemblyOutcome<MachineType> assemble_machine(ServerConfig<MachineType>& config,
                                              bool with_audio,
                                              bool defer_auto_station = false) {
    using Memory = typename MachineType::Memory;

    std::cout << "Initializing " << Memory::MACHINE_DISPLAY_NAME << "...\n";

    auto am = std::make_unique<AssembledMachine<MachineType>>();
    auto& machine = am->machine;
    try {
        am->machine_directory.emplace(MachineDirectory::create());
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot make the machine directory: " << e.what() << "\n";
        return {nullptr, ExitCode::IOERR};
    }

    // Load ROMs
    load_roms(machine, config);

    // Install disc controller
    if (auto exit_code = install_disc_controller(machine, config)) {
        return {nullptr, *exit_code};
    }

    // Resolve the machine identity UUID up front so transport extensions can
    // pick it up via inst.config["machine_uuid"] before their create_backend
    // runs. The MachineIdentity proper is constructed later (serving), but the
    // UUID -- the only value extensions need -- is fixed once generated.
    if (config.launch.machine_uuid.empty()) {
        config.launch.machine_uuid = generate_uuid_v4();
    }

    beebium::PluginLoader plugin_loader;
    if (config.launch.default_extension_dirpath) {
        std::cout << "Extension directory: " << *config.launch.default_extension_dirpath << "\n";
    }
    for (const auto& p : config.launch.extension_dirpaths) {
        std::cout << "Extension directory: " << p << "\n";
    }

    // Build the Econet transport registry from any econet-transport extension
    // instances, before install_econet so the transport is ready when the
    // machine first reads the ADLC. Transport-extension instances are removed
    // from config.machine.extension_instances; the later peripheral load loop only sees
    // peripherals.
    {
        std::vector<typename ServerConfig<MachineType>::ExtensionInstance> remaining;
        remaining.reserve(config.machine.extension_instances.size());
        for (auto& inst : config.machine.extension_instances) {
            const beebium::ExtensionManifest* manifest =
                config.launch.extension_resolver.find_by_name(inst.name);

            if (!manifest || manifest->extension_kind != "econet-transport") {
                remaining.push_back(std::move(inst));
                continue;
            }

            inst.config["machine_uuid"] = config.launch.machine_uuid;
            auto loaded = instantiate_extension(*manifest, inst, plugin_loader, "transport");
            if (!loaded) {
                return {nullptr, ExitCode::CONFIG};
            }

            auto* transport = dynamic_cast<beebium::EconetTransportExtension*>(loaded.get());
            if (!transport) {
                std::cerr << "Error: extension '" << manifest->name
                          << "' has extension_kind=econet-transport but is "
                             "not an EconetTransportExtension instance.\n";
                return {nullptr, 1};
            }
            (void)loaded.release();
            am->transport_registry.add(
                std::unique_ptr<beebium::EconetTransportExtension>(transport));
        }
        config.machine.extension_instances = std::move(remaining);
    }

    // Validate each transport's config before anything else (issue #158): an
    // invalid --aun discovery= value, say, is a clear launch error here rather
    // than a silent fallback or a failure deep in create_backend.
    for (const auto& transport : am->transport_registry.extensions()) {
        if (auto error = transport->config_error()) {
            std::cerr << "Error: " << transport->name() << " " << *error << "\n";
            return {nullptr, ExitCode::USAGE};
        }
    }

    // Install Econet hardware. --station auto is resolved to a concrete number
    // first. On the `start` path we DEFER that resolution (it may browse mDNS
    // for a few seconds) until the gRPC server is listening and its port is
    // printed, so a launcher's port-wait never times out; Econet is enabled
    // then, before the machine runs. Off that path (capture-screenshot), or for
    // a fixed station, resolve now -- offline (no browse/announce) so build-time
    // boots stay deterministic.
    if (config.machine.station_auto_range.has_value() && defer_auto_station) {
        am->pending_auto_station = config.machine.station_auto_range;
    } else {
        int resolved_station = config.machine.station_number;
        bool auto_assigned = false;
        if (auto exit_code = resolve_auto_station(
                config, am->transport_registry, /*offline=*/true,
                resolved_station, auto_assigned)) {
            return {nullptr, *exit_code};
        }
        if (auto exit_code = install_econet(machine, config, am->transport_registry,
                                            resolved_station, auto_assigned)) {
            return {nullptr, *exit_code};
        }
    }

    // Where the media a template names are copied: the machine's directory.
    const DiscPaths::CopyDestination disc_destination{
        am->machine_directory->dirpath(),
        config.launch.preset
            ? std::optional(config.launch.reference_container_dirpath())
            : std::nullopt};

    // Load disc images
    if (auto exit_code = load_disc_images(machine, config, disc_destination)) {
        return {nullptr, *exit_code};
    }

    // Enable output. Video is always on; audio only when the caller wants it
    // (the screenshot path does not).
    machine.state().memory.enable_video_output();
    if (with_audio) {
        machine.state().memory.enable_audio_output();
    }

    // Apply startup options before reset
    apply_startup_options(machine, config);

    // Reset machine
    machine.reset();

    // Set up peripheral extension registry.
    beebium::ExtensionRegistry& extension_registry = am->extension_registry;
    for (const auto& point : machine_attachment_points<Memory>()) {
        extension_registry.register_extension_point(point);
    }

    // Load peripheral extensions (from --<cli-name> flags). Sort so providers
    // are loaded before consumers (RTLD_GLOBAL takes effect before children
    // resolve parent symbols); stable_partition preserves command-line order.
    std::stable_partition(
        config.machine.extension_instances.begin(),
        config.machine.extension_instances.end(),
        [&config](const auto& inst) {
            const auto* m = config.launch.extension_resolver.find_by_name(inst.name);
            return m && !m->provides.empty();
        });

    for (auto& inst : config.machine.extension_instances) {
        const beebium::ExtensionManifest* manifest =
            config.launch.extension_resolver.find_by_name(inst.name);
        if (!manifest) {
            std::cerr << "Error: Extension '" << inst.name << "' not found";
            for (const auto& p : config.launch.extension_dirpaths) {
                std::cerr << " in " << p;
            }
            if (config.launch.default_extension_dirpath && config.launch.extension_dirpaths.empty()) {
                std::cerr << " in " << *config.launch.default_extension_dirpath;
            }
            std::cerr << "\n";
            return {nullptr, ExitCode::CONFIG};
        }

        // A file path parameter arrives anchored in the launch's container, by
        // the preset loader or the option parser (see ResourcePaths.hpp). One
        // the manifest declares bundled names a resource, and a bare name is
        // searched for: a hard disc through DiscPaths (copied into the
        // machine's directory, so a shipped read-only master is never opened
        // writable), a ROM on the ROM search path ending in the roms/ of
        // the plugin that ships it. Any other names a location the extension
        // owns, which is never searched.
        const auto container_dirpath = config.launch.reference_container_dirpath();
        for (const auto& p : manifest->parameters) {
            if (p.type != "filepath") continue;
            auto it = inst.config.find(p.key);
            if (it == inst.config.end() || it->second.empty()) continue;
            if (p.bundled == "hard-disc") {
                try {
                    it->second = beebium::server::DiscPaths::resolve_disc_image(
                                     it->second, container_dirpath, disc_destination)
                                     .string();
                } catch (const std::exception& e) {
                    std::cerr << "Error: cannot resolve disc image for '"
                              << inst.name << "': " << e.what() << "\n";
                    return {nullptr, ExitCode::CONFIG};
                }
            } else if (!p.bundled.empty() && is_bare_reference(it->second)) {
                // The manifest decides which names may be given; the search
                // decides where the file comes from.
                const auto images = bundled_images(config.launch.extension_resolver);
                const auto* image = find_bundled_image(images, *manifest, p, it->second);
                if (!image) {
                    std::cerr << "Error: " << inst.name << " " << p.key << " '" << it->second
                              << "' is not a " << p.bundled << " this extension ships"
                              << " (for a file of your own, give its path, e.g. ./"
                              << it->second << ")\n";
                    return {nullptr, ExitCode::CONFIG};
                }
                try {
                    it->second = resolve_reference(it->second, ResourceKind::Rom,
                                                   container_dirpath,
                                                   {image->filepath.parent_path()})
                                     .path.string();
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << inst.name << " " << p.key << ": " << e.what()
                              << "\n";
                    return {nullptr, ExitCode::CONFIG};
                }
            }
        }

        auto loaded = instantiate_extension(*manifest, inst, plugin_loader, "extension");
        if (!loaded) {
            return {nullptr, ExitCode::CONFIG};
        }

        if (manifest->extension_kind == "peripheral") {
            auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
            if (!peripheral) {
                std::cerr << "Error: Extension '" << inst.name
                          << "' has extension_kind=peripheral but is not a "
                             "PeripheralExtension instance.\n";
                return {nullptr, ExitCode::CONFIG};
            }
            (void)loaded.release();
            extension_registry.register_extension(
                std::unique_ptr<beebium::PeripheralExtension>(peripheral));
        } else {
            std::cerr << "Error: Extension '" << inst.name
                      << "' has unsupported extension_kind '"
                      << manifest->extension_kind << "'.\n";
            return {nullptr, ExitCode::CONFIG};
        }
    }

    // Initialise extensions (dependency resolution + topological sort). This is
    // what installs a coprocessor into the Tube socket.
    std::cout << "Initialising extensions...\n";
    am->extension_context.emplace(
        beebium::HasOneMHzBus<Memory> ? &machine.state().memory.one_mhz_bus() : nullptr,
        beebium::HasUserPort<Memory> ? &machine.state().memory.user_port() : nullptr,
        beebium::HasTubeSocket<Memory> ? &machine.state().memory.tube_socket : nullptr,
        &machine.state().memory.indicators,
        beebium::HasSerialPort<Memory> ? &machine.state().memory.serial_port() : nullptr);
    extension_registry.resolve_and_init(*am->extension_context);

    // Wire the bus quiescer into every extension dispatcher so a gRPC-thread
    // handler halts the emulation thread across a device mutation. The server
    // supplies it (forwarding to Machine::with_emulation_paused); a dispatcher
    // author never sets it.
    for (auto* ext : extension_registry.extensions()) {
        for (auto* dispatcher : ext->rpc_dispatchers()) {
            dispatcher->set_bus_quiescer(
                [&machine](const std::function<void()>& fn) {
                    machine.with_emulation_paused(fn);
                });
        }
    }

    // All registrations are complete -- close the registration window and start
    // the indicators consumer thread.
    machine.state().memory.indicators.start();

    // Log initialisation order
    for (auto* ext : extension_registry.extensions()) {
        std::cout << "  " << ext->name();
        if (!ext->id().empty()) {
            std::cout << " [" << ext->id() << "]";
        }
        if (!ext->provides().empty()) {
            std::cout << " provides:";
            for (auto p : ext->provides()) std::cout << " " << p;
        }
        if (!ext->attaches_to().empty()) {
            std::cout << " on:";
            for (auto a : ext->attaches_to()) std::cout << " " << a;
        }
        std::cout << "\n";
    }

    // Find the coprocessor extension, if any, through the abstract interface.
    // There is one Tube socket, so at most one coprocessor may attach.
    for (auto* ext : extension_registry.extensions()) {
        if (auto* cop = dynamic_cast<beebium::CoprocessorExtension*>(ext)) {
            if (am->coprocessor) {
                std::cerr << "Error: more than one coprocessor attached to the "
                             "Tube; the Tube has a single socket.\n";
                return {nullptr, 1};
            }
            am->coprocessor = cop;
        }
    }

    // Wire the coprocessor debug target's execution quiescer: halting the
    // coprocessor (which runs on the host emulation thread) to mutate its debug
    // entries means pausing the host machine. The debugger SERVICE built over
    // this target is a serving concern and belongs to the caller.
    if (am->coprocessor) {
        if (auto* target = am->coprocessor->debug_target()) {
            target->set_execution_quiescer(
                [&machine](const std::function<void()>& fn) {
                    machine.with_emulation_paused(fn);
                });
        }
    }

    return {std::move(am), ExitCode::OK};
}

} // namespace beebium::server
