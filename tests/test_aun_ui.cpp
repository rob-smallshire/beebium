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

// Direct (non-gRPC) tests for AunUi: view-building for each state and
// event-dispatch for the map-file edit affordances (Add/Edit/Remove peer,
// Add/Edit/Remove subnet, Save-to-map-file, Reload). Drives a real
// AunEconetTransportExtension on an ephemeral port with a hermetic temp map
// file, so no test touches the real per-user aun-map.json.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
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

namespace {

class AunUiFixture {
public:
    AunUiFixture() {
        std::random_device rd;
        map_filepath_ = std::filesystem::temp_directory_path() /
                        ("beebium-aun-ui-map-" + std::to_string(rd()) + ".json");
        ext_ = std::make_unique<beebium::AunEconetTransportExtension>();
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

    // Build the current view and return it.
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

// Recursively locate a control by id anywhere in the view tree, descending
// into Groups and into a ModalEditor's anchor and editor subtree.
const beebium::Control* find_control(const beebium::Control& root,
                                     const std::string& id) {
    if (root.id() == id) return &root;
    if (root.control_case() == beebium::Control::kGroup) {
        for (int i = 0; i < root.group().controls_size(); ++i) {
            if (auto* hit = find_control(root.group().controls(i), id)) {
                return hit;
            }
        }
    } else if (root.control_case() == beebium::Control::kModalEditor) {
        const auto& modal = root.modal_editor();
        if (modal.has_anchor()) {
            if (auto* hit = find_control(modal.anchor(), id)) return hit;
        }
        if (modal.has_editor()) {
            if (auto* hit = find_control(modal.editor(), id)) return hit;
        }
    }
    return nullptr;
}

// A DispatchRequest for a plain Button control.
beebium::DispatchRequest button_event(const std::string& control_id) {
    beebium::DispatchRequest req;
    req.set_control_id(control_id);
    return req;
}

// A DispatchRequest for a ModalEditor commit, with field_id/value pairs.
beebium::DispatchRequest commit_event(
    const std::string& control_id,
    const std::vector<std::pair<std::string, std::string>>& fields) {
    beebium::DispatchRequest req;
    req.set_control_id(control_id);
    auto* commit = req.mutable_editor_commit();
    for (const auto& [field_id, value] : fields) {
        auto* f = commit->add_fields();
        f->set_field_id(field_id);
        f->set_string_value(value);
    }
    return req;
}

}  // namespace

using beebium::Control;

TEST_CASE("AunUi: empty state shows placeholders and the add/reload affordances",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto view = fixture.view();
    const auto& root = view.root();
    REQUIRE(root.group().label() == "AUN");

    CHECK(find_control(root, "no_peers") != nullptr);
    CHECK(find_control(root, "add_peer") != nullptr);       // Add-peer form
    CHECK(find_control(root, "add_subnet") != nullptr);     // Add-subnet form
    CHECK(find_control(root, "subnets_group") != nullptr);
    CHECK(find_control(root, "reload_map") != nullptr);
    const auto* path = find_control(root, "map_path");
    REQUIRE(path != nullptr);
    CHECK(path->label().text().find(fixture.map_filepath().string()) !=
          std::string::npos);
    // The add-peer form is a ModalEditor with the net.stn/host/port/label fields.
    const auto* add = find_control(root, "add_peer");
    REQUIRE(add->control_case() == Control::kModalEditor);
    CHECK(find_control(root, "add_peer.net_stn") != nullptr);
    CHECK(find_control(root, "add_peer.host") != nullptr);
    CHECK(find_control(root, "add_peer.port") != nullptr);
}

TEST_CASE("AunUi: a map-file peer shows its label with Edit and Remove",
          "[aun][ui]") {
    AunUiFixture fixture;
    REQUIRE(fixture.extension()
                .add_map_peer(0, 254, "127.0.0.1", 32768, "file server")
                .error.empty());
    auto view = fixture.view();
    const auto& root = view.root();

    const auto* label = find_control(root, "peer.0.254.label");
    REQUIRE(label != nullptr);
    CHECK(label->label().text().find("file server") != std::string::npos);
    CHECK(label->label().secondary_text() == "map file");
    CHECK(find_control(root, "edit_peer.0.254") != nullptr);
    CHECK(find_control(root, "remove_peer.0.254") != nullptr);
    // A map-file peer has no Save-to-map-file button.
    CHECK(find_control(root, "save_peer.0.254") == nullptr);
}

TEST_CASE("AunUi: a non-map-file peer offers Save to map file",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().peer_set().set_peer(0, 100, make_ip(10, 0, 0, 1), 40001,
                                            beebium::AunPeerProvenance::Launch);
    auto view = fixture.view();
    const auto& root = view.root();
    CHECK(find_control(root, "save_peer.0.100") != nullptr);
    CHECK(find_control(root, "edit_peer.0.100") == nullptr);
    CHECK(find_control(root, "remove_peer.0.100") == nullptr);
}

TEST_CASE("AunUi: a discovered peer's Save warns about the ephemeral port",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().peer_set().set_peer(
        0, 101, make_ip(10, 0, 0, 2), 40002,
        beebium::AunPeerProvenance::Discovered);
    auto view = fixture.view();
    const auto& root = view.root();
    CHECK(find_control(root, "save_peer.0.101") != nullptr);
    const auto* warning = find_control(root, "save_peer.0.101.warning");
    REQUIRE(warning != nullptr);
    CHECK(warning->label().text().find("ephemeral") != std::string::npos);
}

TEST_CASE("AunUi: subnet rules list with Edit and Remove", "[aun][ui]") {
    AunUiFixture fixture;
    REQUIRE(fixture.extension()
                .add_map_subnet(128, "192.168.5.0/24", "risc os")
                .error.empty());
    auto view = fixture.view();
    const auto& root = view.root();
    const auto* label = find_control(root, "subnet.128.label");
    REQUIRE(label != nullptr);
    CHECK(label->label().text().find("192.168.5.0/24") != std::string::npos);
    CHECK(label->label().secondary_text() == "risc os");
    CHECK(find_control(root, "edit_subnet.128") != nullptr);
    CHECK(find_control(root, "remove_subnet.128") != nullptr);
}

TEST_CASE("AunUi: a map-file load error is shown", "[aun][ui]") {
    AunUiFixture fixture;
    std::ofstream(fixture.map_filepath(), std::ios::binary | std::ios::trunc)
        << R"({"peers": [}})";
    fixture.extension().reload_map_file();
    auto view = fixture.view();
    const auto* err = find_control(view.root(), "map_load_error");
    REQUIRE(err != nullptr);
    CHECK(err->label().text().find("Map file error") != std::string::npos);
}

TEST_CASE("AunUi: Add-peer commit writes the map file", "[aun][ui]") {
    AunUiFixture fixture;
    auto req = commit_event("add_peer", {{"add_peer.net_stn", "0.254"},
                                         {"add_peer.host", "192.168.1.10"},
                                         {"add_peer.port", "32768"},
                                         {"add_peer.label", "fs"}});
    fixture.extension().ui()->handle_event(req);

    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].stn == 254);
    CHECK(peers[0].host == "192.168.1.10");
    CHECK(peers[0].label == "fs");
    // No edit error after a valid submit.
    CHECK(find_control(fixture.view().root(), "edit_error") == nullptr);
}

TEST_CASE("AunUi: an invalid Add-peer commit shows a field-named error",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto req = commit_event("add_peer", {{"add_peer.net_stn", "0.999"},
                                         {"add_peer.host", "192.168.1.10"},
                                         {"add_peer.port", "32768"}});
    fixture.extension().ui()->handle_event(req);

    CHECK(fixture.extension().map_peers().empty());  // nothing written
    auto view = fixture.view();
    const auto* err = find_control(view.root(), "edit_error");
    REQUIRE(err != nullptr);
    CHECK(err->label().text().find("net.stn") != std::string::npos);
}

TEST_CASE("AunUi: Edit-peer commit replaces the endpoint", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().add_map_peer(0, 254, "192.168.1.10", 32768, "fs");
    auto req = commit_event("edit_peer.0.254", {{"edit_peer.0.254.host", "192.168.1.99"},
                                                {"edit_peer.0.254.port", "40000"},
                                                {"edit_peer.0.254.label", "moved"}});
    fixture.extension().ui()->handle_event(req);

    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].host == "192.168.1.99");
    CHECK(peers[0].port == 40000);
    CHECK(peers[0].label == "moved");
}

TEST_CASE("AunUi: Remove-peer deletes the entry", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().add_map_peer(0, 254, "192.168.1.10", 32768, "");
    REQUIRE(fixture.extension().map_peers().size() == 1);
    fixture.extension().ui()->handle_event(button_event("remove_peer.0.254"));
    CHECK(fixture.extension().map_peers().empty());
}

TEST_CASE("AunUi: Save-to-map-file copies a live peer into the file",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().peer_set().set_peer(0, 100, make_ip(10, 0, 0, 1), 40001,
                                            beebium::AunPeerProvenance::Launch);
    fixture.extension().ui()->handle_event(button_event("save_peer.0.100"));
    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].net == 0);
    CHECK(peers[0].stn == 100);
    CHECK(peers[0].host == "10.0.0.1");
    CHECK(peers[0].port == 40001);
}

TEST_CASE("AunUi: Add-subnet and Remove-subnet", "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().ui()->handle_event(
        commit_event("add_subnet", {{"add_subnet.net", "128"},
                                    {"add_subnet.subnet", "192.168.5.0/24"},
                                    {"add_subnet.label", "risc"}}));
    REQUIRE(fixture.extension().map_subnets().size() == 1);
    CHECK(fixture.extension().map_subnets()[0].net == 128);

    fixture.extension().ui()->handle_event(button_event("remove_subnet.128"));
    CHECK(fixture.extension().map_subnets().empty());
}

TEST_CASE("AunUi: an invalid Add-subnet shows a field-named error",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.extension().ui()->handle_event(
        commit_event("add_subnet", {{"add_subnet.net", "1"},
                                    {"add_subnet.subnet", "192.168.5.0/16"}}));
    CHECK(fixture.extension().map_subnets().empty());
    auto view = fixture.view();
    const auto* err = find_control(view.root(), "edit_error");
    REQUIRE(err != nullptr);
    CHECK(err->label().text().find("subnet") != std::string::npos);
}

TEST_CASE("AunUi: Reload picks up an external edit", "[aun][ui]") {
    AunUiFixture fixture;
    std::ofstream(fixture.map_filepath(), std::ios::binary | std::ios::trunc)
        << R"({"peers":[{"net":0,"station":1,"host":"10.0.0.1","port":32768}]})";
    fixture.extension().ui()->handle_event(button_event("reload_map"));
    auto peers = fixture.extension().map_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].stn == 1);
}

TEST_CASE("AunUi: connect button + UDP port when the backend is live",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto view = fixture.view();
    const auto* button = find_control(view.root(), "connect_action");
    REQUIRE(button != nullptr);
    REQUIRE(button->button().label() == "Disconnect");
    const auto* port = find_control(view.root(), "udp_port");
    REQUIRE(port != nullptr);
    CHECK(port->label().text().find("Listening on UDP port") !=
          std::string::npos);
}

TEST_CASE("AunUi: connect_action dispatch flips is_connected", "[aun][ui]") {
    AunUiFixture fixture;
    REQUIRE(fixture.backend().is_connected());
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
}
