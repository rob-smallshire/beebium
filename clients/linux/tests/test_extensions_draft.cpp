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

// The draft's extensions as a tree of attachment points (client
// specification 5.2, 11): the model's own points, what is attached to each
// and beneath a provider the points it provides, the ids the server would
// assign, what can be added while there is room, and additions, removals
// and changes written as the macOS client writes them.

#include "preset/ExtensionsDraft.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

PresetSchema model_b_schema() {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-model-b.json");
    std::stringstream text;
    text << file.rdbuf();
    auto schema = PresetSchema::parse(text.str());
    REQUIRE(schema);
    return *schema;
}

MachineDraft with_extensions(const char* extensions) {
    auto document = ordered_json::parse(R"({"model": "model-b"})");
    document["extensions"] = ordered_json::parse(extensions);
    return MachineDraft(document);
}

SchemaAttachmentPoint point(const ExtensionsDraft& extensions, const std::string& id) {
    for (const auto& p : extensions.model_points()) {
        if (p.id == id) {
            return p;
        }
    }
    FAIL("no point " << id);
    return {};
}

std::vector<std::string> names(const std::vector<SchemaExtension>& extensions) {
    std::vector<std::string> out;
    for (const auto& e : extensions) {
        out.push_back(e.name);
    }
    return out;
}

}  // namespace

TEST_CASE("The model's own points are listed, but for those excluded", "[preset][extensions]") {
    MachineDraft draft = with_extensions("[]");
    const ExtensionsDraft extensions(draft, model_b_schema());
    std::vector<std::string> ids;
    for (const auto& p : extensions.model_points({"tube"})) {
        ids.push_back(p.id);
    }
    CHECK(ids == std::vector<std::string>{"1mhz-bus", "user-port", "printer-port", "serial-port"});
}

TEST_CASE("Instances take the ids the server assigns", "[preset][extensions]") {
    MachineDraft draft = with_extensions(R"([{"name": "acorn-scsi"},
        {"name": "acorn-scsi", "config": {"id": "acorn-scsi-1"}}, {"name": "acorn-scsi"}])");
    const ExtensionsDraft extensions(draft, model_b_schema());
    const auto all = extensions.instances();
    REQUIRE(all.size() == 3);
    CHECK(all[0].id == "acorn-scsi");
    CHECK(all[1].id == "acorn-scsi-1");
    CHECK(all[2].id == "acorn-scsi-2");  // never a given id
}

TEST_CASE("Adding writes the name alone, and a second instance its own id", "[preset][extensions]") {
    MachineDraft draft = with_extensions("[]");
    ExtensionsDraft extensions(draft, model_b_schema());
    const auto bus = point(extensions, "1mhz-bus");
    extensions.add("hybrid-music-5000", bus);
    extensions.add("hybrid-music-5000", bus);
    CHECK(*draft.get("/extensions") == ordered_json::parse(R"([{"name": "hybrid-music-5000"},
        {"name": "hybrid-music-5000", "config": {"id": "hybrid-music-5000-1"}}])"));
    CHECK(extensions.attached(bus).size() == 2);
}

TEST_CASE("A full point offers nothing to add; its instance can change", "[preset][extensions]") {
    MachineDraft draft = with_extensions(R"([{"name": "acorn-rtc", "config": {"layout": "x"}},
        {"name": "file-printer"}])");
    ExtensionsDraft extensions(draft, model_b_schema());
    const auto printer = point(extensions, "printer-port");
    CHECK(extensions.addable(printer).empty());
    const auto instance = extensions.attached(printer).at(0);
    const auto others = names(extensions.alternatives(printer, instance));
    CHECK(others.size() == 3);
    CHECK(std::find(others.begin(), others.end(), "file-printer") == others.end());
    extensions.change(instance, "tcp-printer");
    CHECK(*draft.get("/extensions") == ordered_json::parse(R"([{"name": "acorn-rtc", "config": {"layout": "x"}},
        {"name": "tcp-printer"}])"));
    extensions.remove(extensions.attached(point(extensions, "user-port")).at(0));
    CHECK(*draft.get("/extensions") == ordered_json::parse(R"([{"name": "tcp-printer"}])"));
    CHECK(names(extensions.addable(point(extensions, "user-port"))) ==
          std::vector<std::string>{"acorn-rtc"});
}

TEST_CASE("What can be added is in display-name order", "[preset][extensions]") {
    MachineDraft draft = with_extensions("[]");
    const ExtensionsDraft extensions(draft, model_b_schema());
    const auto offered = extensions.addable(point(extensions, "serial-port"));
    REQUIRE(offered.size() == 6);
    for (std::size_t i = 1; i < offered.size(); ++i) {
        std::string a = offered[i - 1].display_name, b = offered[i].display_name;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        CHECK(a <= b);
    }
}

TEST_CASE("Discs sit beneath the adapter they name, else the last", "[preset][extensions]") {
    MachineDraft draft = with_extensions(R"([
        {"name": "acorn-scsi", "config": {"id": "first"}},
        {"name": "acorn-scsi", "config": {"id": "second"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "adapter-id": "first"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0}}])");
    ExtensionsDraft extensions(draft, model_b_schema());
    const auto adapters = extensions.attached(point(extensions, "1mhz-bus"));
    REQUIRE(adapters.size() == 2);
    const auto provided = extensions.provided_points(adapters[0]);
    REQUIRE(provided.size() == 1);
    CHECK(provided[0].id == "scsi");
    CHECK(extensions.attached(provided[0], &adapters[0]).at(0).index == 2);
    CHECK(extensions.attached(provided[0], &adapters[1]).at(0).index == 3);
    // Beneath one of several adapters, a new disc names it.
    extensions.add("scsi-hard-disc", provided[0], &adapters[0]);
    CHECK((*draft.get("/extensions"))[4]["config"]["adapter-id"] == "first");
    CHECK(extensions.attached(provided[0], &adapters[0]).size() == 2);
}

TEST_CASE("An extension the catalogue lacks is named by its identifier", "[preset][extensions]") {
    MachineDraft draft = with_extensions("[]");
    const ExtensionsDraft extensions(draft, model_b_schema());
    CHECK(extensions.display_name("no-such-extension") == "no-such-extension");
    CHECK(extensions.display_name("acorn-rtc") != "acorn-rtc");
}
