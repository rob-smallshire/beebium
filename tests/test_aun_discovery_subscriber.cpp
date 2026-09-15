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

// Unit tests for AunDiscoverySubscriber. We drive the subscriber via
// inject_added / inject_removed instead of a real Browser, so the
// tests are deterministic and CI-portable.

#include <catch2/catch_test_macros.hpp>

#include "AunDiscoverySubscriber.hpp"

#include <beebium/discovery/Browser.hpp>
#include <beebium/econet/AunBackend.hpp>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <memory>
#include <utility>

using namespace beebium;
using namespace beebium::discovery;

namespace {

uint32_t loopback_ip() { return htonl(INADDR_LOOPBACK); }

// A guaranteed-NOT-local address (RFC 5737 TEST-NET-3, 203.0.113.1) so a
// service reads as a REMOTE peer -- exercising the ordinary mDNS-driven
// removal path, which same-host peers deliberately bypass.
uint32_t nonlocal_ip() { return htonl(0xCB007101u); }

// Fake Browser that does nothing -- used so the subscriber's
// constructor doesn't allocate a real platform browser. Tests drive
// the subscriber via its inject_* helpers instead.
class FakeBrowser final : public Browser {
public:
    bool start(const std::string&, BrowserCallbacks) override {
        browsing_ = true;
        return true;
    }
    void stop() override { browsing_ = false; }
    BrowserState state() const override {
        return BrowserState{.available = true, .browsing = browsing_};
    }
private:
    bool browsing_ = false;
};

DiscoveredService make_service(const std::string& name,
                               uint8_t net, uint8_t stn, uint16_t port,
                               uint32_t ip = htonl(INADDR_LOOPBACK)) {
    DiscoveredService svc;
    svc.instance_name = name;
    svc.hostname = "fake.local.";
    svc.ipv4_addr_net_byte_order = ip;
    svc.port = port;
    svc.txt_records["version"] = "1";
    svc.txt_records["net"] = std::to_string(net);
    svc.txt_records["station"] = std::to_string(stn);
    svc.txt_records["port"] = std::to_string(port);
    return svc;
}

}  // namespace

TEST_CASE("AunDiscoverySubscriber::parse_txt: valid v1 entry",
          "[aun][discovery][subscriber]") {
    std::map<std::string, std::string> txt = {
        {"version", "1"},
        {"net", "3"},
        {"station", "254"},
        {"port", "32768"},
    };
    uint8_t net = 0, stn = 0;
    REQUIRE(AunDiscoverySubscriber::parse_txt(txt, net, stn));
    CHECK(net == 3);
    CHECK(stn == 254);
}

TEST_CASE("AunDiscoverySubscriber::parse_txt: rejects missing version",
          "[aun][discovery][subscriber]") {
    std::map<std::string, std::string> txt = {
        {"net", "0"}, {"station", "1"}, {"port", "1"},
    };
    uint8_t net = 0, stn = 0;
    CHECK_FALSE(AunDiscoverySubscriber::parse_txt(txt, net, stn));
}

TEST_CASE("AunDiscoverySubscriber::parse_txt: rejects unknown version",
          "[aun][discovery][subscriber]") {
    std::map<std::string, std::string> txt = {
        {"version", "2"}, {"net", "0"}, {"station", "1"}, {"port", "1"},
    };
    uint8_t net = 0, stn = 0;
    CHECK_FALSE(AunDiscoverySubscriber::parse_txt(txt, net, stn));
}

TEST_CASE("AunDiscoverySubscriber::parse_txt: rejects out-of-range bytes",
          "[aun][discovery][subscriber]") {
    std::map<std::string, std::string> txt = {
        {"version", "1"}, {"net", "256"}, {"station", "1"}, {"port", "1"},
    };
    uint8_t net = 0, stn = 0;
    CHECK_FALSE(AunDiscoverySubscriber::parse_txt(txt, net, stn));
}

TEST_CASE("AunDiscoverySubscriber: adds peer on inject_added",
          "[aun][discovery][subscriber]") {
    AunBackend backend(/*local_net=*/0, /*local_stn=*/1, 0);
    REQUIRE(backend.is_connected());
    AunDiscoverySubscriber subscriber(backend, /*local_stn=*/1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("Beebium 0.254", 0, 254, 32768));

    auto peers = backend.list_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].net == 0);
    CHECK(peers[0].stn == 254);
    CHECK(peers[0].port == 32768);
    CHECK_FALSE(backend.is_operator_configured(0, 254));
}

TEST_CASE("AunDiscoverySubscriber: skips own announcement",
          "[aun][discovery][subscriber]") {
    AunBackend backend(/*local_net=*/3, /*local_stn=*/254, 0);
    AunDiscoverySubscriber subscriber(backend, /*local_stn=*/254,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("Beebium 3.254", 3, 254, 32768));

    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: operator entry blocks discovered overwrite",
          "[aun][discovery][subscriber]") {
    AunBackend backend(0, 1, 0);
    backend.add_peer(0, 254, loopback_ip(), 40001,
                     PeerSource::OperatorConfigured);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("Beebium 0.254", 0, 254, 50001));

    auto peers = backend.list_peers();
    REQUIRE(peers.size() == 1);
    // Operator's port stays.
    CHECK(peers[0].port == 40001);
    CHECK(backend.is_operator_configured(0, 254));
}

TEST_CASE("AunDiscoverySubscriber: removed event drops the discovered peer",
          "[aun][discovery][subscriber]") {
    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    // A REMOTE peer (non-local IP): mDNS removal drops it, as ever.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, 32768, nonlocal_ip()));
    REQUIRE(backend.peer_count() == 1);

    subscriber.inject_removed("Beebium 0.254");
    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: removed event leaves operator peer alone",
          "[aun][discovery][subscriber]") {
    AunBackend backend(0, 1, 0);
    backend.add_peer(0, 254, loopback_ip(), 40001,
                     PeerSource::OperatorConfigured);

    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    // Pretend discovery saw the peer (operator entry blocks the add,
    // but the name->peer map should still be populated... actually no:
    // we don't insert into the name map if the add was a no-op. Test
    // the operator-survives behaviour: an injected remove for an
    // unknown name is harmless.)
    subscriber.inject_removed("Beebium 0.254");
    auto peers = backend.list_peers();
    REQUIRE(peers.size() == 1);
    CHECK(peers[0].port == 40001);
}

TEST_CASE("AunDiscoverySubscriber: malformed TXT does nothing",
          "[aun][discovery][subscriber]") {
    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    DiscoveredService svc;
    svc.instance_name = "broken";
    svc.ipv4_addr_net_byte_order = loopback_ip();
    svc.port = 32768;
    // No TXT records -- parse_txt rejects.
    subscriber.inject_added(svc);

    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: missing IPv4 address is skipped",
          "[aun][discovery][subscriber]") {
    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    auto svc = make_service("v6only", 0, 254, 32768, /*ip=*/0);
    subscriber.inject_added(svc);
    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: on_peers_changed fires on add and remove",
          "[aun][discovery][subscriber][callback]") {
    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    int call_count = 0;
    subscriber.set_on_peers_changed([&] { ++call_count; });

    // Remote peer, so the removal actually drops it and fires the callback.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, 32768, nonlocal_ip()));
    CHECK(call_count == 1);

    subscriber.inject_removed("Beebium 0.254");
    CHECK(call_count == 2);
}

TEST_CASE("AunDiscoverySubscriber: on_peers_changed skipped when operator pinned",
          "[aun][discovery][subscriber][callback]") {
    // Operator already pinned this peer, so add_peer is a no-op --
    // and the UI dirty-bump would be misleading (nothing changed).
    AunBackend backend(0, 1, 0);
    backend.add_peer(0, 254, loopback_ip(), 40001,
                     PeerSource::OperatorConfigured);

    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());
    int call_count = 0;
    subscriber.set_on_peers_changed([&] { ++call_count; });

    subscriber.inject_added(make_service("Beebium 0.254", 0, 254, 50001));
    CHECK(call_count == 0);
}

TEST_CASE("AunDiscoverySubscriber: on_peers_changed not invoked for self",
          "[aun][discovery][subscriber][callback]") {
    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());
    int call_count = 0;
    subscriber.set_on_peers_changed([&] { ++call_count; });

    // Our own announcement: same (net, stn) as backend.
    subscriber.inject_added(make_service("Beebium 0.1", 0, 1, 32768));
    CHECK(call_count == 0);
}

// =============================================================================
// Same-host (loopback) peer lifetime: governed by the bind-probe liveness
// sweep, not by mDNS removal. Survives a NIC toggle (mDNS withdrawal while the
// peer is still up on loopback); reaped only when the peer's server exits.
// =============================================================================

TEST_CASE("AunDiscoverySubscriber: same-host peer survives mDNS removal (NIC toggle)",
          "[aun][discovery][subscriber][samehost]") {
    // Stand up a real same-host "peer server" so its loopback port is bound.
    auto peer = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(peer->is_connected());
    const uint16_t peer_port = peer->local_port();

    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    // Discovered on loopback (same-host) -> add_peer reroutes to 127.0.0.1.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    REQUIRE(backend.peer_count() == 1);

    // mDNS withdraws the advertisement (Wi-Fi/Ethernet toggle). The peer is
    // still up on loopback, so it must be KEPT, not dropped.
    subscriber.inject_removed("Beebium 0.254");
    CHECK(backend.peer_count() == 1);

    // A liveness sweep while the peer is still bound leaves it in place.
    subscriber.sweep_once();
    CHECK(backend.peer_count() == 1);
}

TEST_CASE("AunDiscoverySubscriber: same-host peer reaped when its server exits",
          "[aun][discovery][subscriber][samehost]") {
    auto peer = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(peer->is_connected());
    const uint16_t peer_port = peer->local_port();

    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    REQUIRE(backend.peer_count() == 1);

    // Peer's server exits -> its loopback port frees. The sweep must reap it.
    peer.reset();
    subscriber.sweep_once();
    CHECK(backend.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: same-host re-add after removal reconciles in place",
          "[aun][discovery][subscriber][samehost]") {
    auto peer = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(peer->is_connected());
    const uint16_t peer_port = peer->local_port();

    AunBackend backend(0, 1, 0);
    AunDiscoverySubscriber subscriber(backend, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    subscriber.inject_removed("Beebium 0.254");        // kept (same-host)
    CHECK(backend.peer_count() == 1);

    // Wi-Fi returns -> mDNS re-adds the same instance. Must update in place,
    // not create a duplicate peer row.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    CHECK(backend.peer_count() == 1);
}
