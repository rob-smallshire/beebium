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

#include <beebium/extension/AttachmentPointCatalogue.hpp>
#include <beebium/extension/PluginLoader.hpp>
#include "test_temp_paths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using namespace beebium;

TEST_CASE("attachment-point catalogue gives display names and occupancy",
          "[extension][attachment-points]") {
    CHECK(attachment_point_display_name("serial-port") == "Serial Port");
    CHECK(attachment_point_display_name("user-port") == "User Port");
    CHECK(attachment_point_display_name("1mhz-bus") == "1 MHz Bus");
    CHECK(attachment_point_display_name("tube") == "Tube");
    CHECK(attachment_point_display_name("scsi") == "SCSI Bus");

    // Occupancy is an integer range [min, max]; max is nullopt when unbounded.
    // Connectors hold at most one; a bus holds several.
    CHECK(find_attachment_point("serial-port")->min_occupancy == 0);
    CHECK(find_attachment_point("serial-port")->max_occupancy == 1);
    CHECK(find_attachment_point("tube")->max_occupancy == 1);
    CHECK(find_attachment_point("1mhz-bus")->max_occupancy == std::nullopt);  // unbounded
    CHECK(find_attachment_point("scsi")->max_occupancy == 7);

    // Human-readable labels.
    CHECK(occupancy_label(*find_attachment_point("serial-port")) == "0..1");
    CHECK(occupancy_label(*find_attachment_point("1mhz-bus")) == "0..N");
}

TEST_CASE("attachment-point display name falls back to the id",
          "[extension][attachment-points]") {
    // An id not (yet) in the catalogue returns itself, so callers always have
    // something to show (e.g. a future "analogue-port").
    CHECK(find_attachment_point("analogue-port") == nullptr);
    CHECK(attachment_point_display_name("analogue-port") == "analogue-port");
}

TEST_CASE("manifest parser reads the attaches_to array", "[extension][manifest]") {
    namespace fs = std::filesystem;
    const fs::path root = beebium::test::unique_temp_path("beebium_attaches_to_test_");
    const fs::path ext_dir = root / "voltmace-delta";
    fs::create_directories(ext_dir);
    {
        std::ofstream f(ext_dir / "manifest.json");
        // A (hypothetical) multi-point extension: spans two attachment points.
        f << R"({
            "name": "voltmace-delta",
            "library": "voltmace-delta",
            "attaches_to": ["analogue-port", "user-port"]
        })";
    }

    PluginLoader loader;
    auto manifests = loader.scan_manifests(root);
    fs::remove_all(root);

    const ExtensionManifest* m = PluginLoader::find_manifest(manifests, "voltmace-delta");
    REQUIRE(m != nullptr);
    REQUIRE(m->attaches_to.size() == 2);
    CHECK(std::find(m->attaches_to.begin(), m->attaches_to.end(), "analogue-port")
          != m->attaches_to.end());
    CHECK(std::find(m->attaches_to.begin(), m->attaches_to.end(), "user-port")
          != m->attaches_to.end());
}

TEST_CASE("manifest with no attaches_to parses to an empty list",
          "[extension][manifest]") {
    namespace fs = std::filesystem;
    const fs::path root = beebium::test::unique_temp_path("beebium_attaches_to_empty_test_");
    const fs::path ext_dir = root / "plain";
    fs::create_directories(ext_dir);
    {
        std::ofstream f(ext_dir / "manifest.json");
        f << R"({"name": "plain", "library": "plain"})";
    }

    PluginLoader loader;
    auto manifests = loader.scan_manifests(root);
    fs::remove_all(root);

    const ExtensionManifest* m = PluginLoader::find_manifest(manifests, "plain");
    REQUIRE(m != nullptr);
    CHECK(m->attaches_to.empty());
}
