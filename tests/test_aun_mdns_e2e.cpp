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

// End-to-end test: two AunBackends, each with an announcer and a
// subscriber, no manual --aun map=. Confirms that mDNS discovery
// auto-populates each backend's peer table and that real AUN traffic
// then routes between them.
//
// This test exercises the full real-mDNS stack and depends on a
// working platform responder (Bonjour on macOS, dnsapi on Windows).
// It is tagged [.mdns] so an operator can skip it in environments
// without mDNS via Catch2's --skip "[.mdns]".

#include <catch2/catch_test_macros.hpp>

#include "AunDiscoveryAnnouncer.hpp"
#include "AunDiscoverySubscriber.hpp"
#include "AunEconetTransportExtension.hpp"

#include <beebium/discovery/Advertiser.hpp>
#include <beebium/discovery/Browser.hpp>
#include "AunPeerSet.hpp"

#include <beebium/econet/AunBackend.hpp>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <thread>

using namespace beebium;
using namespace beebium::discovery;
using namespace std::chrono_literals;

namespace {

bool platform_supports_mdns() {
    auto adv = create_advertiser();
    auto br = create_browser();
    return adv->state().available && br->state().available;
}

// Bound the polling so a hung subscription doesn't hang CI forever.
constexpr auto MDNS_TIMEOUT = 8s;
constexpr auto POLL_INTERVAL = 50ms;

// Pick station numbers from a tiny per-run-random set so concurrent
// CI shards don't shadow each other.
std::pair<uint8_t, uint8_t> pick_stations() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(2, 200);
    int a = dist(gen);
    int b;
    do { b = dist(gen); } while (b == a);
    return {static_cast<uint8_t>(a), static_cast<uint8_t>(b)};
}

// A DNS-SD service type unique to this process and call. Running each
// test case on its own type isolates it from production _aun._udp
// records (other Beebiums, stray daemons, prior crashed runs) and from
// the other test case, so discovery can't be starved by pollution. The
// service-name label is capped at 15 characters, which "_bbt" + a short
// time suffix + a call counter stays within.
std::string unique_service_type() {
    static std::atomic<unsigned> counter{0};
    unsigned n = counter.fetch_add(1, std::memory_order_relaxed);
    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    return "_bbt" + std::to_string(static_cast<unsigned long long>(t) % 100000ULL)
         + std::to_string(n) + "._udp";
}

}  // namespace

TEST_CASE("AUN mDNS e2e: peers discover each other on the same net, no map=",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }

    auto [stn_a, stn_b] = pick_stations();

    AunBackend backend_a(/*local_net=*/0, /*local_stn=*/stn_a, 0);
    AunBackend backend_b(/*local_net=*/0, /*local_stn=*/stn_b, 0);
    REQUIRE(backend_a.is_connected());
    REQUIRE(backend_b.is_connected());

    // The peer set is the discovery target and the routing authority; attaching
    // it applies its resolution to the live backend so real traffic routes.
    AunPeerSet peers_a;
    AunPeerSet peers_b;
    peers_a.attach(&backend_a);
    peers_b.attach(&backend_b);

    AunDiscoveryAnnouncer announce_a(0, stn_a, backend_a.local_port(),
                                     "beebium-test", "1.0", "");
    AunDiscoveryAnnouncer announce_b(0, stn_b, backend_b.local_port(),
                                     "beebium-test", "1.0", "");
    const std::string svc_type = unique_service_type();
    announce_a.set_service_type(svc_type);
    announce_b.set_service_type(svc_type);
    REQUIRE(announce_a.start());
    REQUIRE(announce_b.start());

    AunDiscoverySubscriber sub_a(peers_a, stn_a);
    AunDiscoverySubscriber sub_b(peers_b, stn_b);
    sub_a.set_service_type(svc_type);
    sub_b.set_service_type(svc_type);
    REQUIRE(sub_a.start());
    REQUIRE(sub_b.start());

    // Wait for both peer tables to learn the other side. We don't
    // require a particular ordering -- whichever gets there first is
    // fine, as long as both make it before the deadline.
    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    bool a_sees_b = false;
    bool b_sees_a = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!a_sees_b) {
            for (const auto& p : peers_a.list_peers()) {
                if (p.net == 0 && p.stn == stn_b
                        && p.port == backend_b.local_port()) {
                    a_sees_b = true;
                    break;
                }
            }
        }
        if (!b_sees_a) {
            for (const auto& p : peers_b.list_peers()) {
                if (p.net == 0 && p.stn == stn_a
                        && p.port == backend_a.local_port()) {
                    b_sees_a = true;
                    break;
                }
            }
        }
        if (a_sees_b && b_sees_a) break;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }

    REQUIRE(a_sees_b);
    REQUIRE(b_sees_a);

    // Discovery must mark them as Discovered, not OperatorConfigured.
    CHECK_FALSE(peers_a.is_operator_configured(0, stn_b));
    CHECK_FALSE(peers_b.is_operator_configured(0, stn_a));

    // Now drive real AUN traffic across the discovered route. The
    // BBC view sets dest_net=0; the backend translates to local_net
    // for lookup -- both sides on net 0 so it's a no-op here.
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.control_byte = 0x42;
    frame.dest_net = 0;
    frame.dest_stn = stn_b;
    frame.src_net = 0;
    frame.src_stn = stn_a;
    frame.data = {0xAA, 0xBB, 0xCC};

    backend_a.send_frame(frame);

    auto frame_deadline = std::chrono::steady_clock::now() + 1s;
    std::optional<NetworkFrame> received;
    while (std::chrono::steady_clock::now() < frame_deadline) {
        received = backend_b.receive_frame();
        if (received.has_value()) break;
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(received.has_value());
    CHECK(received->type == FrameType::Unicast);
    CHECK(received->port == 0x99);
    CHECK(received->control_byte == 0x42);
    CHECK(received->src_stn == stn_a);
    CHECK(received->dest_stn == stn_b);
    REQUIRE(received->data.size() == 3);
    CHECK(received->data[0] == 0xAA);
    CHECK(received->data[1] == 0xBB);
    CHECK(received->data[2] == 0xCC);
}

TEST_CASE("AUN mDNS e2e: cross-net discovery routes via local_net translation",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }

    auto [stn_a, stn_b] = pick_stations();

    // Different absolute nets; both sides should see each other as
    // cross-net peers.
    AunBackend backend_a(/*local_net=*/3, /*local_stn=*/stn_a, 0);
    AunBackend backend_b(/*local_net=*/5, /*local_stn=*/stn_b, 0);
    REQUIRE(backend_a.is_connected());
    REQUIRE(backend_b.is_connected());

    AunPeerSet peers_a;
    AunPeerSet peers_b;
    peers_a.set_local_net(3);
    peers_b.set_local_net(5);
    peers_a.attach(&backend_a);
    peers_b.attach(&backend_b);

    AunDiscoveryAnnouncer announce_a(3, stn_a, backend_a.local_port(),
                                     "beebium-test", "1.0", "");
    AunDiscoveryAnnouncer announce_b(5, stn_b, backend_b.local_port(),
                                     "beebium-test", "1.0", "");
    const std::string svc_type = unique_service_type();
    announce_a.set_service_type(svc_type);
    announce_b.set_service_type(svc_type);
    REQUIRE(announce_a.start());
    REQUIRE(announce_b.start());

    AunDiscoverySubscriber sub_a(peers_a, stn_a);
    AunDiscoverySubscriber sub_b(peers_b, stn_b);
    sub_a.set_service_type(svc_type);
    sub_b.set_service_type(svc_type);
    REQUIRE(sub_a.start());
    REQUIRE(sub_b.start());

    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    bool a_sees_b = false, b_sees_a = false;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& p : peers_a.list_peers()) {
            if (p.net == 5 && p.stn == stn_b) { a_sees_b = true; break; }
        }
        for (const auto& p : peers_b.list_peers()) {
            if (p.net == 3 && p.stn == stn_a) { b_sees_a = true; break; }
        }
        if (a_sees_b && b_sees_a) break;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }

    REQUIRE(a_sees_b);
    REQUIRE(b_sees_a);

    // BBC on A explicitly addresses cross-net peer with dest_net=5.
    NetworkFrame frame;
    frame.type = FrameType::Unicast;
    frame.port = 0x99;
    frame.dest_net = 5;
    frame.dest_stn = stn_b;
    frame.src_net = 0;
    frame.src_stn = stn_a;
    frame.data = {0xCC};

    backend_a.send_frame(frame);

    auto frame_deadline = std::chrono::steady_clock::now() + 1s;
    std::optional<NetworkFrame> received;
    while (std::chrono::steady_clock::now() < frame_deadline) {
        received = backend_b.receive_frame();
        if (received.has_value()) break;
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(received.has_value());
    // Cross-net: src_net delivered as 3 (A's absolute net), NOT
    // translated to 0 (because A is not on B's local net).
    CHECK(received->src_net == 3);
    CHECK(received->src_stn == stn_a);
    // dest_net is always delivered as 0: a BBC cannot learn its own net
    // number, so net 0 is the only form in which NFS recognises a frame as
    // its own (an absolute dest_net makes NFS discard every inbound frame
    // when --aun net= is non-zero -- see AunBackend::receive_frame).
    CHECK(received->dest_net == 0);
    CHECK(received->dest_stn == stn_b);
}

// Regression test for the "already-present peer" discovery gap: a
// station that starts browsing AFTER another is already advertising
// must still discover it (not only peers that arrive live while we are
// already browsing). This reproduces a real bug where the second
// machine launched never listed the first: the browser resolved the
// already-present peer but then issued its getaddrinfo on the single
// interface the browse Add arrived on, which could be one that never
// answers -- so the address callback never fired and the peer was
// dropped. The earlier same-net/cross-net cases start both peers at
// once, so both arrive as live Adds and never exercise this path.
//
// NOTE: this is a real-mDNS test ([.mdns]) and is therefore EXCLUDED
// from the default CTest run -- mDNS is not dependable in CI (no
// reliable responder, and stray records leak between runs). It is the
// behaviour-level guard for the interface-index regression, but it only
// runs when the [.mdns] suite is invoked explicitly on a machine with a
// working responder (e.g. a developer pre-release check or a dedicated
// mDNS job), NOT per-commit. There is no deterministic CI equivalent
// because BonjourBrowser calls dns_sd.h directly with no mockable seam;
// the bug was a wrong interface argument to a syscall whose behaviour
// depends on the host's live network interfaces.
TEST_CASE("AUN mDNS e2e: late subscriber discovers an already-present peer",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }

    auto [stn_a, stn_b] = pick_stations();
    const std::string svc_type = unique_service_type();

    // Peer A comes up first and is already advertising + browsing.
    AunBackend backend_a(/*local_net=*/0, /*local_stn=*/stn_a, 0);
    REQUIRE(backend_a.is_connected());
    AunPeerSet peers_a;
    peers_a.attach(&backend_a);
    AunDiscoveryAnnouncer announce_a(0, stn_a, backend_a.local_port(),
                                     "beebium-test", "1.0", "");
    announce_a.set_service_type(svc_type);
    REQUIRE(announce_a.start());
    AunDiscoverySubscriber sub_a(peers_a, stn_a);
    sub_a.set_service_type(svc_type);
    REQUIRE(sub_a.start());

    // Give A's advertisement time to propagate before B starts, so B
    // sees A as an already-present service at browse start rather than
    // as a live arrival.
    std::this_thread::sleep_for(2s);

    // Peer B starts second: it must discover the already-present A.
    AunBackend backend_b(/*local_net=*/0, /*local_stn=*/stn_b, 0);
    REQUIRE(backend_b.is_connected());
    AunPeerSet peers_b;
    peers_b.attach(&backend_b);
    AunDiscoveryAnnouncer announce_b(0, stn_b, backend_b.local_port(),
                                     "beebium-test", "1.0", "");
    announce_b.set_service_type(svc_type);
    REQUIRE(announce_b.start());
    AunDiscoverySubscriber sub_b(peers_b, stn_b);
    sub_b.set_service_type(svc_type);
    REQUIRE(sub_b.start());

    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    bool b_sees_a = false;
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& p : peers_b.list_peers()) {
            if (p.net == 0 && p.stn == stn_a
                    && p.port == backend_a.local_port()) {
                b_sees_a = true;
                break;
            }
        }
        if (b_sees_a) break;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }

    REQUIRE(b_sees_a);
}

namespace {

// Poll until pred() holds or the mDNS deadline passes.
template <typename Pred>
bool wait_until(Pred pred) {
    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    return pred();
}

bool has_peer(const AunPeerSet& peers, uint8_t net, uint8_t stn,
              uint16_t port) {
    for (const auto& p : peers.list_peers()) {
        if (p.net == net && p.stn == stn && p.port == port) return true;
    }
    return false;
}

}  // namespace

// Mark Moxon's field scenario (#68): a file server, an incumbent client, and a
// newcomer that starts on the incumbent's station number then changes it. After
// the fix all three coexist -- the server keeps the original client and gains
// the newcomer's new station, and the newcomer's stale advertisement orphans
// nobody.
TEST_CASE("AUN mDNS e2e: a station change keeps the incumbent and adopts the new number",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    const std::string svc_type = unique_service_type();
    constexpr uint8_t stn_server = 254;
    constexpr uint8_t stn_incumbent = 80;
    constexpr uint8_t stn_new = 81;

    // Server S and incumbent client A come up and find each other.
    AunBackend backend_s(0, stn_server, 0);
    AunBackend backend_a(0, stn_incumbent, 0);
    REQUIRE(backend_s.is_connected());
    REQUIRE(backend_a.is_connected());
    AunPeerSet peers_s;
    AunPeerSet peers_a;
    peers_s.attach(&backend_s);
    peers_a.attach(&backend_a);
    AunDiscoveryAnnouncer ann_s(0, stn_server, backend_s.local_port(),
                                "beebium-test", "1.0", "");
    AunDiscoveryAnnouncer ann_a(0, stn_incumbent, backend_a.local_port(),
                                "beebium-test", "1.0", "");
    ann_s.set_service_type(svc_type);
    ann_a.set_service_type(svc_type);
    REQUIRE(ann_s.start());
    REQUIRE(ann_a.start());
    AunDiscoverySubscriber sub_s(peers_s, stn_server);
    AunDiscoverySubscriber sub_a(peers_a, stn_incumbent);
    sub_s.set_service_type(svc_type);
    sub_a.set_service_type(svc_type);
    REQUIRE(sub_s.start());
    REQUIRE(sub_a.start());

    REQUIRE(wait_until([&] {
        return has_peer(peers_s, 0, stn_incumbent, backend_a.local_port());
    }));

    // The newcomer B comes up ALSO as station 80 -- the collision.
    AunBackend backend_b(0, stn_incumbent, 0);
    REQUIRE(backend_b.is_connected());
    AunPeerSet peers_b;
    peers_b.attach(&backend_b);
    AunDiscoveryAnnouncer ann_b(0, stn_incumbent, backend_b.local_port(),
                                "beebium-test", "1.0", "");
    ann_b.set_service_type(svc_type);
    REQUIRE(ann_b.start());
    AunDiscoverySubscriber sub_b(peers_b, stn_incumbent);
    sub_b.set_service_type(svc_type);
    REQUIRE(sub_b.start());

    // S refuses B's 80 and keeps A's: first live station wins.
    REQUIRE(wait_until([&] {
        return peers_s.station_collisions().count >= 1;
    }));
    {
        auto ep = peers_s.resolve(0, stn_incumbent);
        REQUIRE(ep.has_value());
        CHECK(ep->port == backend_a.local_port());  // still A, not B
    }

    // B changes its station to 81: re-announce and re-filter, exactly as the
    // transport extension's station-changed callback does.
    ann_b.set_local_station(stn_new);
    REQUIRE(ann_b.start());
    sub_b.set_local_station(stn_new);
    backend_b.on_station_id_changed(stn_new);

    // S now sees B at 0.81, and A's 0.80 survived untouched.
    REQUIRE(wait_until([&] {
        return has_peer(peers_s, 0, stn_new, backend_b.local_port());
    }));
    {
        auto ep = peers_s.resolve(0, stn_incumbent);
        REQUIRE(ep.has_value());
        CHECK(ep->port == backend_a.local_port());  // A untouched
    }
    // All three reachable: B has the server too.
    REQUIRE(wait_until([&] {
        return has_peer(peers_b, 0, stn_server, backend_s.local_port());
    }));

    // #138: the collision warning on the server clears once B's colliding 0.80
    // advertisement is withdrawn (B re-announced as 0.81). The report tracks
    // collisions IN EFFECT, so S's count returns to 0 on its own.
    REQUIRE(wait_until([&] {
        return peers_s.station_collisions().count == 0;
    }));
    CHECK(peers_s.station_collisions().last.empty());
}

// #147: when two instances claim the same number, each is told something
// different based on which bound first (the `since` stamp). The incumbent (A,
// earlier since) is told a claim was rejected; the newcomer (B, later since) is
// told the number is in use and to change its own. A third machine (S) watching
// both keeps the peer-vs-peer wording.
TEST_CASE("AUN mDNS e2e: own-number collision tells incumbent and newcomer apart",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    const std::string svc_type = unique_service_type();
    constexpr uint8_t stn_server = 254;
    constexpr uint8_t stn = 80;

    AunBackend backend_s(0, stn_server, 0);
    AunBackend backend_a(0, stn, 0);
    AunBackend backend_b(0, stn, 0);
    REQUIRE(backend_s.is_connected());
    REQUIRE(backend_a.is_connected());
    REQUIRE(backend_b.is_connected());
    AunPeerSet peers_s, peers_a, peers_b;
    peers_s.attach(&backend_s);
    peers_a.attach(&backend_a);
    peers_b.attach(&backend_b);

    // A bound first (since 1000) -> incumbent; B bound later (2000) -> newcomer.
    AunDiscoveryAnnouncer ann_s(0, stn_server, backend_s.local_port(),
                                "beebium-test", "1.0", "uuid-s");
    AunDiscoveryAnnouncer ann_a(0, stn, backend_a.local_port(),
                                "beebium-test", "1.0", "uuid-a");
    AunDiscoveryAnnouncer ann_b(0, stn, backend_b.local_port(),
                                "beebium-test", "1.0", "uuid-b");
    ann_a.set_since(1000);
    ann_b.set_since(2000);
    for (auto* a : {&ann_s, &ann_a, &ann_b}) a->set_service_type(svc_type);
    REQUIRE(ann_s.start());
    REQUIRE(ann_a.start());
    REQUIRE(ann_b.start());

    AunDiscoverySubscriber sub_s(peers_s, stn_server, nullptr, "uuid-s");
    AunDiscoverySubscriber sub_a(peers_a, stn, nullptr, "uuid-a");
    AunDiscoverySubscriber sub_b(peers_b, stn, nullptr, "uuid-b");
    sub_a.set_own_since(1000);
    sub_b.set_own_since(2000);
    for (auto* s : {&sub_s, &sub_a, &sub_b}) s->set_service_type(svc_type);
    REQUIRE(sub_s.start());
    REQUIRE(sub_a.start());
    REQUIRE(sub_b.start());

    // A sees B claim its number and, being the incumbent, reports a rejection.
    REQUIRE(wait_until([&] {
        return peers_a.station_collisions().count >= 1;
    }));
    CHECK(peers_a.station_collisions().last.find(
              "tried to claim station 0.80 and was rejected") !=
          std::string::npos);

    // B sees A hold the number and, being the newcomer, is told to renumber.
    REQUIRE(wait_until([&] {
        return peers_b.station_collisions().count >= 1;
    }));
    CHECK(peers_b.station_collisions().last.find(
              "Station 0.80 is already in use by") != std::string::npos);
    CHECK(peers_b.station_collisions().last.find("the next Break") !=
          std::string::npos);

    // The third machine watching two 0.80s keeps the peer-vs-peer wording.
    REQUIRE(wait_until([&] {
        return peers_s.station_collisions().count >= 1;
    }));
    CHECK(peers_s.station_collisions().last.find("rejected advertisement from") !=
          std::string::npos);
}

// #148, the user's exact sequence: two Station 80 instances and a Station 254
// file server; renumber the second 80 to 81. The machine that held 80 is seen
// by the renumbered machine only once -- as a claim on its own number while it
// was still 80 -- and mDNS never re-delivers it, so before the fix 80 never
// appeared in 81's peer list. After the fix all three see each other.
TEST_CASE("AUN mDNS e2e: a renumbered station adopts the holder of its old number",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    const std::string svc_type = unique_service_type();
    constexpr uint8_t stn_server = 254;
    constexpr uint8_t stn = 80;
    constexpr uint8_t stn_new = 81;

    // Server S and the incumbent client A come up first and find each other,
    // so A is unambiguously S's 0.80 (all three share this one host, so a
    // stale same-host entry would otherwise linger across B's renumber -- a
    // test-rig artefact absent on the real multi-host network). B is brought up
    // only after that.
    AunBackend backend_s(0, stn_server, 0);
    AunBackend backend_a(0, stn, 0);
    REQUIRE(backend_s.is_connected());
    REQUIRE(backend_a.is_connected());
    AunPeerSet peers_s, peers_a;
    peers_s.attach(&backend_s);
    peers_a.attach(&backend_a);
    AunDiscoveryAnnouncer ann_s(0, stn_server, backend_s.local_port(),
                                "beebium-test", "1.0", "uuid-s");
    AunDiscoveryAnnouncer ann_a(0, stn, backend_a.local_port(),
                                "beebium-test", "1.0", "uuid-a");
    ann_s.set_service_type(svc_type);
    ann_a.set_service_type(svc_type);
    REQUIRE(ann_s.start());
    REQUIRE(ann_a.start());
    AunDiscoverySubscriber sub_s(peers_s, stn_server, nullptr, "uuid-s");
    AunDiscoverySubscriber sub_a(peers_a, stn, nullptr, "uuid-a");
    sub_s.set_service_type(svc_type);
    sub_a.set_service_type(svc_type);
    REQUIRE(sub_s.start());
    REQUIRE(sub_a.start());
    REQUIRE(wait_until([&] {
        return has_peer(peers_s, 0, stn, backend_a.local_port());
    }));

    // The second Station 80 (B) comes up: a collision with A everywhere.
    AunBackend backend_b(0, stn, 0);
    REQUIRE(backend_b.is_connected());
    AunPeerSet peers_b;
    peers_b.attach(&backend_b);
    AunDiscoveryAnnouncer ann_b(0, stn, backend_b.local_port(),
                                "beebium-test", "1.0", "uuid-b");
    ann_b.set_service_type(svc_type);
    REQUIRE(ann_b.start());
    AunDiscoverySubscriber sub_b(peers_b, stn, nullptr, "uuid-b");
    sub_b.set_service_type(svc_type);
    REQUIRE(sub_b.start());

    // B (the second 80) must have seen A's 0.80 as an own-number collision
    // before it renumbers -- that is the announcement it must remember.
    REQUIRE(wait_until([&] {
        return peers_b.station_collisions().count >= 1;
    }));
    CHECK_FALSE(peers_b.resolve(0, stn).has_value());  // A not yet a peer of B

    // B renumbers to 81, exactly as the transport's station-changed callback
    // does: restamp since, re-announce, re-filter, tell the backend.
    ann_b.set_since(3000);
    ann_b.set_local_station(stn_new);
    REQUIRE(ann_b.start());
    sub_b.set_own_since(3000);
    sub_b.set_local_station(stn_new);
    backend_b.on_station_id_changed(stn_new);

    // The fix: B now has A at 0.80, routable -- adopted from the remembered
    // announcement with no re-delivery from mDNS.
    REQUIRE(wait_until([&] {
        return has_peer(peers_b, 0, stn, backend_a.local_port());
    }));
    CHECK(backend_b.is_reachable(0, stn));
    // B's own-number collision cleared when it moved off 80.
    REQUIRE(wait_until([&] {
        return peers_b.station_collisions().count == 0;
    }));

    // A sees B at its new 0.81, and the server sees both.
    REQUIRE(wait_until([&] {
        return has_peer(peers_a, 0, stn_new, backend_b.local_port());
    }));
    REQUIRE(wait_until([&] {
        return has_peer(peers_s, 0, stn, backend_a.local_port()) &&
               has_peer(peers_s, 0, stn_new, backend_b.local_port());
    }));
}

// #139: a map-file entry and discovery coexist. When the file pins a station
// to a different endpoint than the one a peer announces over mDNS, the file
// wins the routing and the disagreement is reported through the live collision
// set -- clearing when the announcement withdraws.
TEST_CASE("AUN mDNS e2e: a map-file entry wins over a discovered one and reports the disagreement",
          "[aun][discovery][e2e][.mdns]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    auto [stn_a, stn_b] = pick_stations();
    const std::string svc_type = unique_service_type();

    AunBackend backend_a(/*local_net=*/0, /*local_stn=*/stn_a, 0);
    AunBackend backend_b(/*local_net=*/0, /*local_stn=*/stn_b, 0);
    REQUIRE(backend_a.is_connected());
    REQUIRE(backend_b.is_connected());

    AunPeerSet peers_a;
    AunPeerSet peers_b;
    peers_a.attach(&backend_a);
    peers_b.attach(&backend_b);

    // A's map file pins B's station to a deliberately wrong port (1), so when
    // B's real announcement arrives it disagrees.
    peers_a.set_peer(0, stn_b, htonl(INADDR_LOOPBACK), 1,
                     AunPeerProvenance::MapFile);

    AunDiscoveryAnnouncer announce_b(0, stn_b, backend_b.local_port(),
                                     "beebium-test", "1.0", "");
    announce_b.set_service_type(svc_type);
    REQUIRE(announce_b.start());

    AunDiscoverySubscriber sub_a(peers_a, stn_a);
    sub_a.set_service_type(svc_type);
    REQUIRE(sub_a.start());

    // A discovers B and reports the disagreement; the file endpoint (port 1)
    // still wins the routing.
    REQUIRE(wait_until([&] {
        return peers_a.station_collisions().count >= 1;
    }));
    CHECK(peers_a.station_collisions().last.find("map file") !=
          std::string::npos);
    REQUIRE(peers_a.resolve(0, stn_b).has_value());
    CHECK(peers_a.resolve(0, stn_b)->port == 1);  // file wins

    // B withdraws (stop announcing): the disagreement clears on its own.
    announce_b.stop();
    REQUIRE(wait_until([&] {
        return peers_a.station_collisions().count == 0;
    }));
}

// Discovery mode (#158): only a browsing transport adopts a discovered peer.
// One publisher on `on`; a `browse` instance adopts it, while `announce` and
// `off` instances -- which never browse -- ignore it entirely.
TEST_CASE("AUN discovery mode: browse adopts a peer that announce and off ignore",
          "[.mdns][aun][discovery]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    const std::string svc = unique_service_type();

    auto make = [&](std::uint8_t station, const std::string& mode,
                    const std::string& uuid) {
        auto ext = std::make_unique<AunEconetTransportExtension>();
        ext->set_discovery_service_type(svc);
        ext->set_config({{"port", "0"}, {"net", "0"}, {"map-file", "none"},
                         {"discovery", mode}, {"machine_uuid", uuid}});
        auto backend = ext->create_backend(station);
        REQUIRE(backend != nullptr);
        REQUIRE(backend->is_connected());
        // Keep the backend alive for the test (the extension holds a non-owning
        // pointer; discovery runs regardless of who owns the socket).
        return std::pair{std::move(ext), std::move(backend)};
    };

    auto publisher = make(90, "on", "disc-pub");        // publishes 0.90
    auto browser = make(91, "browse", "disc-browse");   // browses, no publish
    auto announcer = make(92, "announce", "disc-announce");  // publishes, no browse
    auto off = make(93, "off", "disc-off");             // neither

    // The browser must discover and adopt the publisher's 0.90.
    bool adopted = false;
    auto deadline = std::chrono::steady_clock::now() + MDNS_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        if (browser.first->peer_set().resolve(0, 90).has_value()) {
            adopted = true;
            break;
        }
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    CHECK(adopted);

    // announce and off never browse, so they adopt nothing -- not even the
    // publisher's record that the browser just picked up.
    CHECK_FALSE(announcer.first->peer_set().resolve(0, 90).has_value());
    CHECK(announcer.first->peer_set().peer_count() == 0);
    CHECK_FALSE(off.first->peer_set().resolve(0, 90).has_value());
    CHECK(off.first->peer_set().peer_count() == 0);
}
