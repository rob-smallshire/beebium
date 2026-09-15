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

// Direct (non-gRPC) tests for AunUi. Drives a real
// AunEconetTransportExtension wired to an AunBackend on an OS-assigned
// ephemeral port, manipulates its peer table directly, and asserts the
// resulting View tree shape and labels.
//
// End-to-end gRPC behaviour is exercised by test_grpc_extension_ui_service
// (framework) and test_grpc_aun_service (typed AUN RPCs); this file
// focuses on AunUi-specific build_view output without re-testing the
// validation gauntlet.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "AunUi.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include "extension_ui.pb.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <memory>

namespace {

// Spin up an AUN extension whose backend is bound to an OS-chosen port,
// expose the live PiconetUi for tests to drive.
class AunUiFixture {
public:
    AunUiFixture() {
        ext_ = std::make_unique<beebium::AunEconetTransportExtension>();
        ext_->set_config({{"port", "0"}});
        backend_owner_ = ext_->create_backend(/*station=*/1);
        REQUIRE(backend_owner_ != nullptr);
    }

    beebium::AunEconetTransportExtension& extension() { return *ext_; }
    beebium::AunBackend& backend() {
        return *static_cast<beebium::AunBackend*>(backend_owner_.get());
    }

private:
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

}  // namespace

// Helper: locate a control by id within the AUN root group. Returns
// nullptr if not found. Used by the slice-2 tests so they don't have
// to know the exact ordering of controls relative to the peers group.
const beebium::Control* find_control(const beebium::Control& root,
                                     const std::string& id) {
    if (root.id() == id) return &root;
    if (root.control_case() != beebium::Control::kGroup) return nullptr;
    for (int i = 0; i < root.group().controls_size(); ++i) {
        if (auto* hit = find_control(root.group().controls(i), id)) {
            return hit;
        }
    }
    return nullptr;
}

TEST_CASE("AunUi build_view (no peers) emits the empty placeholder",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    beebium::View view;
    ui->build_view(&view);

    const auto& root = view.root();
    REQUIRE(root.id() == "root");
    REQUIRE(root.control_case() == beebium::Control::kGroup);
    REQUIRE(root.group().label() == "AUN");

    const auto* peers = find_control(root, "peers_group");
    REQUIRE(peers != nullptr);
    REQUIRE(peers->control_case() == beebium::Control::kGroup);
    REQUIRE(peers->group().label() == "Peers");
    REQUIRE(peers->group().controls_size() == 1);

    const auto& empty_label = peers->group().controls(0);
    REQUIRE(empty_label.id() == "no_peers");
    REQUIRE(empty_label.control_case() == beebium::Control::kLabel);
    REQUIRE(empty_label.label().text() == "No peer stations configured");
}

TEST_CASE("AunUi build_view (one peer) emits one labelled row",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.backend().add_peer(0, 254, make_ip(127, 0, 0, 1), 32768);

    auto* ui = fixture.extension().ui();
    beebium::View view;
    ui->build_view(&view);

    const auto* peers = find_control(view.root(), "peers_group");
    REQUIRE(peers != nullptr);
    REQUIRE(peers->group().controls_size() == 1);

    const auto& peer = peers->group().controls(0);
    REQUIRE(peer.id() == "peer.0.254");
    REQUIRE(peer.control_case() == beebium::Control::kLabel);
    REQUIRE(peer.label().text() == "0.254  127.0.0.1:32768");
    // Operator-configured peers carry no secondary caption -- the
    // empty-string default communicates "no provenance marker".
    CHECK(peer.label().secondary_text().empty());
}

TEST_CASE("AunUi build_view marks discovered peers via secondary_text",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.backend().add_peer(0, 254, make_ip(127, 0, 0, 1), 32768,
                               beebium::PeerSource::Discovered);

    auto* ui = fixture.extension().ui();
    beebium::View view;
    ui->build_view(&view);

    const auto* peers = find_control(view.root(), "peers_group");
    REQUIRE(peers != nullptr);
    REQUIRE(peers->group().controls_size() == 1);

    const auto& peer = peers->group().controls(0);
    // Primary text unchanged -- provenance lives on secondary_text so
    // the renderer can style it as a muted caption.
    CHECK(peer.label().text() == "0.254  127.0.0.1:32768");
    CHECK(peer.label().secondary_text() == "mDNS");
}

TEST_CASE("AunUi build_view (multiple peers) emits one row per peer",
          "[aun][ui]") {
    AunUiFixture fixture;
    fixture.backend().add_peer(0, 253, make_ip(192, 168, 1, 5), 32769);
    fixture.backend().add_peer(0, 254, make_ip(127, 0, 0, 1), 32768);

    auto* ui = fixture.extension().ui();
    beebium::View view;
    ui->build_view(&view);

    const auto* peers = find_control(view.root(), "peers_group");
    REQUIRE(peers != nullptr);
    REQUIRE(peers->group().controls_size() == 2);

    // Order is whatever AunBackend::list_peers() returns -- we don't
    // promise a sort order, but each id and label must match its peer.
    bool saw_253 = false;
    bool saw_254 = false;
    for (int i = 0; i < peers->group().controls_size(); ++i) {
        const auto& c = peers->group().controls(i);
        REQUIRE(c.control_case() == beebium::Control::kLabel);
        if (c.id() == "peer.0.253") {
            saw_253 = true;
            REQUIRE(c.label().text() == "0.253  192.168.1.5:32769");
        } else if (c.id() == "peer.0.254") {
            saw_254 = true;
            REQUIRE(c.label().text() == "0.254  127.0.0.1:32768");
        }
    }
    REQUIRE(saw_253);
    REQUIRE(saw_254);
}

TEST_CASE("AunUi build_view (no backend) reports backend-unavailable",
          "[aun][ui]") {
    // port=none disables the network: create_backend returns nullptr,
    // backend() stays null. AunUi should still produce a coherent View
    // saying so, rather than crashing or rendering empty.
    beebium::AunEconetTransportExtension ext;
    ext.set_config({{"port", "none"}});
    auto backend = ext.create_backend(/*station=*/1);
    REQUIRE(backend == nullptr);

    auto* ui = ext.ui();
    REQUIRE(ui != nullptr);

    beebium::View view;
    ui->build_view(&view);

    const auto* peers = find_control(view.root(), "peers_group");
    REQUIRE(peers != nullptr);
    REQUIRE(peers->group().controls_size() == 1);
    REQUIRE(peers->group().controls(0).id() == "no_peers");
    REQUIRE(peers->group().controls(0).label().text() == "AUN backend unavailable");

    // No backend -> no Connect button and no UDP port label.
    REQUIRE(find_control(view.root(), "connect_action") == nullptr);
    REQUIRE(find_control(view.root(), "udp_port") == nullptr);
}

TEST_CASE("AunUi build_view (bind failure) reports the specific port and cause",
          "[aun][ui]") {
    // Occupy an OS-chosen UDP port, then ask the extension to bind that exact
    // port -> a bind conflict. Where the duplicate bind fails (macOS/BSD),
    // the "unavailable" line must NAME the port (not a generic message), so
    // the sidebar leads straight to the cause. Skipped where SO_REUSEADDR
    // lets the duplicate bind succeed (create_backend then returns a backend).
    beebium::AunBackend holder(0, 1, 0);
    REQUIRE(holder.is_connected());
    const std::uint16_t held = holder.local_port();

    beebium::AunEconetTransportExtension ext;
    ext.set_config({{"port", std::to_string(held)}});
    auto backend = ext.create_backend(/*station=*/1);
    if (backend != nullptr) {
        SUCCEED("duplicate bind allowed on this platform; nothing to assert");
        return;
    }

    beebium::View view;
    ext.ui()->build_view(&view);

    const auto* peers = find_control(view.root(), "peers_group");
    REQUIRE(peers != nullptr);
    REQUIRE(peers->group().controls_size() == 1);
    const auto& text = peers->group().controls(0).label().text();
    CHECK(text != "AUN backend unavailable");
    CHECK(text.find(std::to_string(held)) != std::string::npos);
}

TEST_CASE("AunUi build_view emits Connect button + UDP port when backend live",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto* ui = fixture.extension().ui();
    beebium::View view;
    ui->build_view(&view);

    const auto* button = find_control(view.root(), "connect_action");
    REQUIRE(button != nullptr);
    REQUIRE(button->control_case() == beebium::Control::kButton);
    REQUIRE(button->button().enabled());
    // Constructor binds the socket and is_connected() is true on a
    // freshly-bound AunBackend, so the action is "Disconnect".
    REQUIRE(button->button().label() == "Disconnect");

    const auto* port_label = find_control(view.root(), "udp_port");
    REQUIRE(port_label != nullptr);
    REQUIRE(port_label->control_case() == beebium::Control::kLabel);
    REQUIRE(port_label->label().text().find("Listening on UDP port") !=
            std::string::npos);
    // The port number is whatever the OS assigned -- just confirm it
    // appears in the label as a non-empty integer.
    REQUIRE(port_label->label().text().find(
                std::to_string(fixture.backend().local_port())) !=
            std::string::npos);
}

TEST_CASE("AunUi connect_action dispatch flips is_connected and the label",
          "[aun][ui]") {
    AunUiFixture fixture;
    auto* ui = fixture.extension().ui();

    // Starts connected.
    REQUIRE(fixture.backend().is_connected());

    beebium::DispatchRequest req;
    req.set_extension_id("aun");
    req.set_control_id("connect_action");
    req.set_view_revision(ui->current_revision());
    // Buttons take no payload.

    ui->handle_event(req);
    REQUIRE_FALSE(fixture.backend().is_connected());

    // Next View shows the button labelled "Connect".
    beebium::View view;
    ui->build_view(&view);
    const auto* button = find_control(view.root(), "connect_action");
    REQUIRE(button != nullptr);
    REQUIRE(button->button().label() == "Connect");

    // And handle_event bumps the revision so a stale event would be
    // rejected by the framework's gauntlet.
    REQUIRE(ui->current_revision() > 1u);
}
