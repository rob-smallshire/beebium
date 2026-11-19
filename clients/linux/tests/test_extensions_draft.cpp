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

PresetSchema schema_of(const char* filename) {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) / filename);
    std::stringstream text;
    text << file.rdbuf();
    auto schema = PresetSchema::parse(text.str());
    REQUIRE(schema);
    return *schema;
}

PresetSchema model_b_schema() {
    return schema_of("describe-preset-schema-model-b.json");
}

PresetSchema master_128_schema() {
    return schema_of("describe-preset-schema-master-128.json");
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
    CHECK(ids == std::vector<std::string>{"1mhz-bus", "user-port", "printer-port", "analogue-port",
                                          "serial-port"});
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
          std::vector<std::string>{"acorn-rtc", "voltmace-delta-14b"});
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

TEST_CASE("A disc naming no adapter is unattached, never given to another", "[preset][extensions]") {
    MachineDraft draft = with_extensions(R"([
        {"name": "acorn-scsi", "config": {"id": "first"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "adapter-id": "gone"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 1}},
        {"name": "no-such-extension"}])");
    const ExtensionsDraft extensions(draft, model_b_schema());
    const auto adapter = extensions.attached(point(extensions, "1mhz-bus")).at(0);
    const auto scsi = extensions.provided_points(adapter).at(0);
    const auto discs = extensions.attached(scsi, &adapter);
    REQUIRE(discs.size() == 1);
    CHECK(discs[0].index == 2);
    const auto orphans = extensions.unattached();
    REQUIRE(orphans.size() == 2);
    CHECK(orphans[0].index == 1);
    CHECK(orphans[1].name == "no-such-extension");
}

TEST_CASE("With no adapter at all, a disc is unattached", "[preset][extensions]") {
    MachineDraft draft = with_extensions(R"([{"name": "scsi-hard-disc", "config": {"scsi-id": 0}}])");
    const ExtensionsDraft extensions(draft, model_b_schema());
    REQUIRE(extensions.unattached().size() == 1);
}

TEST_CASE("A coprocessor is offered on either Tube of a Master 128", "[preset][extensions][at]") {
    MachineDraft draft = with_extensions("[]");
    const ExtensionsDraft extensions(draft, master_128_schema());
    const std::vector<std::string> coprocessors{"acorn-65c02-coprocessor",
                                                "acorn-65c102-coprocessor"};
    auto offered = names(extensions.addable(point(extensions, "tube")));
    std::sort(offered.begin(), offered.end());
    CHECK(offered == coprocessors);
    offered = names(extensions.addable(point(extensions, "internal-tube")));
    std::sort(offered.begin(), offered.end());
    CHECK(offered == coprocessors);
}

TEST_CASE("A coprocessor fitted from the internal Tube is placed there with at", "[preset][extensions][at]") {
    MachineDraft draft = with_extensions("[]");
    ExtensionsDraft extensions(draft, master_128_schema());
    const auto internal = point(extensions, "internal-tube");
    const auto external = point(extensions, "tube");
    extensions.add("acorn-65c102-coprocessor", internal);
    CHECK(*draft.get("/extensions") == ordered_json::parse(
              R"([{"name": "acorn-65c102-coprocessor", "config": {"at": "internal-tube"}}])"));
    CHECK(extensions.attached(internal).size() == 1);
    CHECK(extensions.attached(external).empty());
    // The internal Tube is full; the external one still offers.
    CHECK(extensions.addable(internal).empty());
    CHECK_FALSE(extensions.addable(external).empty());
    CHECK(extensions.unattached().empty());

    // From the external Tube, the first of its alternatives, it needs no at.
    MachineDraft plain = with_extensions("[]");
    ExtensionsDraft on_tube(plain, master_128_schema());
    on_tube.add("acorn-65c02-coprocessor", point(on_tube, "tube"));
    CHECK(*plain.get("/extensions") ==
          ordered_json::parse(R"([{"name": "acorn-65c02-coprocessor"}])"));
}

TEST_CASE("A coprocessor naming no at sits on the first of its Tubes, as the server places it",
          "[preset][extensions][at]") {
    MachineDraft draft = with_extensions(R"([{"name": "acorn-65c02-coprocessor"}])");
    const ExtensionsDraft extensions(draft, master_128_schema());
    CHECK(extensions.attached(point(extensions, "tube")).size() == 1);
    CHECK(extensions.attached(point(extensions, "internal-tube")).empty());
    // And on a Model B, as it always has.
    MachineDraft model_b = with_extensions(R"([{"name": "acorn-65c02-coprocessor"}])");
    const ExtensionsDraft on_b(model_b, model_b_schema());
    CHECK(on_b.attached(point(on_b, "tube")).size() == 1);
}

TEST_CASE("A coprocessor changed for another stays on its Tube", "[preset][extensions][at]") {
    MachineDraft draft = with_extensions(
        R"([{"name": "acorn-65c02-coprocessor", "config": {"at": "internal-tube", "id": "cp"}}])");
    ExtensionsDraft extensions(draft, master_128_schema());
    const auto internal = point(extensions, "internal-tube");
    const auto instance = extensions.attached(internal).at(0);
    CHECK(names(extensions.alternatives(internal, instance)) ==
          std::vector<std::string>{"acorn-65c102-coprocessor"});
    extensions.change(instance, "acorn-65c102-coprocessor");
    CHECK(*draft.get("/extensions") == ordered_json::parse(
              R"([{"name": "acorn-65c102-coprocessor", "config": {"at": "internal-tube"}}])"));
    CHECK(extensions.attached(internal).size() == 1);
}

TEST_CASE("A cartridge goes in either Master 128 slot, each holding one", "[preset][extensions][at]") {
    MachineDraft draft = with_extensions("[]");
    ExtensionsDraft extensions(draft, master_128_schema());
    const auto rear = point(extensions, "cartridge-rear");
    const auto front = point(extensions, "cartridge-front");
    CHECK(names(extensions.addable(rear)) == std::vector<std::string>{"rom-cartridge"});
    CHECK(names(extensions.addable(front)) == std::vector<std::string>{"rom-cartridge"});
    extensions.add("rom-cartridge", front);
    extensions.add("rom-cartridge", rear);
    CHECK(*draft.get("/extensions") == ordered_json::parse(R"([
        {"name": "rom-cartridge", "config": {"at": "cartridge-front"}},
        {"name": "rom-cartridge", "config": {"id": "rom-cartridge-1"}}])"));
    // Occupancy is the point's own: one in each, and each full.
    CHECK(extensions.attached(rear).size() == 1);
    CHECK(extensions.attached(front).size() == 1);
    CHECK(extensions.attached(front).at(0).id == "rom-cartridge");
    CHECK(extensions.addable(rear).empty());
    CHECK(extensions.addable(front).empty());
    // A point nothing fits is not offered.
    for (const auto& p : extensions.model_points()) {
        CHECK(p.id != "modem-connector");
    }
}
