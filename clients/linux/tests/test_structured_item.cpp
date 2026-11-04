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

// A structured list item (client specification 12.2): its manifest's
// item_format, brace-delimited placeholders between literal separators,
// converts between the object a preset holds, its field strings and its
// one-line display, as the macOS client converts them.

#include "preset/StructuredItem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

std::vector<SchemaField> peer_fields() {
    std::vector<SchemaField> fields(4);
    fields[0].key = "net";
    fields[0].type = "integer";
    fields[1].key = "station";
    fields[1].type = "integer";
    fields[2].key = "host";
    fields[2].type = "string";
    fields[3].key = "port";
    fields[3].type = "integer";
    return fields;
}

}  // namespace

TEST_CASE("An item format is its placeholders between literal separators", "[preset][item]") {
    const auto format = ItemFormat::parse("{net}.{station}@{host}@{port}");
    REQUIRE(format);
    CHECK(format->placeholders() == std::vector<std::string>{"net", "station", "host", "port"});
    CHECK_FALSE(ItemFormat::parse("{net").has_value());
    CHECK_FALSE(ItemFormat::parse("").has_value());
}

TEST_CASE("Field strings make the one-line display, and a line splits back into them",
          "[preset][item]") {
    const auto format = *ItemFormat::parse("{net}.{station}@{host}@{port}");
    const ItemStrings strings{{"net", "0"}, {"station", "254"}, {"host", "192.0.2.7"},
                              {"port", "32768"}};
    CHECK(format.line(strings) == "0.254@192.0.2.7@32768");
    const auto back = format.split("0.254@192.0.2.7@32768");
    REQUIRE(back);
    CHECK(*back == strings);
    // A host with dots is fine: each separator is taken in turn.
    const auto dotted = format.split("1.2@fileserver.example@32768");
    REQUIRE(dotted);
    CHECK(dotted->at("host") == "fileserver.example");
    CHECK_FALSE(format.split("no separators here").has_value());
    const auto subnet = *ItemFormat::parse("{net}@{address}");
    CHECK(subnet.split("3@192.0.2.0")->at("address") == "192.0.2.0");
}

TEST_CASE("An object is written typed, keeping a number that does not parse as written",
          "[preset][item]") {
    const auto fields = peer_fields();
    const ordered_json object = item_object(
        fields, {{"net", "0"}, {"station", "254"}, {"host", "192.0.2.7"}, {"port", "32x"}});
    CHECK(object["net"] == 0);
    CHECK(object["net"].is_number_integer());
    CHECK(object["station"] == 254);
    CHECK(object["host"] == "192.0.2.7");
    // For the resolver to flag, not for the client to drop.
    CHECK(object["port"] == "32x");
    // An empty field is left out.
    const ordered_json sparse = item_object(fields, {{"net", ""}, {"station", "1"}});
    CHECK_FALSE(sparse.contains("net"));
    CHECK(sparse["station"] == 1);
}

TEST_CASE("An object's fields read back as strings, in the fields' order", "[preset][item]") {
    const auto fields = peer_fields();
    const auto strings = item_strings(
        fields, ordered_json::parse(R"({"net": 0, "station": 254, "host": "h", "port": 32768})"));
    CHECK(strings.at("net") == "0");
    CHECK(strings.at("port") == "32768");
    CHECK(item_strings(fields, ordered_json::parse(R"({"station": true})")).at("station") == "true");
    CHECK(item_strings(fields, ordered_json::object()).at("host").empty());
}

TEST_CASE("Every item format in the real catalogue parses and round-trips", "[preset][item]") {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-model-b.json");
    std::stringstream text;
    text << file.rdbuf();
    const auto schema = PresetSchema::parse(text.str());
    REQUIRE(schema);
    int formats = 0;
    for (const auto& extension : schema->catalogue()) {
        for (const auto& parameter : extension.parameters) {
            if (parameter.item_format.empty()) {
                continue;
            }
            INFO(extension.name << " " << parameter.key << " " << parameter.item_format);
            const auto format = ItemFormat::parse(parameter.item_format);
            REQUIRE(format);
            // Every placeholder is a field of the item.
            ItemStrings strings;
            int n = 0;
            for (const auto& key : format->placeholders()) {
                const bool known = std::any_of(parameter.fields.begin(), parameter.fields.end(),
                                               [&](const SchemaField& f) { return f.key == key; });
                CHECK(known);
                strings[key] = std::to_string(++n);
            }
            CHECK(format->split(format->line(strings)) == strings);
            ++formats;
        }
    }
    CHECK(formats >= 2);
}
