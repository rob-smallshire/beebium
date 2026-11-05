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

// The draft's sideways slots by socket (client specification 5.2, 11): a
// socket addressed at any of its slots, highest priority first, an edited
// socket written explicitly, contents moved between sockets, and each
// socket's change judged against the preset as resolved.

#include "preset/SidewaysDraft.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

// A Model B's four sockets, as its schema gives them.
std::vector<SchemaSocket> model_b_sockets() {
    std::vector<SchemaSocket> sockets;
    const char* labels[] = {"IC52", "IC88", "IC100", "IC101"};
    for (int i = 0; i < 4; ++i) {
        SchemaSocket socket;
        socket.label = labels[i];
        socket.slots = {i, i + 4, i + 8, i + 12};
        socket.capabilities = {"rom", "ram", "empty"};
        sockets.push_back(socket);
    }
    return sockets;
}

MachineDraft resolved() {
    return MachineDraft(ordered_json::parse(R"({"model": "model-b", "sideways_bank": {"slots": [
        {"slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom"},
        {"slot": 15, "type": "rom", "image_uri": "bbc-basic_2.rom"}]}})"));
}

}  // namespace

TEST_CASE("Sockets come highest priority first, each read at any of its slots",
          "[preset][sideways]") {
    MachineDraft draft = resolved();
    const SidewaysDraft sideways(draft, model_b_sockets());
    REQUIRE(sideways.sockets().size() == 4);
    CHECK(sideways.sockets()[0].label == "IC101");
    CHECK(sideways.sockets()[3].label == "IC52");
    CHECK(sideways.content(0) == SocketContent{"rom", "bbc-basic_2.rom", false});
    CHECK(sideways.content(1) == SocketContent{"rom", "acorn-dfs_2_26.rom", false});
    CHECK(sideways.content(2).type == "empty");
    CHECK_FALSE(sideways.changed(0));
}

TEST_CASE("A socket's new content is written explicitly at its slot", "[preset][sideways]") {
    MachineDraft draft = resolved();
    SidewaysDraft sideways(draft, model_b_sockets());
    sideways.set(2, SocketContent{"ram", "", true});  // IC88, slots 1, 5, 9, 13
    const auto slots = *draft.get("/sideways_bank/slots");
    REQUIRE(slots.size() == 3);
    CHECK(slots[2] == ordered_json::parse(R"({"slot": 13, "type": "ram", "image_uri": null, "write_protected": true})"));
    CHECK(sideways.changed(2));
    // Emptied: still written, so it means empty whatever the defaults.
    sideways.set(0, SocketContent{});
    CHECK((*draft.get("/sideways_bank/slots"))[1] == ordered_json::parse(R"({"slot": 15, "type": "empty"})"));
    sideways.revert(0);
    CHECK(sideways.content(0).image_uri == "bbc-basic_2.rom");
    CHECK_FALSE(sideways.changed(0));
}

TEST_CASE("Contents move between sockets, each keeping its slots", "[preset][sideways]") {
    MachineDraft draft = resolved();
    SidewaysDraft sideways(draft, model_b_sockets());
    sideways.swap(0, 1);  // BASIC down, DFS up
    CHECK(sideways.content(0).image_uri == "acorn-dfs_2_26.rom");
    CHECK(sideways.content(1).image_uri == "bbc-basic_2.rom");
    const auto slots = *draft.get("/sideways_bank/slots");
    // DFS now answers at slot 15, BASIC at 14.
    bool dfs_at_15 = false;
    for (const auto& entry : slots) {
        dfs_at_15 = dfs_at_15 || (entry["slot"] == 15 && entry["image_uri"] == "acorn-dfs_2_26.rom");
    }
    CHECK(dfs_at_15);
    CHECK(sideways.changed(0));
    CHECK(sideways.changed(1));
}

TEST_CASE("The schema gives the sockets and their aliasing", "[preset][sideways]") {
    const auto schema = PresetSchema::parse(R"({"schema_version": 2, "model": {"id": "model-b"},
        "sections": [{"type": "sideways_bank", "has_aliasing": true, "sockets": [
            {"label": "IC52", "slots": [0, 4, 8, 12], "capabilities": ["rom", "ram", "empty"]},
            {"label": "Slot 15", "slots": [15], "capabilities": ["rom", "ram", "write_protect"]}]}]})");
    REQUIRE(schema);
    CHECK(schema->has_aliasing());
    const auto sockets = schema->sockets();
    REQUIRE(sockets.size() == 2);
    CHECK(sockets[0].priority() == 12);
    CHECK(sockets[1].can("write_protect"));
    CHECK_FALSE(sockets[0].can("write_protect"));
}
