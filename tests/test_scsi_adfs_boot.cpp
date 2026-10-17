// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Full end-to-end integration test: Boot a Model B+ with ADFS ROM and
// SCSI hard disc, select the hard disc, and execute *CAT to verify the
// complete stack from 6502 code through ADFS through SCSI bus protocol
// to disc image file I/O.

#include <catch2/catch_test_macros.hpp>
#include "test_extension_ids.hpp"

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <6502/6502.h>

#include <AcornScsiHostAdapter.hpp>
#include <ScsiHardDisc.hpp>
#include <ScsiHardDiscExtension.hpp>

#include "test_keyboard_helpers.hpp"
#include "test_temp_paths.hpp"

#include <array>
#include <filesystem>
#include <fstream>

using namespace beebium;

namespace {

#ifdef BEEBIUM_ROM_DIR
const std::filesystem::path kRomDir = BEEBIUM_ROM_DIR;
#else
const std::filesystem::path kRomDir;
#endif

#ifdef BEEBIUM_TEST_ASSETS_DIR
const std::filesystem::path kAssetsDir = BEEBIUM_TEST_ASSETS_DIR;
#else
const std::filesystem::path kAssetsDir;
#endif

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool roms_available() {
    return !kRomDir.empty()
        && std::filesystem::exists(kRomDir / "acorn-mos_2_0.rom")
        && std::filesystem::exists(kRomDir / "bbc-basic_2.rom");
}

bool adfs_available() {
    return !kAssetsDir.empty()
        && std::filesystem::exists(kAssetsDir / "roms" / "acorn-adfs_1_30.rom")
        && std::filesystem::exists(kAssetsDir / "scsi" / "scsi0.dat")
        && std::filesystem::exists(kAssetsDir / "scsi" / "scsi0.dsc");
}

template<typename MachineType>
std::string read_mode7_screen(MachineType& machine, int row, int col, int length) {
    std::string result;
    uint16_t addr = 0x7C00 + row * 40 + col;
    for (int i = 0; i < length; ++i) {
        uint8_t ch = machine.read(addr + i);
        if (ch >= 0x20 && ch <= 0x7E) {
            result += static_cast<char>(ch);
        } else {
            result += '.';
        }
    }
    return result;
}

template<typename MachineType>
bool find_string_on_screen(MachineType& machine, const std::string& target, int max_rows = 25) {
    for (int row = 0; row < max_rows; ++row) {
        std::string screen_row = read_mode7_screen(machine, row, 0, 40);
        if (screen_row.find(target) != std::string::npos) {
            return true;
        }
    }
    return false;
}

template<typename MachineType>
void dump_screen(MachineType& machine) {
    for (int row = 0; row < 25; ++row) {
        WARN("  Row " << row << ": [" << read_mode7_screen(machine, row, 0, 40) << "]");
    }
}

void run_cycles(ModelBPlus& machine, FrameRenderer& renderer, uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            renderer.process(machine.memory().video_output.value());
        }
    }
}

}  // namespace

TEST_CASE("ScsiHardDiscExtension registers hdd-N-activity-led when indicators are available",
          "[scsi][hdd][indicators]") {
    if (!adfs_available()) SKIP("SCSI disc image not available");

    ModelBPlus machine;

    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    registry.register_extension(with_default_id(AcornScsiHostAdapter::create()));
    auto hdd_ext = ScsiHardDiscExtension::create();
    hdd_ext->set_config({
        {"scsi-id", "0"},
        {"image", (kAssetsDir / "scsi" / "scsi0.dat").string()},
        {"id", "test-hdd"},
    });
    registry.register_extension(with_default_id(std::move(hdd_ext)));

    ExtensionContext ctx(
        &machine.memory().one_mhz_bus(), nullptr, nullptr, &machine.memory().indicators);
    registry.resolve_and_init(ctx);

    auto names = machine.memory().indicators.names();
    REQUIRE(std::find(names.begin(), names.end(), "hdd-0-activity-led") != names.end());

    auto meta = machine.memory().indicators.metadata("hdd-0-activity-led");
    REQUIRE(meta.count("label") == 1);
    REQUIRE(meta.count("color") == 1);
    REQUIRE(meta.count("shape") == 1);
}

TEST_CASE("ScsiHardDiscExtension uses configured scsi-id in indicator name",
          "[scsi][hdd][indicators]") {
    if (!adfs_available()) SKIP("SCSI disc image not available");

    ModelBPlus machine;

    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    registry.register_extension(with_default_id(AcornScsiHostAdapter::create()));
    auto hdd_ext = ScsiHardDiscExtension::create();
    hdd_ext->set_config({
        {"scsi-id", "5"},
        {"image", (kAssetsDir / "scsi" / "scsi0.dat").string()},
        {"id", "test-hdd"},
    });
    registry.register_extension(with_default_id(std::move(hdd_ext)));

    ExtensionContext ctx(
        &machine.memory().one_mhz_bus(), nullptr, nullptr, &machine.memory().indicators);
    registry.resolve_and_init(ctx);

    auto names = machine.memory().indicators.names();
    REQUIRE(std::find(names.begin(), names.end(), "hdd-5-activity-led") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "hdd-0-activity-led") == names.end());
}

TEST_CASE("ScsiHardDiscExtension init without indicators does not throw",
          "[scsi][hdd][indicators]") {
    if (!adfs_available()) SKIP("SCSI disc image not available");

    ModelBPlus machine;

    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    registry.register_extension(with_default_id(AcornScsiHostAdapter::create()));
    auto hdd_ext = ScsiHardDiscExtension::create();
    hdd_ext->set_config({
        {"scsi-id", "0"},
        {"image", (kAssetsDir / "scsi" / "scsi0.dat").string()},
        {"id", "test-hdd"},
    });
    registry.register_extension(with_default_id(std::move(hdd_ext)));

    // Construct the context WITHOUT indicators (4th arg defaulted to nullptr).
    ExtensionContext ctx(&machine.memory().one_mhz_bus());
    REQUIRE_NOTHROW(registry.resolve_and_init(ctx));
}

TEST_CASE("ScsiHardDiscExtension reopened at a copy serves and writes the copy",
          "[scsi][hdd][reopen]") {
    if (!adfs_available()) SKIP("SCSI disc image not available");
    beebium::test::ScopedTempDir dir("beebium_scsi_reopen_");
    for (const auto* name : {"original", "copy"}) {
        std::filesystem::copy_file(kAssetsDir / "scsi" / "scsi0.dat",
                                   dir / (std::string(name) + ".dat"));
        std::filesystem::copy_file(kAssetsDir / "scsi" / "scsi0.dsc",
                                   dir / (std::string(name) + ".dsc"));
    }

    ModelBPlus machine;
    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");
    auto adapter = AcornScsiHostAdapter::create();
    auto* adapter_ptr = adapter.get();
    registry.register_extension(with_default_id(std::move(adapter)));
    auto hdd_ext = ScsiHardDiscExtension::create();
    auto* hdd = hdd_ext.get();
    hdd_ext->set_config({{"scsi-id", "0"},
                         {"image", (dir / "original.dat").string()},
                         {"id", "boot"}});
    registry.register_extension(with_default_id(std::move(hdd_ext)));
    ExtensionContext ctx(&machine.memory().one_mhz_bus());
    registry.resolve_and_init(ctx);

    std::string error;
    REQUIRE(hdd->reopen("boot", dir / "copy.dat", error));
    CHECK(hdd->devices().at(0).backing_path == (dir / "copy.dat").string());

    auto* target = static_cast<ScsiHardDisc*>(adapter_ptr->target_registry().target(0));
    REQUIRE(target->image()->dat_filepath() == dir / "copy.dat");
    std::array<uint8_t, 256> sector{};
    sector.fill(0xA5);
    REQUIRE(target->image()->write_sector(sector.data(), 0));

    auto first_byte = [](const std::filesystem::path& filepath) {
        std::ifstream in(filepath, std::ios::binary);
        return static_cast<uint8_t>(in.get());
    };
    CHECK(first_byte(dir / "copy.dat") == 0xA5);
    CHECK(first_byte(dir / "original.dat") == first_byte(kAssetsDir / "scsi" / "scsi0.dat"));
}

TEST_CASE("ADFS SCSI boot and *CAT on Model B+", "[scsi][adfs][e2e]") {
    if (!roms_available()) SKIP("ROMs not available");
    if (!adfs_available()) SKIP("ADFS ROM or SCSI disc image not available");

    // Load ROMs
    auto mos = load_rom(kRomDir / "acorn-mos_2_0.rom");
    auto basic = load_rom(kRomDir / "bbc-basic_2.rom");
    auto adfs = load_rom(kAssetsDir / "roms" / "acorn-adfs_1_30.rom");
    REQUIRE(mos.size() == 16384);
    REQUIRE(basic.size() == 16384);
    REQUIRE(adfs.size() == 16384);

    // Set up Model B+ with MOS 2.0, BASIC, and ADFS 1.30
    ModelBPlus machine;
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_dfs(adfs.data(), adfs.size());  // ADFS goes in the DFS slot

    // Install SCSI adapter and hard disc as extensions (simulating plugin loading)
    ExtensionRegistry registry;
    registry.register_extension_point("1mhz-bus");

    // SCSI adapter: attaches to "1mhz-bus", provides "scsi"
    registry.register_extension(with_default_id(AcornScsiHostAdapter::create()));

    // Hard disc: attaches to "scsi" (dependency resolved by registry)
    // Configure via the config map, just as the CLI/preset path would
    auto hdd_ext = ScsiHardDiscExtension::create();
    hdd_ext->set_config({
        {"scsi-id", "0"},
        {"image", (kAssetsDir / "scsi" / "scsi0.dat").string()},
        {"id", "boot-disc"},
    });
    registry.register_extension(with_default_id(std::move(hdd_ext)));

    // Resolve dependencies and init: adapter first (provides "scsi"), then HDD
    ExtensionContext ctx(&machine.memory().one_mhz_bus());
    registry.resolve_and_init(ctx);

    // Enable video output and reset
    machine.memory().enable_video_output();
    machine.reset();

    // Set up frame rendering
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    // Boot to BASIC prompt (~4M cycles)
    INFO("Booting Model B+ with ADFS and SCSI hard disc...");
    run_cycles(machine, renderer, 4'000'000);

    // Check boot messages
    INFO("Screen after boot:");
    dump_screen(machine);

    REQUIRE(find_string_on_screen(machine, "Acorn ADFS"));
    REQUIRE(find_string_on_screen(machine, "BASIC"));

    // Type *CAT to catalogue the hard disc
    INFO("Typing *CAT...");
    beebium::test::type_string_with_shift(machine, renderer, "*CAT\r", 100000);

    // Wait for ADFS to read the disc catalogue via SCSI
    INFO("Waiting for SCSI disc access...");
    run_cycles(machine, renderer, 10'000'000);

    // Check the catalogue output
    INFO("Screen after *CAT:");
    dump_screen(machine);

    // The BeebEm scsi0.dat image should contain ADFS directory entries.
    // At minimum, ADFS *CAT should show the directory header.
    // The "$" root directory should be displayed.
    CHECK(find_string_on_screen(machine, "$"));

    registry.shutdown();
}
