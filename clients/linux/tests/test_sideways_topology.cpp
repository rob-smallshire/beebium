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

// The sideways topology as the server reports it (client specification 11):
// sockets, not slots, each with its aliased slots, highest boot priority
// first, and the protection groups by id and kind.

#include "connection/SidewaysTopology.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace beebium::linux_client;

namespace {

// A Model B: four sockets, each answering four slots; BASIC in IC101.
beebium::GetSlotStatusResponse model_b() {
    beebium::GetSlotStatusResponse response;
    response.set_has_aliasing(true);
    response.set_num_physical_slots(4);
    const char* labels[] = {"IC52", "IC88", "IC100", "IC101"};
    for (std::uint32_t socket = 0; socket < 4; ++socket) {
        auto* status = response.add_sockets();
        status->set_socket_index(socket);
        status->set_socket_label(labels[socket]);
        for (std::uint32_t slot = socket; slot < 16; slot += 4) {
            status->add_aliased_slots(slot);
        }
        status->set_type(beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
    }
    auto* basic = response.mutable_sockets(3);
    basic->set_type(beebium::SIDEWAYS_SLOT_TYPE_ROM);
    basic->set_populated(true);
    basic->set_image_name("/roms/bbc-basic_2.rom");
    basic->mutable_rom_header()->set_recognised(true);
    basic->mutable_rom_header()->set_title("BASIC");
    basic->mutable_rom_header()->set_copyright("(C)1982 Acorn");
    basic->mutable_rom_header()->add_kinds("language");
    return response;
}

// A board of sixteen independent slots, slot 15 RAM with a one-slot write
// switch, and a whole-board write switch over all of them.
beebium::GetSlotStatusResponse board() {
    beebium::GetSlotStatusResponse response;
    response.set_num_physical_slots(16);
    for (std::uint32_t slot = 0; slot < 16; ++slot) {
        auto* status = response.add_sockets();
        status->set_socket_index(slot);
        status->add_aliased_slots(slot);
        status->set_type(slot == 15 ? beebium::SIDEWAYS_SLOT_TYPE_RAM
                                    : beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
    }
    auto* one = response.add_protection_groups();
    one->set_id("slot-15");
    one->set_label("Write-protect slot 15");
    one->add_slots(15);
    one->set_supports_write_protect(true);
    one->set_write_protected(true);
    auto* whole = response.add_protection_groups();
    whole->set_id("board");
    whole->set_label("Write-protect (S2)");
    for (std::uint32_t slot = 0; slot < 16; ++slot) {
        whole->add_slots(slot);
    }
    whole->set_supports_write_protect(true);
    auto* hide = response.add_protection_groups();
    hide->set_id("socket-14");
    hide->set_label("Hide (S1)");
    hide->add_slots(14);
    hide->set_supports_hide(true);
    hide->set_hidden(true);
    return response;
}

}  // namespace

TEST_CASE("A Model B is four sockets, each with its aliased slots, highest priority first",
          "[sideways]") {
    const SidewaysTopology topology = sideways_topology(model_b());
    CHECK(topology.has_aliasing);
    REQUIRE(topology.sockets.size() == 4);
    // The MOS looks from slot 15 down, so the socket answering 15 leads.
    CHECK(topology.sockets[0].label == "IC101");
    CHECK(topology.sockets[0].slots == std::vector<std::uint32_t>{3, 7, 11, 15});
    CHECK(topology.sockets[1].label == "IC100");
    CHECK(topology.sockets[3].label == "IC52");
    const SidewaysSocket& basic = topology.sockets[0];
    CHECK(basic.type == SidewaysSlotType::rom);
    CHECK(basic.populated);
    CHECK(basic.image_filepath == "/roms/bbc-basic_2.rom");
    CHECK(basic.rom.recognised);
    CHECK(basic.rom.title == "BASIC");
    CHECK(basic.rom.copyright == "(C)1982 Acorn");
    CHECK(basic.rom.kinds == std::vector<std::string>{"language"});
    CHECK(topology.sockets[1].type == SidewaysSlotType::empty);
    CHECK_FALSE(topology.sockets[1].rom.recognised);
}

TEST_CASE("Protection groups keep their id, kinds and state, and are found by slot",
          "[sideways]") {
    const SidewaysTopology topology = sideways_topology(board());
    REQUIRE(topology.sockets.size() == 16);
    CHECK(topology.sockets[0].index == 15);
    CHECK(topology.sockets[0].type == SidewaysSlotType::ram);
    REQUIRE(topology.groups.size() == 3);
    CHECK(topology.groups[0].id == "slot-15");
    CHECK(topology.groups[0].supports_write_protect);
    CHECK(topology.groups[0].write_protected);
    CHECK_FALSE(topology.groups[0].supports_hide);
    CHECK(topology.groups[1].slots.size() == 16);
    CHECK(topology.groups[2].supports_hide);
    CHECK(topology.groups[2].hidden);

    // Slot 15 is governed by its own switch and the board's.
    const auto covering = topology.groups_covering(topology.sockets[0]);
    REQUIRE(covering.size() == 2);
    CHECK(covering[0]->id == "slot-15");
    CHECK(covering[1]->id == "board");
    // Slot 14 by the board's write switch and its hide switch.
    const auto fourteen = topology.groups_covering(topology.sockets[1]);
    REQUIRE(fourteen.size() == 2);
    CHECK(fourteen[0]->id == "board");
    CHECK(fourteen[1]->id == "socket-14");
}

TEST_CASE("A header change reaches the socket answering its slot", "[sideways]") {
    SidewaysTopology topology = sideways_topology(model_b());
    beebium::RomHeader header;
    header.set_recognised(true);
    header.set_title("Wordwise");
    header.set_version("1.20");
    // Slot 9 is IC88's.
    CHECK(topology.apply_header_change(9, header));
    const auto ic88 = std::find_if(topology.sockets.begin(), topology.sockets.end(),
                                   [](const SidewaysSocket& s) { return s.label == "IC88"; });
    REQUIRE(ic88 != topology.sockets.end());
    CHECK(ic88->rom.title == "Wordwise");
    CHECK(ic88->rom.version == "1.20");
    CHECK(ic88->populated);
    // The same again changes nothing; a slot no socket answers is ignored.
    CHECK_FALSE(topology.apply_header_change(9, header));
    beebium::GetSlotStatusResponse sparse;
    auto* only = sparse.add_sockets();
    only->set_socket_index(15);
    only->add_aliased_slots(15);
    SidewaysTopology one = sideways_topology(sparse);
    CHECK_FALSE(one.apply_header_change(3, header));
}

TEST_CASE("A machine with no sideways topology has no sockets", "[sideways]") {
    const SidewaysTopology topology = sideways_topology(beebium::GetSlotStatusResponse{});
    CHECK(topology.sockets.empty());
    CHECK(topology.groups.empty());
}
