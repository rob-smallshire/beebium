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
#include "AunPeerSet.hpp"

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
                               uint32_t ip = htonl(INADDR_LOOPBACK),
                               const std::string& impl_identity = "") {
    DiscoveredService svc;
    svc.instance_name = name;
    svc.hostname = "fake.local.";
    svc.ipv4_addr_net_byte_order = ip;
    svc.port = port;
    svc.txt_records["version"] = "1";
    svc.txt_records["net"] = std::to_string(net);
    svc.txt_records["station"] = std::to_string(stn);
    svc.txt_records["port"] = std::to_string(port);
    if (!impl_identity.empty()) {
        svc.txt_records["impl-identity"] = impl_identity;
    }
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
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, /*local_stn=*/1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("Beebium 0.254", 0, 254, 32768));

    auto list = peers.list_peers();
    REQUIRE(list.size() == 1);
    CHECK(list[0].net == 0);
    CHECK(list[0].stn == 254);
    CHECK(list[0].port == 32768);
    CHECK_FALSE(peers.is_operator_configured(0, 254));
}

TEST_CASE("AunDiscoverySubscriber: skips own announcement",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    peers.set_local_net(3);
    AunDiscoverySubscriber subscriber(peers, /*local_stn=*/254,
                                      std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");

    // Our own announcement, reflected back by Bonjour (possibly under a renamed
    // instance name), carries our impl-identity -- recognised as ourselves.
    subscriber.inject_added(
        make_service("Beebium 3.254 (2)", 3, 254, 32768,
                     htonl(INADDR_LOOPBACK), "our-uuid"));

    CHECK(peers.peer_count() == 0);
    CHECK(peers.station_collisions().count == 0);  // not a collision -- it's us
}

TEST_CASE("AunDiscoverySubscriber: operator entry blocks discovered overwrite",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    peers.set_peer(0, 254, loopback_ip(), 40001,
                     AunPeerProvenance::Launch);
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("Beebium 0.254", 0, 254, 50001));

    auto list = peers.list_peers();
    REQUIRE(list.size() == 1);
    // Operator's port stays.
    CHECK(list[0].port == 40001);
    CHECK(peers.is_operator_configured(0, 254));
}

TEST_CASE("AunDiscoverySubscriber: removed event drops the discovered peer",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    // A REMOTE peer (non-local IP): mDNS removal drops it, as ever.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, 32768, nonlocal_ip()));
    REQUIRE(peers.peer_count() == 1);

    subscriber.inject_removed("Beebium 0.254");
    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: removed event leaves operator peer alone",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    peers.set_peer(0, 254, loopback_ip(), 40001,
                     AunPeerProvenance::Launch);

    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    // Pretend discovery saw the peer (operator entry blocks the add,
    // but the name->peer map should still be populated... actually no:
    // we don't insert into the name map if the add was a no-op. Test
    // the operator-survives behaviour: an injected remove for an
    // unknown name is harmless.)
    subscriber.inject_removed("Beebium 0.254");
    auto list = peers.list_peers();
    REQUIRE(list.size() == 1);
    CHECK(list[0].port == 40001);
}

TEST_CASE("AunDiscoverySubscriber: malformed TXT does nothing",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    DiscoveredService svc;
    svc.instance_name = "broken";
    svc.ipv4_addr_net_byte_order = loopback_ip();
    svc.port = 32768;
    // No TXT records -- parse_txt rejects.
    subscriber.inject_added(svc);

    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: missing IPv4 address is skipped",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    auto svc = make_service("v6only", 0, 254, 32768, /*ip=*/0);
    subscriber.inject_added(svc);
    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: on_peers_changed fires on add and remove",
          "[aun][discovery][subscriber][callback]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
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
    AunPeerSet peers;
    peers.set_peer(0, 254, loopback_ip(), 40001,
                     AunPeerProvenance::Launch);

    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());
    int call_count = 0;
    subscriber.set_on_peers_changed([&] { ++call_count; });

    subscriber.inject_added(make_service("Beebium 0.254", 0, 254, 50001));
    CHECK(call_count == 0);
}

TEST_CASE("AunDiscoverySubscriber: on_peers_changed not invoked for self",
          "[aun][discovery][subscriber][callback]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());
    int call_count = 0;
    subscriber.set_on_peers_changed([&] { ++call_count; });

    // Our own announcement: same (net, stn) as the peer set's local station.
    subscriber.inject_added(make_service("Beebium 0.1", 0, 1, 32768));
    CHECK(call_count == 0);
}

TEST_CASE("AunDiscoverySubscriber: set_local_station updates the self-filter",
          "[aun][discovery][subscriber]") {
    // We are station 0.80. Use remote (non-local) endpoints so the same-host
    // sweep does not enter into it.
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80,
                                      std::make_unique<FakeBrowser>());

    // An advertisement for 0.80 is us -> self-filtered.
    subscriber.inject_added(make_service("Beebium 0.80", 0, 80, 40001, nonlocal_ip()));
    CHECK(peers.peer_count() == 0);

    // The guest's station changes to 81. Now 0.80 is a different machine and
    // must be accepted as a peer, while 0.81 is us and must be filtered.
    subscriber.set_local_station(81);

    subscriber.inject_added(make_service("Beebium 0.80", 0, 80, 40001, nonlocal_ip()));
    CHECK(peers.peer_count() == 1);

    subscriber.inject_added(make_service("Beebium 0.81", 0, 81, 40002, nonlocal_ip()));
    CHECK(peers.peer_count() == 1);  // 0.81 is now us -> still filtered
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

    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    // Discovered on loopback (same-host) -> add_peer reroutes to 127.0.0.1.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    REQUIRE(peers.peer_count() == 1);

    // mDNS withdraws the advertisement (Wi-Fi/Ethernet toggle). The peer is
    // still up on loopback, so it must be KEPT, not dropped.
    subscriber.inject_removed("Beebium 0.254");
    CHECK(peers.peer_count() == 1);

    // A liveness sweep while the peer is still bound leaves it in place.
    subscriber.sweep_once();
    CHECK(peers.peer_count() == 1);
}

TEST_CASE("AunDiscoverySubscriber: same-host peer reaped when its server exits",
          "[aun][discovery][subscriber][samehost]") {
    auto peer = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(peer->is_connected());
    const uint16_t peer_port = peer->local_port();

    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    REQUIRE(peers.peer_count() == 1);

    // Peer's server exits -> its loopback port frees. The sweep must reap it.
    peer.reset();
    subscriber.sweep_once();
    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: same-host re-add after removal reconciles in place",
          "[aun][discovery][subscriber][samehost]") {
    auto peer = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(peer->is_connected());
    const uint16_t peer_port = peer->local_port();

    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    subscriber.inject_removed("Beebium 0.254");        // kept (same-host)
    CHECK(peers.peer_count() == 1);

    // Wi-Fi returns -> mDNS re-adds the same instance. Must update in place,
    // not create a duplicate peer row.
    subscriber.inject_added(
        make_service("Beebium 0.254", 0, 254, peer_port, loopback_ip()));
    CHECK(peers.peer_count() == 1);
}

// =============================================================================
// Station-number collisions: first live station wins. A discovered
// advertisement for a (net, stn) already held by a different, still-live
// instance must not displace the incumbent (see #68).
// =============================================================================

TEST_CASE("AunDiscoverySubscriber: a colliding station is rejected and reported",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    // Incumbent: station 0.254 from machine A (remote endpoint).
    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    REQUIRE(peers.peer_count() == 1);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 40001);

    // Newcomer B advertises the same 0.254 at a different endpoint -> collision.
    subscriber.inject_added(make_service("B 0.254", 0, 254, 50002,
                                         htonl(0xCB007102u)));

    // Incumbent A is kept; B is not adopted; the collision is reported.
    CHECK(peers.peer_count() == 1);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 40001);
    auto report = peers.station_collisions();
    CHECK(report.count == 1);
    CHECK(report.last.find("0.254") != std::string::npos);
}

TEST_CASE("AunDiscoverySubscriber: withdrawing a colliding advertisement leaves the incumbent",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    subscriber.inject_added(make_service("B 0.254", 0, 254, 50002,
                                         htonl(0xCB007102u)));
    REQUIRE(peers.peer_count() == 1);

    // B (the rejected collider) is withdrawn -- it never owned the entry, so
    // this must remove nothing.
    subscriber.inject_removed("B 0.254");
    CHECK(peers.peer_count() == 1);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 40001);  // still A

    // A's own withdrawal removes A.
    subscriber.inject_removed("A 0.254");
    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: same-instance re-advertisement updates in place",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    REQUIRE(peers.peer_count() == 1);

    // The SAME instance re-advertises at a new port (an ephemeral-port change):
    // not a collision; the endpoint updates in place with no duplicate.
    subscriber.inject_added(make_service("A 0.254", 0, 254, 40009, nonlocal_ip()));
    CHECK(peers.peer_count() == 1);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 40009);
    CHECK(peers.station_collisions().count == 0);
}

TEST_CASE("AunDiscoverySubscriber: a parked collider is adopted when the incumbent leaves",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    subscriber.inject_added(make_service("B 0.254", 0, 254, 50002,
                                         htonl(0xCB007102u)));  // refused -> parked
    REQUIRE(peers.peer_count() == 1);
    CHECK(peers.resolve(0, 254)->port == 40001);  // A holds it

    // A leaves -> the parked newcomer B is adopted for 0.254.
    subscriber.inject_removed("A 0.254");
    REQUIRE(peers.peer_count() == 1);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 50002);  // now B
}

TEST_CASE("AunDiscoverySubscriber: a parked collider is adopted after the incumbent is reaped",
          "[aun][discovery][subscriber][collision]") {
    // Incumbent A is a real same-host backend so the sweep can probe its port.
    auto peer_a = std::make_unique<AunBackend>(0, 254, 0);
    REQUIRE(peer_a->is_connected());
    const uint16_t port_a = peer_a->local_port();
    // Newcomer B: a second live same-host port.
    auto peer_b = std::make_unique<AunBackend>(0, 99, 0);
    REQUIRE(peer_b->is_connected());
    const uint16_t port_b = peer_b->local_port();

    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, port_a, loopback_ip()));
    REQUIRE(peers.peer_count() == 1);
    subscriber.inject_added(make_service("B 0.254", 0, 254, port_b, loopback_ip()));
    REQUIRE(peers.peer_count() == 1);
    CHECK(peers.resolve(0, 254)->port == port_a);  // still A

    // A's server exits (free its port); the sweep reaps A and adopts B.
    peer_a.reset();
    subscriber.sweep_once();
    REQUIRE(peers.peer_count() == 1);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == port_b);  // now B
}

TEST_CASE("AunDiscoverySubscriber: a parked collider withdrawn before the incumbent leaves is not adopted",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    subscriber.inject_added(make_service("B 0.254", 0, 254, 50002,
                                         htonl(0xCB007102u)));  // parked
    subscriber.inject_removed("B 0.254");                       // withdrawn

    // A leaves: nothing is waiting to be adopted.
    subscriber.inject_removed("A 0.254");
    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunDiscoverySubscriber: a different instance claiming our own station is reported",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80,
                                      std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");

    // Our own announcement -- even reflected back under a Bonjour-renamed
    // instance name -- is recognised by our impl-identity and skipped silently.
    subscriber.inject_added(make_service("Beebium 0.80 (2)", 0, 80, 40000,
                                         nonlocal_ip(), "our-uuid"));
    CHECK(peers.peer_count() == 0);
    CHECK(peers.station_collisions().count == 0);

    // A DIFFERENT instance (a different identity) claiming 0.80 is flagged as a
    // collision, and never adopted -- we do not yield our own number.
    subscriber.inject_added(make_service("Beebium 0.80", 0, 80, 50000,
                                         nonlocal_ip(), "other-uuid"));
    CHECK(peers.peer_count() == 0);
    auto report = peers.station_collisions();
    CHECK(report.count == 1);
    // The claimant carries no `since`, so it is treated as the incumbent and we
    // are the newcomer (#147).
    CHECK(report.last.find("already in use") != std::string::npos);
}

// #147: an own-number collision tells the newcomer (we bound later) the number
// is taken and the incumbent (we bound first) that a claim was rejected. The
// role is decided by comparing the bind-time `since` each side advertises.
TEST_CASE("AunDiscoverySubscriber: own-number collision -- we are the newcomer",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80, std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");
    subscriber.set_own_since(2000);  // we bound later
    auto svc = make_service("Beebium 0.80", 0, 80, 50000, nonlocal_ip(),
                            "other-uuid");
    svc.txt_records["since"] = "1000";  // claimant bound first -> incumbent
    subscriber.inject_added(svc);
    auto report = peers.station_collisions();
    REQUIRE(report.count == 1);
    CHECK(report.last.find("Station 0.80 is already in use by") !=
          std::string::npos);
    CHECK(report.last.find("the next Break") != std::string::npos);
}

TEST_CASE("AunDiscoverySubscriber: own-number collision -- we are the incumbent",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80, std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");
    subscriber.set_own_since(1000);  // we bound first
    auto svc = make_service("Beebium 0.80", 0, 80, 50000, nonlocal_ip(),
                            "other-uuid");
    svc.txt_records["since"] = "2000";  // claimant bound later -> newcomer
    subscriber.inject_added(svc);
    auto report = peers.station_collisions();
    REQUIRE(report.count == 1);
    CHECK(report.last.find("tried to claim station 0.80 and was rejected") !=
          std::string::npos);
}

TEST_CASE("AunDiscoverySubscriber: own-number collision -- a tie breaks on identity",
          "[aun][discovery][subscriber][collision]") {
    // Same `since`: the smaller impl-identity is the incumbent. Both machines
    // compare the same two strings and so reach opposite, agreeing roles.
    SECTION("our identity is the smaller -> we are the incumbent") {
        AunPeerSet peers;
        AunDiscoverySubscriber subscriber(peers, 80,
                                          std::make_unique<FakeBrowser>(),
                                          /*own_identity=*/"aaa");
        subscriber.set_own_since(1000);
        auto svc = make_service("Beebium 0.80", 0, 80, 50000, nonlocal_ip(),
                                "zzz");
        svc.txt_records["since"] = "1000";
        subscriber.inject_added(svc);
        CHECK(peers.station_collisions().last.find("was rejected") !=
              std::string::npos);
    }
    SECTION("our identity is the larger -> we are the newcomer") {
        AunPeerSet peers;
        AunDiscoverySubscriber subscriber(peers, 80,
                                          std::make_unique<FakeBrowser>(),
                                          /*own_identity=*/"zzz");
        subscriber.set_own_since(1000);
        auto svc = make_service("Beebium 0.80", 0, 80, 50000, nonlocal_ip(),
                                "aaa");
        svc.txt_records["since"] = "1000";
        subscriber.inject_added(svc);
        CHECK(peers.station_collisions().last.find("already in use") !=
              std::string::npos);
    }
}

TEST_CASE("AunDiscoverySubscriber: own-number collision -- a claimant with no since is the incumbent",
          "[aun][discovery][subscriber][collision]") {
    // Another implementation that does not publish `since` counts as the
    // incumbent, so we (even having bound first by wall clock) are told we are
    // the newcomer -- we cannot prove we were first to a peer that is silent.
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80, std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");
    subscriber.set_own_since(1000);
    subscriber.inject_added(make_service("Other 0.80", 0, 80, 50000,
                                         nonlocal_ip(), "other-uuid"));
    CHECK(peers.station_collisions().last.find("already in use") !=
          std::string::npos);
}

TEST_CASE("AunDiscoverySubscriber: with no identity, our number is skipped silently",
          "[aun][discovery][subscriber][collision]") {
    // A subscriber with no own identity cannot tell its reflection from another
    // machine, so it falls back to the conservative silent self-filter.
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80,
                                      std::make_unique<FakeBrowser>());
    subscriber.inject_added(make_service("Beebium 0.80", 0, 80, 40000, nonlocal_ip()));
    CHECK(peers.peer_count() == 0);
    CHECK(peers.station_collisions().count == 0);
}

// =============================================================================
// Collision report clears when the collision is no longer in effect (#138).
// The count is the set currently in effect, not a running total.
// =============================================================================

TEST_CASE("AunDiscoverySubscriber: a peer-vs-peer collision clears when the collider is withdrawn",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    subscriber.inject_added(make_service("B 0.254", 0, 254, 50002,
                                         htonl(0xCB007102u)));  // parked -> collision
    CHECK(peers.station_collisions().count == 1);

    // The collider leaves: the collision is no longer in effect.
    subscriber.inject_removed("B 0.254");
    CHECK(peers.station_collisions().count == 0);
    CHECK(peers.station_collisions().last.empty());
}

TEST_CASE("AunDiscoverySubscriber: a collision clears when the collider re-announces under a new number",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    subscriber.inject_added(make_service("A 0.254", 0, 254, 40001, nonlocal_ip()));
    subscriber.inject_added(make_service("B 0.254", 0, 254, 50002,
                                         htonl(0xCB007102u)));
    CHECK(peers.station_collisions().count == 1);

    // B picks a new number: mDNS withdraws its old (station-embedding) name and
    // announces a new one. The withdrawal clears the collision; the new name is
    // an ordinary, non-colliding peer.
    subscriber.inject_removed("B 0.254");
    CHECK(peers.station_collisions().count == 0);
    subscriber.inject_added(make_service("B 0.253", 0, 253, 50002,
                                         htonl(0xCB007102u)));
    CHECK(peers.station_collisions().count == 0);
    CHECK(peers.peer_count() == 2);  // A at 254 and B at 253
}

TEST_CASE("AunDiscoverySubscriber: an own-number collision clears when the claimant leaves",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80,
                                      std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");

    // A different instance claims our station 0.80.
    subscriber.inject_added(make_service("X 0.80", 0, 80, 50000,
                                         nonlocal_ip(), "other-uuid"));
    CHECK(peers.station_collisions().count == 1);
    CHECK(peers.station_collisions().last.find("already in use") !=
          std::string::npos);

    // The claimant leaves: our number is no longer contested.
    subscriber.inject_removed("X 0.80");
    CHECK(peers.station_collisions().count == 0);
    CHECK(peers.station_collisions().last.empty());
}

TEST_CASE("AunDiscoverySubscriber: an own-number collision clears when this machine changes its number",
          "[aun][discovery][subscriber][collision]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 80,
                                      std::make_unique<FakeBrowser>(),
                                      /*own_identity=*/"our-uuid");

    subscriber.inject_added(make_service("X 0.80", 0, 80, 50000,
                                         nonlocal_ip(), "other-uuid"));
    CHECK(peers.station_collisions().count == 1);

    // We move to 81: the claimant of our former 80 no longer collides with us.
    subscriber.set_local_station(81);
    CHECK(peers.station_collisions().count == 0);
}

// =============================================================================
// File-vs-discovered disagreement (#139): the map file wins, and the conflict
// is reported through the live collision set with a "map file" description.
// =============================================================================

TEST_CASE("AunDiscoverySubscriber: a file-vs-discovered disagreement is raised and clears on withdrawal",
          "[aun][discovery][subscriber][map-file]") {
    AunPeerSet peers;
    // The map file pins 0.254 at 10.0.0.1:32768.
    peers.set_peer(0, 254, htonl(0x0A000001u), 32768, AunPeerProvenance::MapFile);
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());

    // A discovered announcement for 0.254 at a DIFFERENT endpoint.
    subscriber.inject_added(make_service("X 0.254", 0, 254, 40001, nonlocal_ip()));
    CHECK(peers.station_collisions().count == 1);
    CHECK(peers.station_collisions().last.find("map file") != std::string::npos);
    // The file still wins the routing.
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 32768);

    subscriber.inject_removed("X 0.254");
    CHECK(peers.station_collisions().count == 0);
}

TEST_CASE("AunDiscoverySubscriber: a disagreement clears when the file is revalidated to agree",
          "[aun][discovery][subscriber][map-file]") {
    AunPeerSet peers;
    peers.set_peer(0, 254, htonl(0x0A000001u), 32768, AunPeerProvenance::MapFile);
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());
    subscriber.inject_added(make_service("X 0.254", 0, 254, 40001, nonlocal_ip()));
    CHECK(peers.station_collisions().count == 1);

    // The map file is edited so it no longer pins 0.254; a reload revalidates.
    peers.remove_peer(0, 254, AunPeerProvenance::MapFile);
    subscriber.revalidate_file_disagreements();
    CHECK(peers.station_collisions().count == 0);
}

TEST_CASE("AunDiscoverySubscriber: no disagreement when the file agrees with the announcement",
          "[aun][discovery][subscriber][map-file]") {
    AunPeerSet peers;
    // The file pins 0.254 at the SAME endpoint the peer advertises.
    peers.set_peer(0, 254, nonlocal_ip(), 40001, AunPeerProvenance::MapFile);
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());
    subscriber.inject_added(make_service("X 0.254", 0, 254, 40001, nonlocal_ip()));
    CHECK(peers.station_collisions().count == 0);  // agree -> no conflict
}

TEST_CASE("AunDiscoverySubscriber: sweep_once fires the on_sweep hook",
          "[aun][discovery][subscriber]") {
    AunPeerSet peers;
    AunDiscoverySubscriber subscriber(peers, 1,
                                      std::make_unique<FakeBrowser>());
    int sweeps = 0;
    subscriber.set_on_sweep([&] { ++sweeps; });
    subscriber.sweep_once();
    subscriber.sweep_once();
    CHECK(sweeps == 2);
}
