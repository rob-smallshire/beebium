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

// Direct (non-gRPC) tests for AunUi after the #144 rebuild: the panel is a
// Connect button, a UDP-port label, an EditableList of peers, an EditableList
// of subnet rules, a FileReference for the map file, and -- on a bad edit --
// one error Indicator. Tests drive a real AunEconetTransportExtension on an
// ephemeral port with a hermetic temp map file, so no test touches the real
// per-user aun-map.json. Dispatch goes straight into handle_event with the
// EditableListEvent / file_action_id payloads the framework would deliver.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "test_aun_helpers.hpp"
#include "AunPeerSet.hpp"
#include "AunUi.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include "extension_ui.pb.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

class AunUiFixture {
public:
    AunUiFixture() {
        std::random_device rd;
        map_filepath_ = std::filesystem::temp_directory_path() /
                        ("beebium-aun-ui-map-" + std::to_string(rd()) + ".json");
        ext_ = std::make_unique<beebium::AunEconetTransportExtension>();
        // Unique discovery type so a real _aun._udp announcer cannot be adopted
        // into the peer set and perturb the view under test (#145).
        ext_->set_discovery_service_type(aun_unique_service_type());
        ext_->set_config({{"port", "0"}, {"map-file", map_filepath_.string()}});
        backend_owner_ = ext_->create_backend(/*station=*/1);
        REQUIRE(backend_owner_ != nullptr);
    }

    ~AunUiFixture() {
        std::error_code ec;
        std::filesystem::remove(map_filepath_, ec);
    }

    beebium::AunEconetTransportExtension& extension() { return *ext_; }
    beebium::AunBackend& backend() {
        return *static_cast<beebium::AunBackend*>(backend_owner_.get());
    }
    const std::filesystem::path& map_filepath() const { return map_filepath_; }

    beebium::View view() {
        beebium::View v;
        ext_->ui()->build_view(&v);
        return v;
    }

private:
    std::filesystem::path map_filepath_;
    std::unique_ptr<beebium::AunEconetTransportExtension> ext_;
    std::unique_ptr<beebium::NetworkBackend> backend_owner_;
};

uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    in_addr addr{};
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a, b, c, d);
    inet_pton(AF_INET, buf, &addr);
    return addr.s_addr;
}

// Locate a top-level control by id (controls are direct or nested Group
// children of the root).
const beebium::Control* find_control(const beebium::Control& root,
                                     const std::string& id) {
    if (root.id() == id) return &root;
    if (root.control_case() == beebium::Control::kGroup) {
        for (const auto& child : root.group().controls()) {
            if (auto* hit = find_control(child, id)) return hit;
        }
    }
    return nullptr;
}

// The EditableList control named `id`, or nullptr.
const beebium::EditableList* list_of(const beebium::View& view,
                                     const std::string& id) {
    const auto* c = find_control(view.root(), id);
    if (!c || c->control_case() != beebium::Control::kEditableList) return nullptr;
    return &c->editable_list();
}

// The item with this id in a list, or nullptr.
const beebium::EditableListItem* item_of(const beebium::EditableList& list,
                                         const std::string& item_id) {
    for (const auto& item : list.items()) {
        if (item.id() == item_id) return &item;
    }
    return nullptr;
}

// The value of an editor field by id, searching an editor Control's group.
std::string editor_field(const beebium::Control& editor,
                         const std::string& field_id) {
    for (const auto& child : editor.group().controls()) {
        if (child.id() == field_id) return child.text_input().value();
    }
    return "<missing>";
}

// The help text of an editor field by id.
std::string editor_field_help(const beebium::Control& editor,
                              const std::string& field_id) {
    for (const auto& child : editor.group().controls()) {
        if (child.id() == field_id) return child.text_input().help();
    }
    return "<missing>";
}

// The inline note of an editor field by id.
std::string editor_field_note(const beebium::Control& editor,
                              const std::string& field_id) {
    for (const auto& child : editor.group().controls()) {
        if (child.id() == field_id) return child.text_input().note();
    }
    return "<missing>";
}

// Build an EditableListEvent dispatch with editor field values.
beebium::DispatchRequest list_event(
    const std::string& control_id, beebium::EditableListEvent::Kind kind,
    const std::string& item_id, const std::string& action_id,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    beebium::DispatchRequest req;
    req.set_control_id(control_id);
    auto* event = req.mutable_editable_list_event();
    event->set_kind(kind);
    if (!item_id.empty()) event->set_item_id(item_id);
    if (!action_id.empty()) event->set_action_id(action_id);
    auto* commit = event->mutable_commit();
    for (const auto& [field_id, value] : fields) {
        auto* f = commit->add_fields();
        f->set_field_id(field_id);
        f->set_string_value(value);
    }
    return req;
}

beebium::DispatchRequest file_action(const std::string& control_id,
                                     const std::string& action_id) {
    beebium::DispatchRequest req;
    req.set_control_id(control_id);
    req.set_file_action_id(action_id);
    return req;
}

beebium::DispatchRequest button_event(const std::string& control_id) {
    beebium::DispatchRequest req;
    req.set_control_id(control_id);
    return req;
}

}  // namespace

using beebium::Control;
using beebium::EditableListEvent;

TEST_CASE("AunUi: empty state is two lists, a file reference and no errors",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto view = fixture.view();
    const auto& root = view.root();
    REQUIRE(root.group().label() == "AUN");

    CHECK(find_control(root, "connect_action") != nullptr);
    CHECK(find_control(root, "udp_port") != nullptr);
    CHECK(find_control(root, "edit_error") == nullptr);

    const auto* peers = list_of(view, "peers");
    REQUIRE(peers != nullptr);
    CHECK(peers->title() == "Peers (0)");  // count rides on the title
    CHECK(peers->help().empty() == false);  // list-level help for a newcomer
    CHECK(peers->can_add());
    CHECK(peers->items_size() == 0);
    CHECK(peers->empty_text().empty() == false);
    // The add editor carries net.stn/host/port/label fields, each with help.
    CHECK(editor_field(peers->add_editor(), "net_stn") == "");
    CHECK(editor_field(peers->add_editor(), "port") == "32768");
    CHECK(editor_field_help(peers->add_editor(), "net_stn").find("Net 0") !=
          std::string::npos);

    const auto* subnets = list_of(view, "subnets");
    REQUIRE(subnets != nullptr);
    CHECK(subnets->title() == "Subnet rules (0)");
    CHECK(subnets->help().empty() == false);
    CHECK(subnets->can_add());

    const auto* file = find_control(root, "map_file");
    REQUIRE(file != nullptr);
    REQUIRE(file->control_case() == Control::kFileReference);
    CHECK(file->file_reference().display_name() == "aun-map.json");
    CHECK(file->file_reference().path().find(fixture.map_filepath().string()) !=
          std::string::npos);
    CHECK(file->file_reference().state() == beebium::Indicator::OK);
    CHECK(file->file_reference().state_text() == "loaded");  // short; counts on titles
    REQUIRE(file->file_reference().actions_size() == 1);
    CHECK(file->file_reference().actions(0).id() == "reload");
}

TEST_CASE("AunUi: a map-file peer is an editable, removable item",
          "[aun][ui]") {
    AunUiFixture fixture;
    REQUIRE(fixture.extension()
                .add_map_peer(0, 254, "127.0.0.1", 32768, "file server")
                .error.empty());
    auto view = fixture.view();
    const auto* peers = list_of(view, "peers");
    REQUIRE(peers != nullptr);
    const auto* item = item_of(*peers, "0.254");
    REQUIRE(item != nullptr);
    CHECK(item->secondary() == "map file");
    CHECK(item->subtitle() == "file server");
    CHECK(item->editable());
    CHECK(item->removable());
    CHECK(item->actions_size() == 0);  // no "Save" on a map-file row
    CHECK(editor_field(item->editor(), "host") == "127.0.0.1");
    CHECK(editor_field(item->editor(), "label") == "file server");
    // The count now rides on the list title; the file state stays short.
    CHECK(list_of(view, "peers")->title() == "Peers (1)");
    CHECK(find_control(view.root(), "map_file")->file_reference().state_text() ==
          "loaded");
}

TEST_CASE("AunUi: a non-map-file peer is read-only with a Save action",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().peer_set().set_peer(0, 100, make_ip(10, 0, 0, 1), 40001,
                                            beebium::AunPeerProvenance::Launch);
    auto view = fixture.view();
    const auto* item = item_of(*list_of(view, "peers"), "0.100");
    REQUIRE(item != nullptr);
    CHECK(item->secondary() == "launch");
    CHECK_FALSE(item->editable());
    CHECK_FALSE(item->removable());
    REQUIRE(item->actions_size() == 1);
    CHECK(item->actions(0).id() == "save_to_map");
    // The Save action opens a prefilled ADD-style sheet (same shape as "+"),
    // seeded with this row's endpoint.
    REQUIRE(item->actions(0).has_editor());
    const auto& save_editor = item->actions(0).editor();
    CHECK(editor_field(save_editor, "net_stn") == "0.100");
    CHECK(editor_field(save_editor, "host") == "10.0.0.1");
    CHECK(editor_field(save_editor, "port") == "40001");
    CHECK(editor_field_note(save_editor, "port").empty());  // no note: not mDNS
    CHECK(item->note().empty());
}

TEST_CASE("AunUi: a discovered peer's Save carries the ephemeral-port note",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().peer_set().set_peer(
        0, 101, make_ip(10, 0, 0, 2), 40002,
        beebium::AunPeerProvenance::Discovered);
    auto view = fixture.view();
    const auto* item = item_of(*list_of(view, "peers"), "0.101");
    REQUIRE(item != nullptr);
    CHECK(item->secondary() == "mDNS");
    REQUIRE(item->actions_size() == 1);
    CHECK(item->actions(0).id() == "save_to_map");
    // The ephemeral-port warning sits on the Save editor's port field, not on
    // the row itself.
    REQUIRE(item->actions(0).has_editor());
    CHECK(editor_field_note(item->actions(0).editor(), "port").find(
              "mDNS peer may pick") != std::string::npos);
    CHECK(item->note().empty());
}

TEST_CASE("AunUi: subnet rules appear as editable, removable items",
          "[aun][ui]") {
    AunUiFixture fixture;
    REQUIRE(fixture.extension()
                .add_map_subnet(128, "192.168.5.0/24", "risc os")
                .error.empty());
    auto view = fixture.view();
    const auto* item = item_of(*list_of(view, "subnets"), "128");
    REQUIRE(item != nullptr);
    CHECK(item->primary().find("192.168.5.0/24") != std::string::npos);
    CHECK(item->subtitle() == "risc os");
    CHECK(item->editable());
    CHECK(item->removable());
    CHECK(editor_field(item->editor(), "subnet") == "192.168.5.0/24");
}

TEST_CASE("AunUi: a map-file load error shows on the file reference",
          "[aun][ui]") {
    AunUiFixture fixture;
    std::ofstream(fixture.map_filepath(), std::ios::binary | std::ios::trunc)
        << R"({"peers": [}})";
    fixture.extension().reload_map_file();
    auto view = fixture.view();
    const auto* file = find_control(view.root(), "map_file");
    REQUIRE(file != nullptr);
    CHECK(file->file_reference().state() == beebium::Indicator::ERROR);
    CHECK(file->file_reference().state_text().find("JSON") != std::string::npos);
}

TEST_CASE("AunUi: ADD peer commit writes the map file", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().ui()->handle_event(
        list_event("peers", EditableListEvent::ADD, "", "",
                   {{"net_stn", "0.254"},
                    {"host", "192.168.1.10"},
                    {"port", "32768"},
                    {"label", "fs"}}));
    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].stn == 254);
    CHECK(peers[0].host == "192.168.1.10");
    CHECK(peers[0].label == "fs");
    CHECK(find_control(fixture.view().root(), "edit_error") == nullptr);
}

TEST_CASE("AunUi: an invalid ADD peer shows a field-named error Indicator",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().ui()->handle_event(
        list_event("peers", EditableListEvent::ADD, "", "",
                   {{"net_stn", "0.999"}, {"host", "192.168.1.10"},
                    {"port", "32768"}}));
    CHECK(fixture.extension().map_peers().empty());
    auto view = fixture.view();
    const auto* err = find_control(view.root(), "edit_error");
    REQUIRE(err != nullptr);
    REQUIRE(err->control_case() == Control::kIndicator);
    CHECK(err->indicator().state() == beebium::Indicator::ERROR);
    CHECK(err->indicator().text().find("net.stn") != std::string::npos);
}

TEST_CASE("AunUi: EDIT peer replaces the endpoint", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().add_map_peer(0, 254, "192.168.1.10", 32768, "fs");
    fixture.extension().ui()->handle_event(
        list_event("peers", EditableListEvent::EDIT, "0.254", "",
                   {{"host", "192.168.1.99"},
                    {"port", "40000"},
                    {"label", "moved"}}));
    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].host == "192.168.1.99");
    CHECK(peers[0].port == 40000);
    CHECK(peers[0].label == "moved");
}

TEST_CASE("AunUi: REMOVE peer deletes the entry", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().add_map_peer(0, 254, "192.168.1.10", 32768, "");
    REQUIRE(fixture.extension().map_peers().size() == 1);
    fixture.extension().ui()->handle_event(
        list_event("peers", EditableListEvent::REMOVE, "0.254", "", {}));
    CHECK(fixture.extension().map_peers().empty());
}

TEST_CASE("AunUi: the Save action copies a live peer into the map file",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().peer_set().set_peer(0, 100, make_ip(10, 0, 0, 1), 40001,
                                            beebium::AunPeerProvenance::Launch);
    // The action now presents a prefilled sheet and dispatches ACTION with the
    // committed values (the user may have adjusted them before confirming).
    fixture.extension().ui()->handle_event(
        list_event("peers", EditableListEvent::ACTION, "0.100", "save_to_map",
                   {{"net_stn", "0.100"},
                    {"host", "10.0.0.1"},
                    {"port", "40001"},
                    {"label", "kept"}}));
    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].net == 0);
    CHECK(peers[0].stn == 100);
    CHECK(peers[0].host == "10.0.0.1");
    CHECK(peers[0].port == 40001);
    CHECK(peers[0].label == "kept");
}

TEST_CASE("AunUi: ADD and REMOVE subnet", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().ui()->handle_event(
        list_event("subnets", EditableListEvent::ADD, "", "",
                   {{"net", "128"},
                    {"subnet", "192.168.5.0/24"},
                    {"label", "risc"}}));
    REQUIRE(fixture.extension().map_subnets().size() == 1);
    CHECK(fixture.extension().map_subnets()[0].net == 128);

    fixture.extension().ui()->handle_event(
        list_event("subnets", EditableListEvent::REMOVE, "128", "", {}));
    CHECK(fixture.extension().map_subnets().empty());
}

TEST_CASE("AunUi: an invalid ADD subnet shows a field-named error Indicator",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().ui()->handle_event(
        list_event("subnets", EditableListEvent::ADD, "", "",
                   {{"net", "1"}, {"subnet", "192.168.5.0/16"}}));
    CHECK(fixture.extension().map_subnets().empty());
    auto view = fixture.view();
    const auto* err = find_control(view.root(), "edit_error");
    REQUIRE(err != nullptr);
    CHECK(err->indicator().text().find("subnet") != std::string::npos);
}

TEST_CASE("AunUi: the Reload file action picks up an external edit",
          "[aun][ui]") {
    AunUiFixture fixture;
    std::ofstream(fixture.map_filepath(), std::ios::binary | std::ios::trunc)
        << R"({"peers":[{"net":0,"station":1,"host":"10.0.0.1","port":32768}]})";
    fixture.extension().ui()->handle_event(file_action("map_file", "reload"));
    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].stn == 1);
}

TEST_CASE("AunUi: the Connect button flips is_connected", "[aun][ui]") {
    AunUiFixture fixture;
    REQUIRE(fixture.backend().is_connected());
    auto view = fixture.view();
    const auto* button = find_control(view.root(), "connect_action");
    REQUIRE(button != nullptr);
    REQUIRE(button->button().label() == "Disconnect");
    fixture.extension().ui()->handle_event(button_event("connect_action"));
    CHECK_FALSE(fixture.backend().is_connected());
}

TEST_CASE("AunUi: no backend reports unavailable", "[aun][ui]") {
    beebium::AunEconetTransportExtension ext;
    ext.set_config({{"port", "none"}, {"map-file", "none"}});
    auto backend = ext.create_backend(/*station=*/1);
    REQUIRE(backend == nullptr);

    beebium::View view;
    ext.ui()->build_view(&view);
    const auto* no_peers = find_control(view.root(), "no_peers");
    REQUIRE(no_peers != nullptr);
    CHECK(no_peers->label().text() == "AUN backend unavailable");
    CHECK(find_control(view.root(), "connect_action") == nullptr);
    CHECK(find_control(view.root(), "peers") == nullptr);
}
