// Copyright (c) 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Regression test for issue #42: SIGINT (Ctrl-C) of beebium-model-b-romram with
// a mounted SCSI hard disc image causes a Segmentation Fault.
//
// Root cause: scsi-hard-disc.dylib's init() calls
// AcornScsiHostAdapter::register_target_indicator (an inline header method)
// which constructs a RetriggerableMonostableFilter via std::make_unique. With
// all-inline virtual methods on the filter classes, the vtable is emitted
// weakly into whichever shared object instantiates them -- here, into
// scsi-hard-disc.dylib. The unique_ptr<IndicatorFilter> is then handed to
// the long-lived Indicators registry owned by Machine::state().memory.
//
// At shutdown the local-variable destruction order in StartSubcommand::invoke()
// is (reverse of construction):
//   1. extension_registry  -- destroys extension instances
//   2. plugin_loader       -- dlclose's every plugin shared object
//   3. machine             -- destroys Indicators, which walks its
//                              vector<IndicatorState> and destroys each
//                              unique_ptr<IndicatorFilter>; the virtual
//                              destructor lookup goes through the now-unmapped
//                              scsi-hard-disc.dylib vtable -> SIGSEGV.
//
// The test deliberately recreates that destruction ordering using PluginLoader
// directly so the failure can be observed without having to spawn the server.

#include <catch2/catch_test_macros.hpp>
#include "test_temp_paths.hpp"

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/PeripheralExtension.hpp>
#include <beebium/extension/PluginLoader.hpp>
#include <beebium/indicators/Indicators.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// Path to the directory containing the deployed plugins (acorn-scsi and
// scsi-hard-disc as sibling subdirectories), supplied via CMake compile
// definition. This matches the layout produced by beebium_finalize_plugin().
#ifdef BEEBIUM_PLUGIN_EXTENSIONS_DIR
const std::filesystem::path kPluginExtensionsDirpath = BEEBIUM_PLUGIN_EXTENSIONS_DIR;
#else
const std::filesystem::path kPluginExtensionsDirpath;
#endif

// Create a small zero-filled DAT file. ScsiHardDiscExtension::init() opens the
// file (for HardDiskImage::open) but does not perform any I/O until SCSI
// commands are issued, so a few KB is enough to satisfy initialisation.
std::filesystem::path make_temp_dat_image() {
    auto path = beebium::test::unique_temp_path("beebium_scsi_plugin_shutdown_test_", ".dat");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    std::vector<char> zeros(4096, 0);
    file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    return path;
}

void remove_image_pair(const std::filesystem::path& dat_filepath) {
    std::error_code ec;
    std::filesystem::remove(dat_filepath, ec);
    auto dsc_filepath = dat_filepath;
    dsc_filepath.replace_extension(".dsc");
    std::filesystem::remove(dsc_filepath, ec);
}

}  // namespace

TEST_CASE("SCSI plugin dlclose followed by Indicators destruction does not crash",
          "[scsi][hdd][indicators][shutdown][regression]") {
    if (kPluginExtensionsDirpath.empty()) {
        SKIP("BEEBIUM_PLUGIN_EXTENSIONS_DIR not set");
    }

    auto dat_filepath = make_temp_dat_image();

    // Indicators and bus port live in the OUTER scope, deliberately outliving
    // the PluginLoader. This mirrors the production lifetime: Machine (which
    // owns Indicators) is destroyed AFTER PluginLoader in
    // StartSubcommand::invoke().
    beebium::Indicators indicators;
    beebium::OneMHzBusPort bus_port;

    {
        // INNER scope: PluginLoader is destructed before Indicators, calling
        // dlclose() on every plugin it loaded.
        beebium::PluginLoader loader;
        auto manifests = loader.scan_manifests(kPluginExtensionsDirpath);

        const auto* acorn_scsi_manifest =
            beebium::PluginLoader::find_manifest(manifests, "acorn-scsi");
        REQUIRE(acorn_scsi_manifest != nullptr);

        const auto* hard_disc_manifest =
            beebium::PluginLoader::find_manifest(manifests, "scsi-hard-disc");
        REQUIRE(hard_disc_manifest != nullptr);

        beebium::ExtensionRegistry registry;
        registry.register_extension_point("1mhz-bus");

        // Provider first (acorn-scsi provides the "scsi" extension point);
        // consumer (scsi-hard-disc) attaches to "scsi" and is initialised
        // afterwards by Kahn's algorithm in resolve_and_init().
        auto adapter_loaded = loader.load_extension(*acorn_scsi_manifest);
        auto* adapter_peripheral =
            dynamic_cast<beebium::PeripheralExtension*>(adapter_loaded.get());
        REQUIRE(adapter_peripheral != nullptr);
        (void)adapter_loaded.release();
        registry.register_extension(
            std::unique_ptr<beebium::PeripheralExtension>(adapter_peripheral));

        std::map<std::string, std::string> hdd_config{
            {"scsi-id", "0"},
            {"image", dat_filepath.string()},
        };
        auto hdd_loaded =
            loader.load_extension(*hard_disc_manifest, std::move(hdd_config));
        auto* hdd_peripheral =
            dynamic_cast<beebium::PeripheralExtension*>(hdd_loaded.get());
        REQUIRE(hdd_peripheral != nullptr);
        (void)hdd_loaded.release();
        registry.register_extension(
            std::unique_ptr<beebium::PeripheralExtension>(hdd_peripheral));

        beebium::ExtensionContext ctx(&bus_port, nullptr, nullptr, &indicators);
        registry.resolve_and_init(ctx);

        // Confirm that the per-LUN HDD activity indicator was registered with
        // its filter -- if scsi-hard-disc.dylib is the source of the filter
        // vtable, this is the object whose destruction will crash later.
        auto names = indicators.names();
        REQUIRE(std::find(names.begin(), names.end(), "hdd-0-activity-led")
                != names.end());

        // Match the production shutdown sequence. After this returns, the
        // plugin instances still exist as objects inside `registry`.
        registry.shutdown();
    }
    // Inner scope ends:
    //   - registry destructor destroys both extension instances (their
    //     destructor code still lives in the loaded plugins).
    //   - plugin_loader destructor calls dlclose() on acorn-scsi.dylib and
    //     scsi-hard-disc.dylib, unmapping their .text/.rodata/vtables.
    //
    // Outer scope now ends, running ~Indicators(). That walks its
    // vector<IndicatorState> destroying each unique_ptr<IndicatorFilter>,
    // which calls the filter's virtual destructor. Without the fix, the
    // vtable has been unmapped and we crash with SIGSEGV.
    //
    // The test "passes" by simply not crashing while leaving this scope.

    remove_image_pair(dat_filepath);
}
