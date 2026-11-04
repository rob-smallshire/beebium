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

// The live peripheral tree (client specification 12.3), as the macOS
// client builds it: grouped by attachment point in the server's order, a
// node's parent the provider the server chose, its name the server's label.

#include "connection/PeripheralTree.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

beebium::AttachmentPointInfo* point(beebium::ListExtensionsResponse& r, const char* id,
                                    const char* name) {
    auto* p = r.add_attachment_points();
    p->set_id(id);
    p->set_display_name(name);
    return p;
}

beebium::ExtensionInfo* extension(beebium::ListExtensionsResponse& r, const char* id,
                                  const char* label,
                                  std::vector<std::pair<const char*, const char*>> attachments) {
    auto* e = r.add_extensions();
    e->set_id(id);
    e->set_label(label);
    e->set_name(std::string("ext-") + id);
    for (const auto& [where, provider] : attachments) {
        e->add_attaches_to(where);
        auto* a = e->add_attachments();
        a->set_point(where);
        a->set_provider_id(provider);
    }
    return e;
}

beebium::ListExtensionsResponse machine() {
    beebium::ListExtensionsResponse r;
    point(r, "1mhz-bus", "1 MHz Bus");
    point(r, "user-port", "User Port");
    point(r, "serial-port", "Serial Port");
    point(r, "scsi-bus", "SCSI Bus");
    point(r, "analogue-port", "Analogue Port");
    auto* adapter = extension(r, "adapter", "Acorn SCSI Host Adapter", {{"1mhz-bus", ""}});
    adapter->set_has_ui(true);
    adapter->set_description("Connects SCSI hard discs");
    extension(r, "disc0", "Hard Disc (SCSI ID 0)", {{"scsi-bus", "adapter"}});
    extension(r, "disc1", "Hard Disc (SCSI ID 1)", {{"scsi-bus", "adapter"}});
    extension(r, "joystick", "Voltmace Delta", {{"user-port", ""}, {"analogue-port", ""}});
    extension(r, "loose", "", {});  // attaches nowhere: unattached
    extension(r, "stray", "Stray", {{"scsi-bus", "missing-adapter"}});
    return r;
}

}  // namespace

TEST_CASE("Peripherals group under their points in the server's order, empty points omitted",
          "[peripherals][tree]") {
    const PeripheralTree tree = peripheral_tree(machine());
    REQUIRE(tree.groups.size() == 2);
    CHECK(tree.groups[0].point_id == "1mhz-bus");
    CHECK(tree.groups[0].display_name == "1 MHz Bus");
    CHECK(tree.groups[1].point_id == "user-port");
    CHECK_FALSE(tree.empty());
}

TEST_CASE("A node's children are those the server attached to it as provider",
          "[peripherals][tree]") {
    const PeripheralTree tree = peripheral_tree(machine());
    const PeripheralNode& adapter = tree.groups[0].nodes.at(0);
    CHECK(adapter.id == "adapter");
    CHECK(adapter.name == "Acorn SCSI Host Adapter");
    CHECK(adapter.has_ui);
    CHECK(adapter.description == "Connects SCSI hard discs");
    REQUIRE(adapter.children.size() == 2);
    CHECK(adapter.children[0].name == "Hard Disc (SCSI ID 0)");
    CHECK(adapter.children[1].name == "Hard Disc (SCSI ID 1)");
}

TEST_CASE("A device on several points shows once, the others as badges", "[peripherals][tree]") {
    const PeripheralTree tree = peripheral_tree(machine());
    const PeripheralNode& joystick = tree.groups[1].nodes.at(0);
    CHECK(joystick.name == "Voltmace Delta");
    CHECK(joystick.also == std::vector<std::string>{"Analogue Port"});
}

TEST_CASE("What attaches nowhere, or to a provider not listed, is shown as unattached",
          "[peripherals][tree]") {
    const PeripheralTree tree = peripheral_tree(machine());
    REQUIRE(tree.unattached.size() == 2);
    // No label: the id stands in; a provider not listed: still shown.
    CHECK(tree.unattached[0].name == "loose");
    CHECK(tree.unattached[1].name == "Stray");
}

TEST_CASE("No extensions is an empty tree", "[peripherals][tree]") {
    beebium::ListExtensionsResponse r;
    point(r, "serial-port", "Serial Port");
    CHECK(peripheral_tree(r).empty());
}

TEST_CASE("Storage devices are listed in tree order", "[peripherals][tree][storage]") {
    auto r = machine();
    for (auto& e : *r.mutable_extensions()) {
        if (e.id() == "disc0" || e.id() == "disc1") {
            auto* device = e.add_storage_devices();
            device->set_id(e.id() + "-drive");
            device->set_name("Hard Disc Drive " + e.id().substr(4));
            device->set_backing_path("/discs/" + e.id() + ".dat");
            device->set_activity_indicator_name("scsi-" + e.id());
        }
    }
    const auto devices = peripheral_tree(r).storage_devices();
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].name == "Hard Disc Drive 0");
    CHECK(devices[0].backing_path == "/discs/disc0.dat");
    CHECK(devices[0].activity_indicator == "scsi-disc0");
    CHECK(devices[1].name == "Hard Disc Drive 1");
}

TEST_CASE("The serial line names the connector, and the RS423 rates once seen",
          "[peripherals][serial]") {
    SerialLine line;
    CHECK(line.text().empty());  // nothing heard
    beebium::SerialStatus none;
    none.set_has_serial_socket(false);
    line.observe(none);
    CHECK(line.text().empty());
    beebium::SerialStatus cassette;
    cassette.set_has_serial_socket(true);
    cassette.set_rs423_selected(false);
    cassette.set_tx_baud(300);
    cassette.set_rx_baud(300);
    line.observe(cassette);
    CHECK(line.text() == "RS423");  // the cassette's rate is never shown
    beebium::SerialStatus rs423 = cassette;
    rs423.set_connector("RS423");
    rs423.set_rs423_selected(true);
    rs423.set_tx_baud(75);
    rs423.set_rx_baud(1200);
    line.observe(rs423);
    CHECK(line.text() == "RS423 Tx 75 / Rx 1200 baud");
    // Back to cassette: the last RS423 rates stand.
    line.observe(cassette);
    CHECK(line.text() == "RS423 Tx 75 / Rx 1200 baud");
}
