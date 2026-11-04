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

// What a preset for a model may hold (client specification 5.2, 12.2), read
// from describe-preset-schema as the server wrote it.

#include "preset/PresetSchema.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;

namespace {

PresetSchema schema_from(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    auto schema = PresetSchema::parse(text.str());
    REQUIRE(schema);
    return *schema;
}

PresetSchema model_b() {
    return schema_from(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-model-b.json");
}

}  // namespace

TEST_CASE("The schema names its model and its sections, in the server's words",
          "[preset][schema]") {
    const PresetSchema schema = model_b();
    CHECK(schema.model_id() == "model-b");
    CHECK(schema.model_name() == "BBC Model B");
    CHECK(schema.section("storage") != nullptr);
    CHECK(schema.section("nonsense") == nullptr);
    CHECK(schema.section_label("sideways_bank") == "Sideways ROM / RAM");
    CHECK(schema.section_label("startup") == "Keyboard links");
}

TEST_CASE("The disc controller options and the station bounds are the server's",
          "[preset][schema]") {
    const PresetSchema schema = model_b();
    const auto options = schema.fdc_options();
    REQUIRE(options.size() == 3);
    CHECK(options[0].id == "none");
    CHECK(options[0].label == "Empty");
    CHECK(options[1].id == "acorn-1770");
    const auto station = schema.station();
    REQUIRE(station);
    CHECK(station->min == 1u);
    CHECK(station->max == 254u);
    CHECK(station->automatic);
    CHECK(station->auto_high == 253u);
}

TEST_CASE("Attachment points keep the server's order, bounds and providers", "[preset][schema]") {
    const auto points = model_b().attachment_points();
    REQUIRE(points.size() >= 6);
    CHECK(points[0].id == "1mhz-bus");
    CHECK_FALSE(points[0].max_occupancy.has_value());  // unbounded
    CHECK_FALSE(points[0].provided_by.has_value());    // the model's own
    const auto scsi = std::find_if(points.begin(), points.end(),
                                   [](const auto& p) { return p.id == "scsi"; });
    REQUIRE(scsi != points.end());
    REQUIRE(scsi->provided_by.has_value());
    CHECK(*scsi->provided_by == std::vector<std::string>{"acorn-scsi"});
    CHECK(scsi->max_occupancy == 7);
}

TEST_CASE("The catalogue gives each extension's parameters, typed", "[preset][schema]") {
    const PresetSchema schema = model_b();
    CHECK(schema.catalogue().size() >= 10);
    const auto disc = schema.extension("scsi-hard-disc");
    REQUIRE(disc);
    CHECK(disc->display_name == "SCSI Hard Disc");
    CHECK(disc->attaches_to == std::vector<std::string>{"scsi"});
    REQUIRE(disc->parameters.size() >= 2);
    CHECK(disc->parameters[0].key == "scsi-id");
    CHECK(disc->parameters[0].type == "integer");
    CHECK(disc->parameters[0].min == 0);
    CHECK(disc->parameters[0].max == 7);
    CHECK(disc->parameters[0].unique);
    CHECK(disc->parameters[1].type == "filepath");
    CHECK(disc->parameters[1].access == "read");
    CHECK(disc->parameters[1].required);

    const auto aun = schema.extension("aun");
    REQUIRE(aun);
    const auto map = std::find_if(aun->parameters.begin(), aun->parameters.end(),
                                  [](const auto& p) { return p.key == "map"; });
    REQUIRE(map != aun->parameters.end());
    CHECK(map->is_list);
    CHECK(map->item_format == "{net}.{station}@{host}@{port}");
    REQUIRE(map->fields.size() >= 4);
    CHECK(map->fields[0].key == "net");
    CHECK(map->fields[0].max == 255);
    CHECK_FALSE(schema.extension("nonesuch").has_value());
}

TEST_CASE("The repository's recorded schemas all read", "[preset][schema]") {
    for (const char* name : {"model-b.json", "model-b-plus.json", "model-b-integra-b.json"}) {
        INFO(name);
        const PresetSchema schema =
            schema_from(std::filesystem::path(BEEBIUM_PRESET_SCHEMA_FIXTURES_DIRPATH) / name);
        CHECK_FALSE(schema.model_id().empty());
        CHECK(schema.section("speed") != nullptr);
    }
    CHECK_FALSE(PresetSchema::parse("{}").has_value());
    CHECK_FALSE(PresetSchema::parse("not json").has_value());
}
