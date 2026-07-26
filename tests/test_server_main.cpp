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

// test_server_main.cpp
// Tests for server_main helper functions (validate_config, load_roms, etc.)

#include <beebium/server/ServerMain.hpp>
#include <beebium/Machines.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium::server;
using MachineType = beebium::ModelB;

// ============================================================================
// validate_config() tests
// ============================================================================

TEST_CASE("validate_config: valid empty config returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

TEST_CASE("validate_config: --links with --screen-mode returns error", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.raw_links = 128;
    config.screen_mode_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE(error.has_value());
    REQUIRE(error->find("--links") != std::string::npos);
    REQUIRE(error->find("--screen-mode") != std::string::npos);
}

TEST_CASE("validate_config: --links with --auto-boot returns error", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.raw_links = 128;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE(error.has_value());
    REQUIRE(error->find("--links") != std::string::npos);
    REQUIRE(error->find("--auto-boot") != std::string::npos);
}

TEST_CASE("validate_config: --links with both --screen-mode and --auto-boot returns error", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.raw_links = 128;
    config.screen_mode_set = true;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE(error.has_value());
}

TEST_CASE("validate_config: --screen-mode without --links returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.screen_mode = 4;
    config.screen_mode_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

TEST_CASE("validate_config: --auto-boot without --links returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.auto_boot = true;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

TEST_CASE("validate_config: both --screen-mode and --auto-boot without --links returns nullopt", "[server_main][validate_config]") {
    ServerConfig<MachineType> config;
    config.screen_mode = 4;
    config.screen_mode_set = true;
    config.auto_boot = true;
    config.auto_boot_set = true;

    auto error = validate_config<MachineType>(config);

    REQUIRE_FALSE(error.has_value());
}

// ============================================================================
// apply_preset() + merge_preset_sideways_configs() - sideways slots
// ============================================================================

TEST_CASE("apply_preset: records preset sideways slots as baseline",
          "[server_main][preset][sideways]") {
    ServerConfig<MachineType> config;
    PresetConfig preset;
    preset.sideways = {
        SidewaysConfig{14, SidewaysSlotType::Rom, "acorn-dfs_2_26.rom"},
    };

    apply_preset(config, preset);

    // apply_preset only stages the baseline; it does not yet touch the active
    // sideways_configs or rom_slots (the merge does, after CLI parsing).
    REQUIRE(config.preset_sideways_configs.size() == 1);
    REQUIRE(config.preset_sideways_configs[0].slot == 14);
    REQUIRE(config.sideways_configs.empty());
    REQUIRE(config.rom_slots.count(14) == 0);
}

TEST_CASE("merge_preset_sideways_configs: applies a preset ROM slot",
          "[server_main][preset][sideways]") {
    ServerConfig<MachineType> config;
    config.preset_sideways_configs = {
        SidewaysConfig{14, SidewaysSlotType::Rom, "acorn-dfs_2_26.rom"},
    };

    merge_preset_sideways_configs(config);

    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 14);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Rom);
    REQUIRE(config.rom_slots[14] == "acorn-dfs_2_26.rom");
}

TEST_CASE("merge_preset_sideways_configs: empty and ram slots map to markers",
          "[server_main][preset][sideways]") {
    ServerConfig<MachineType> config;
    config.preset_sideways_configs = {
        SidewaysConfig{13, SidewaysSlotType::Empty, ""},
        SidewaysConfig{4, SidewaysSlotType::Ram, ""},
    };

    merge_preset_sideways_configs(config);

    REQUIRE(config.rom_slots[13] == EMPTY_SLOT_MARKER);
    REQUIRE(config.rom_slots[4] == RAM_SLOT_MARKER);
}

TEST_CASE("merge_preset_sideways_configs: CLI --sideways overrides preset for same slot",
          "[server_main][preset][sideways]") {
    ServerConfig<MachineType> config;
    // Simulate a CLI --sideways 14:empty already parsed, as the second pass of
    // parse_start_arguments would leave it.
    config.sideways_configs.push_back(SidewaysConfig{14, SidewaysSlotType::Empty, ""});
    config.rom_slots[14] = EMPTY_SLOT_MARKER;
    // The preset wants DFS in slot 14.
    config.preset_sideways_configs = {
        SidewaysConfig{14, SidewaysSlotType::Rom, "acorn-dfs_2_26.rom"},
    };

    merge_preset_sideways_configs(config);

    // CLI wins: exactly one slot-14 entry, still Empty, no duplicate added.
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Empty);
    REQUIRE(config.rom_slots[14] == EMPTY_SLOT_MARKER);
}

// ============================================================================
// load_roms() tests
// ============================================================================

#ifdef BEEBIUM_ROM_DIR
// Helper to set up config for testing
// For machines with a default DFS (like Model B+), this would clear the DFS slot
// to avoid loading a ROM that might not exist in the test environment.
// Model B has no default DFS since it has no FDC by default.
void setup_test_config(ServerConfig<MachineType>& /*config*/) {
    // Model B has no default DFS ROM, so nothing to clear
}

TEST_CASE("load_roms: applies default MOS ROM when not specified", "[server_main][load_roms]") {
    using Memory = MachineType::Memory;
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    // config.mos_filepath is empty, should use default

    load_roms(machine, config);

    REQUIRE(config.mos_filepath == Memory::DEFAULT_MOS_ROM);
}

TEST_CASE("load_roms: uses specified MOS ROM filepath", "[server_main][load_roms]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    config.mos_filepath = "acorn-mos_1_20.rom";  // Explicit MOS

    load_roms(machine, config);

    REQUIRE(config.mos_filepath == "acorn-mos_1_20.rom");
}

TEST_CASE("load_roms: applies default language ROM when slot not overridden", "[server_main][load_roms]") {
    using Memory = MachineType::Memory;
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);

    load_roms(machine, config);

    REQUIRE(config.rom_slots.count(Memory::DEFAULT_LANGUAGE_SLOT) == 1);
    REQUIRE(config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] == Memory::DEFAULT_LANGUAGE_ROM);
}

TEST_CASE("load_roms: does not override user-specified language slot", "[server_main][load_roms]") {
    using Memory = MachineType::Memory;
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] = "acorn-mos_1_20.rom";  // Use MOS as test ROM

    load_roms(machine, config);

    // Should not have been overwritten with default
    REQUIRE(config.rom_slots[Memory::DEFAULT_LANGUAGE_SLOT] == "acorn-mos_1_20.rom");
}

TEST_CASE("load_roms: handles empty slot marker", "[server_main][load_roms]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);
    config.rom_slots[10] = EMPTY_SLOT_MARKER;

    // Should not throw
    REQUIRE_NOTHROW(load_roms(machine, config));
}

TEST_CASE("load_roms: loads MOS ROM into machine memory", "[server_main][load_roms]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);

    // Should not throw
    REQUIRE_NOTHROW(load_roms(machine, config));

    // After load_roms, can read from the MOS ROM area (0xC000-0xFFFF)
    // The reset vector at 0xFFFC/0xFFFD should contain the entry point
    // (MOS 1.20 reset vector is $D9CD)
    uint8_t reset_low = machine.state().memory.read(0xFFFC);
    uint8_t reset_high = machine.state().memory.read(0xFFFD);
    uint16_t reset_vector = reset_low | (reset_high << 8);

    // MOS 1.20 reset vector should be in the range 0xC000-0xFFFF
    REQUIRE(reset_vector >= 0xC000);
}

TEST_CASE("load_roms: preset sideways DFS loads into slot 14",
          "[server_main][load_roms][preset][sideways]") {
    RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);

    MachineType machine;
    ServerConfig<MachineType> config;
    setup_test_config(config);

    // A "Model B (Disc)" preset: plain Model B has no default DFS, so the
    // preset must supply one. apply_preset stages it; merge folds it in.
    PresetConfig preset;
    preset.sideways = {
        SidewaysConfig{14, SidewaysSlotType::Rom, "acorn-dfs_2_26.rom"},
    };
    apply_preset(config, preset);
    merge_preset_sideways_configs(config);

    REQUIRE(config.rom_slots[14] == "acorn-dfs_2_26.rom");
    REQUIRE_NOTHROW(load_roms(machine, config));
}

// ============================================================================
// install_disc_controller() tests
// ============================================================================

TEST_CASE("install_disc_controller: returns nullopt when no FDC specified", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    // config.fdc_type is empty

    auto result = install_disc_controller(machine, config);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("install_disc_controller: returns nullopt for 'none' type", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.fdc_type = "none";

    auto result = install_disc_controller(machine, config);

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("install_disc_controller: installs valid FDC type", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.fdc_type = "acorn-1770";

    auto result = install_disc_controller(machine, config);

    REQUIRE_FALSE(result.has_value());
    // Machine should now have a disc controller installed
    REQUIRE(machine.state().memory.has_disc_controller());
}

TEST_CASE("install_disc_controller: returns error for unknown FDC type", "[server_main][install_disc_controller]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.fdc_type = "unknown-controller";

    auto result = install_disc_controller(machine, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == 1);
}

// ============================================================================
// load_disc_images() tests
// ============================================================================

TEST_CASE("load_disc_images: handles empty floppy paths (no-op)", "[server_main][load_disc_images]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    // config.floppy_filepaths are empty

    // Should not throw
    REQUIRE_NOTHROW(load_disc_images(machine, config));
}

// ============================================================================
// apply_startup_options() tests
// ============================================================================

TEST_CASE("apply_startup_options: does nothing when no options set", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    // All startup options are at default values

    // Should not throw
    REQUIRE_NOTHROW(apply_startup_options(machine, config));
}

TEST_CASE("apply_startup_options: sets raw links when specified", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.raw_links = 0xAB;

    apply_startup_options(machine, config);

    REQUIRE(machine.state().memory.startup_options() == 0xAB);
}

TEST_CASE("apply_startup_options: sets screen mode when specified", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.screen_mode = 4;
    config.screen_mode_set = true;

    apply_startup_options(machine, config);

    // Screen mode is encoded in bits 0-2 of startup options
    uint8_t startup = machine.state().memory.startup_options();
    REQUIRE((startup & 0x07) == 4);
}

TEST_CASE("apply_startup_options: sets auto-boot when specified", "[server_main][apply_startup_options]") {
    MachineType machine;
    ServerConfig<MachineType> config;
    config.auto_boot = true;
    config.auto_boot_set = true;

    // Should not throw
    REQUIRE_NOTHROW(apply_startup_options(machine, config));
}
#endif

// ============================================================================
// Signal handler tests
// ============================================================================

#include <beebium/server/Platform.hpp>
#include <csignal>

#ifndef _WIN32
TEST_CASE("signal handler: sets atomic flag without calling callback directly",
          "[server_main][signal]") {
    // Reset state
    beebium::server::platform::detail::g_signal_received.store(false);

    bool callback_called = false;
    beebium::server::platform::install_shutdown_handler([&callback_called] {
        callback_called = true;
    });

    // Simulate what the kernel does: call the signal handler function
    beebium::server::platform::detail::signal_handler(SIGTERM);

    // The handler should have set the atomic flag but NOT called the callback
    REQUIRE(beebium::server::platform::detail::g_signal_received.load());
    REQUIRE_FALSE(callback_called);

    // Now dispatch the pending signal (as the main loop would)
    beebium::server::platform::dispatch_pending_signal();

    // Now the callback should have been called
    REQUIRE(callback_called);
    // And the flag should be cleared
    REQUIRE_FALSE(beebium::server::platform::detail::g_signal_received.load());

    // Clean up
    beebium::server::platform::remove_shutdown_handler();
}

TEST_CASE("signal handler: dispatch_pending_signal is a no-op when no signal received",
          "[server_main][signal]") {
    beebium::server::platform::detail::g_signal_received.store(false);

    bool callback_called = false;
    beebium::server::platform::install_shutdown_handler([&callback_called] {
        callback_called = true;
    });

    // Dispatch without any signal having been received
    beebium::server::platform::dispatch_pending_signal();

    REQUIRE_FALSE(callback_called);

    beebium::server::platform::remove_shutdown_handler();
}
#endif  // !_WIN32

TEST_CASE("signal handler: invoke_shutdown sets g_running false and interrupts waits",
          "[server_main][signal]") {
    // Reset g_running
    g_running = true;

    MachineType machine;
    bool pacing_stopped = false;

    g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
    g_request_pacing_stop = [&pacing_stopped]() { pacing_stopped = true; };
    g_notify_clients_shutdown = nullptr;

    invoke_shutdown();

    REQUIRE_FALSE(g_running);
    REQUIRE(machine.shutdown_requested());
    REQUIRE(pacing_stopped);

    // Clean up
    g_request_machine_shutdown = nullptr;
    g_request_pacing_stop = nullptr;
    g_running = true;
}

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

TEST_CASE("stderr_accepts_nonblocking_write reflects pipe writability",
          "[server_main]") {
    // The emulation loop calls this before its pacing log so a piped, undrained
    // stderr that has filled up cannot block write() and freeze emulation --
    // the fault that hangs an app-launched server. A full pipe must read as
    // "would block" (skip the log); draining it must restore writability.
    int saved_stderr = dup(STDERR_FILENO);
    REQUIRE(saved_stderr >= 0);

    int fds[2];
    REQUIRE(pipe(fds) == 0);
    // Non-blocking write end so filling it here cannot block the test itself.
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    char junk[4096];
    while (write(fds[1], junk, sizeof(junk)) > 0) { /* fill the buffer */ }

    // Point stderr at the now-full pipe.
    REQUIRE(dup2(fds[1], STDERR_FILENO) >= 0);
    // A pipe is not a terminal, so the log would be suppressed on this ground
    // alone -- and it would block, so the writability gate rejects it too.
    CHECK_FALSE(beebium::server::stderr_is_terminal());
    CHECK_FALSE(beebium::server::stderr_accepts_nonblocking_write());

    // Draining frees space, so it becomes writable again.
    char sink[8192];
    REQUIRE(read(fds[0], sink, sizeof(sink)) > 0);
    CHECK(beebium::server::stderr_accepts_nonblocking_write());

    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
    close(fds[0]);
    close(fds[1]);
}

TEST_CASE("SIGTERM: terminates server process within 2 seconds",
          "[server_main][signal][integration]") {
    // Fork a child that installs the shutdown handler and blocks in a loop
    pid_t child = fork();
    REQUIRE(child >= 0);

    if (child == 0) {
        // Child process: install handler and block
        std::atomic<bool> running{true};
        beebium::server::platform::install_shutdown_handler([&running] {
            running = false;
        });

        // Simulate the main loop: poll for pending signals with bounded waits
        while (running) {
            beebium::server::platform::dispatch_pending_signal();
            usleep(10000);  // 10ms
        }

        _exit(0);
    }

    // Parent: give child time to start, then send SIGTERM
    usleep(100000);  // 100ms
    kill(child, SIGTERM);

    // Wait for child to exit (timeout 2 seconds)
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 40; ++i) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        usleep(50000);  // 50ms
    }

    if (!exited) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }

    REQUIRE(exited);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}
#endif
