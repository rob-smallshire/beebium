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

// The draft's hard discs as one list (client specification 5.2): the
// capability read from the schema, a disc added with the lowest free ID
// and an adapter fitted with it, the adapter taken out with the last disc
// when it is the only one, room and duplicate IDs counted per adapter.

#include "preset/HardDiscsDraft.hpp"

#include <catch2/catch_test_macros.hpp>

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

HardDiscsDraft discs_of(MachineDraft& draft) {
    const auto schema = model_b_schema();
    const auto capability = HardDiscCapability::from_schema(schema);
    REQUIRE(capability);
    return HardDiscsDraft(draft, schema, *capability);
}

}  // namespace

TEST_CASE("The schema describes SCSI discs on an adapter on the 1 MHz bus", "[preset][hard-discs]") {
    const auto capability = HardDiscCapability::from_schema(model_b_schema());
    REQUIRE(capability);
    CHECK(capability->disc == "scsi-hard-disc");
    CHECK(capability->point == "scsi");
    CHECK(capability->adapter == "acorn-scsi");
    CHECK(capability->id_key == "scsi-id");
    CHECK(capability->id_max == 7);
    CHECK(capability->image_key == "image");
    CHECK(capability->adapter_key == "adapter-id");
    CHECK(capability->per_adapter == 7);
}

TEST_CASE("The first disc fits an adapter, and the last takes it out", "[preset][hard-discs]") {
    MachineDraft draft = with_extensions(R"([{"name": "acorn-rtc"}])");
    HardDiscsDraft discs = discs_of(draft);
    discs.add();
    discs.add();
    CHECK(*draft.get("/extensions") == ordered_json::parse(R"([{"name": "acorn-rtc"}, {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 1}}])"));
    discs.set_image(discs.discs()[0], "/home/me/discs/scsi0.dat");
    CHECK(discs.discs()[0].image == "/home/me/discs/scsi0.dat");
    discs.set_image(discs.discs()[0], "");
    CHECK((*draft.get("/extensions"))[2] == ordered_json::parse(R"({"name": "scsi-hard-disc", "config": {"scsi-id": 0}})"));
    discs.remove(discs.discs()[0]);
    CHECK(draft.get("/extensions")->size() == 3);
    discs.remove(discs.discs()[0]);
    CHECK(*draft.get("/extensions") == ordered_json::parse(R"([{"name": "acorn-rtc"}])"));
}

TEST_CASE("With several adapters a disc names its own, and nothing cascades", "[preset][hard-discs]") {
    MachineDraft draft = with_extensions(R"([{"name": "acorn-scsi", "config": {"id": "a"}},
        {"name": "acorn-scsi", "config": {"id": "b"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0}}])");
    HardDiscsDraft discs = discs_of(draft);
    CHECK(discs.adapters() == std::vector<std::string>{"a", "b"});
    discs.add("a");
    const auto added = discs.discs().back();
    CHECK(added.adapter == "a");
    CHECK(added.id == 0);  // free on a
    CHECK_FALSE(discs.duplicate_id(added));
    discs.set_adapter(added, "");  // the last: b, where 0 is taken
    CHECK(discs.duplicate_id(discs.discs().back()));
    discs.remove(discs.discs()[0]);
    discs.remove(discs.discs()[0]);
    CHECK(discs.adapters().size() == 2);
}

TEST_CASE("Room is counted per adapter", "[preset][hard-discs]") {
    MachineDraft draft = with_extensions(R"([{"name": "acorn-scsi"}])");
    HardDiscsDraft discs = discs_of(draft);
    for (int i = 0; i < 7; ++i) {
        REQUIRE(discs.can_add());
        discs.add();
    }
    CHECK_FALSE(discs.can_add());
    CHECK(discs.discs().back().id == 6);
}
