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

#include <filesystem>
#include <fstream>
#include <sstream>

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

// The Master 128's schema with cartridges providing banks 0 to 3.
PresetSchema master_with_cartridges() {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-master-128.json");
    std::stringstream text;
    text << file.rdbuf();
    auto schema = PresetSchema::parse(text.str());
    REQUIRE(schema);
    return *schema;
}

const DraftSocket& at_slot(const std::vector<DraftSocket>& sockets, int slot) {
    for (const auto& s : sockets) {
        if (std::find(s.socket.slots.begin(), s.socket.slots.end(), slot) != s.socket.slots.end()) {
            return s;
        }
    }
    FAIL("No socket at slot " << slot);
    return sockets.front();
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
    CHECK(sideways.entry_index(2) == std::optional<std::size_t>(2));
    CHECK_FALSE(sideways.entry_index(3).has_value());
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

namespace {

// Two of a Master's MOS banks, each its own socket.
std::vector<SchemaSocket> master_mos_sockets() {
    std::vector<SchemaSocket> sockets;
    for (int slot : {14, 15}) {
        SchemaSocket socket;
        socket.label = "MOS ROM (bank " + std::to_string(slot) + ")";
        socket.slots = {slot};
        socket.capabilities = {"rom"};
        sockets.push_back(socket);
    }
    return sockets;
}

ordered_json master_banks(const char* bank_14, const char* bank_15, bool mark_15 = true) {
    ordered_json slots = ordered_json::array();
    slots.push_back({{"slot", 14}, {"type", "rom"}, {"image_uri", bank_14}, {"source", "mos"}});
    ordered_json entry{{"slot", 15}, {"type", "rom"}, {"image_uri", bank_15}};
    if (mark_15) {
        entry["source"] = "mos";
    }
    slots.push_back(entry);
    return ordered_json{{"model", "master-128"}, {"sideways_bank", {{"slots", slots}}}};
}

}  // namespace

TEST_CASE("A bank from the MOS is read as such and written back with its marker",
          "[preset][sideways][mos]") {
    MachineDraft draft(master_banks("acorn-dfs_2_24.rom", "acorn-terminal_1_20_mos_3_20.rom"));
    SidewaysDraft sideways(draft, master_mos_sockets());
    CHECK(sideways.content(0) == SocketContent{"rom", "acorn-terminal_1_20_mos_3_20.rom", false, true});
    // Written back as it was, marker and all.
    sideways.set(0, sideways.content(0));
    CHECK((*draft.get("/sideways_bank/slots"))[1] ==
          ordered_json::parse(R"({"slot": 15, "type": "rom", "image_uri": "acorn-terminal_1_20_mos_3_20.rom", "source": "mos"})"));
    CHECK_FALSE(sideways.changed(0));
    // The user's own image carries none.
    sideways.set(0, SocketContent{"rom", "/home/me/roms/mine.rom", false});
    CHECK_FALSE((*draft.get("/sideways_bank/slots"))[1].contains("source"));
    CHECK(sideways.changed(0));
}

TEST_CASE("A bank moved to another socket is the user's, not the MOS's", "[preset][sideways][mos]") {
    MachineDraft draft(master_banks("acorn-dfs_2_24.rom", "acorn-terminal_1_20_mos_3_20.rom"));
    SidewaysDraft sideways(draft, master_mos_sockets());
    sideways.swap(0, 1);
    CHECK(sideways.content(0) == SocketContent{"rom", "acorn-dfs_2_24.rom", false, false});
    CHECK(sideways.content(1) == SocketContent{"rom", "acorn-terminal_1_20_mos_3_20.rom", false, false});
}

TEST_CASE("The MOS's banks follow the server's resolution; the user's stay", "[preset][sideways][mos]") {
    MachineDraft draft(master_banks("acorn-dfs_2_24.rom", "/home/me/roms/mine.rom", false));
    const ordered_json answer = master_banks("acorn-dfs_2_45.rom", "acorn-terminal_1_20_mos_3_50.rom");
    CHECK(follow_mos_banks(draft, answer));
    const auto slots = *draft.get("/sideways_bank/slots");
    CHECK(slots[0] == ordered_json::parse(R"({"slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_45.rom", "source": "mos"})"));
    CHECK(slots[1] == ordered_json::parse(R"({"slot": 15, "type": "rom", "image_uri": "/home/me/roms/mine.rom"})"));
    // Nothing new: nothing changes.
    CHECK_FALSE(follow_mos_banks(draft, answer));
    // An answer without the bank leaves it.
    CHECK_FALSE(follow_mos_banks(draft, ordered_json{{"model", "master-128"}}));
    CHECK((*draft.get("/sideways_bank/slots"))[0]["image_uri"] == "acorn-dfs_2_45.rom");
}

TEST_CASE("A cartridge slot's banks have no socket until a cartridge is fitted there",
          "[preset][sideways][cartridge]") {
    const PresetSchema schema = master_with_cartridges();
    MachineDraft draft(ordered_json::parse(R"({"model": "master-128"})"));
    const auto sockets = draft_sockets(schema, draft);
    const DraftSocket& rear = at_slot(sockets, 0);
    CHECK(rear.unfitted());
    CHECK(rear.point_name == "Rear Cartridge Slot");
    CHECK(rear.provider.empty());
    CHECK(rear.socket.capabilities == std::vector<std::string>{"empty"});
    const DraftSocket& own = at_slot(sockets, 4);
    CHECK_FALSE(own.unfitted());
    CHECK(own.point_name.empty());
    CHECK(own.socket.can("ram"));
}

TEST_CASE("A fitted cartridge's banks take the capabilities its manifest gives them",
          "[preset][sideways][cartridge]") {
    const PresetSchema schema = master_with_cartridges();
    MachineDraft draft(ordered_json::parse(R"({"model": "master-128", "extensions": [
        {"name": "acorn-rom-cartridge", "config": {"at": "cartridge-front"}}]})"));
    const auto sockets = draft_sockets(schema, draft);
    for (int slot : {2, 3}) {
        INFO(slot);
        const DraftSocket& front = at_slot(sockets, slot);
        CHECK_FALSE(front.unfitted());
        CHECK(front.provider == "Acorn ROM Cartridge");
        CHECK(front.point_name == "Front Cartridge Slot");
        CHECK(front.socket.capabilities == std::vector<std::string>{"rom", "empty"});
    }
    CHECK(at_slot(sockets, 1).unfitted());  // the rear slot is still empty
}
