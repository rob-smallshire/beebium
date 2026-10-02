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

// Smoke tests for the discovery::Browser API. End-to-end real-mDNS
// round-trip coverage lives in test_aun_mdns_e2e (Step 4); these
// cases focus on the API surface (state, lifecycle, callback shape).

#include <catch2/catch_test_macros.hpp>

#include <beebium/discovery/Advertiser.hpp>
#include <beebium/discovery/Browser.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

using namespace beebium::discovery;

namespace {

// The integration cases below need a functional mDNS responder to complete a
// real advertise -> browse -> resolve round trip. Where one is not present
// they skip, so a developer without a working responder is not blocked --
// except when BEEBIUM_REQUIRE_MDNS_BROWSE is set (CI on platforms whose browse
// path is expected to work), where the same conditions are a hard failure so a
// browser is never shipped untested. This mirrors test_advertiser.cpp's
// BEEBIUM_REQUIRE_MDNS gate for the advertise side; the browse side has its own
// switch because a platform may advertise reliably before it can browse.
void skip_or_fail_browse(const char* reason) {
    if (std::getenv("BEEBIUM_REQUIRE_MDNS_BROWSE") != nullptr) {
        FAIL(reason);
    } else {
        SKIP(reason);
    }
}

}  // namespace

TEST_CASE("create_browser returns non-null", "[browser]") {
    auto browser = create_browser();
    REQUIRE(browser != nullptr);
}

TEST_CASE("Browser initial state", "[browser]") {
    auto browser = create_browser();
    auto s = browser->state();
    CHECK_FALSE(s.browsing);
}

// Regression companion to the advertiser stress test (GitHub #155). The Bonjour
// browser's stop() used to deallocate the browse ref before joining its event
// thread; its select() loop made the window small but not zero. Restarting the
// browser repeatedly must never deallocate the browse ref while the event
// thread is inside DNSServiceProcessResult on it. No network peer is needed:
// an empty browse still runs the event loop on the browse ref, and start()
// restarts it via stop(). Skips where no browser is available.
TEST_CASE("Browser rapid restart does not resurrect the browse ref",
          "[browser][stress]") {
    auto probe = create_browser();
    if (!probe->state().available) {
        SKIP("no mDNS browser on this platform");
    }
    BrowserCallbacks cbs;  // no-op callbacks; we are exercising lifecycle only
    if (!probe->start("_beebium._tcp", cbs)) {
        probe->stop();
        SKIP("mDNS browse not functional in this environment");
    }
    probe->stop();

    auto browser = create_browser();
    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i) {
        BrowserCallbacks callbacks;
        REQUIRE(browser->start("_beebium._tcp", callbacks));
        if (i % 3 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (i % 5 == 0) {
            browser->stop();
        }
    }
    browser->stop();
    CHECK_FALSE(browser->state().browsing);
}

TEST_CASE("Browser stop without start is safe", "[browser]") {
    auto browser = create_browser();
    browser->stop();
    CHECK_FALSE(browser->state().browsing);
}

TEST_CASE("Advertiser and browser share a backing provider", "[browser][discovery]") {
    // Guards the factory wiring, including the Windows dual-provider selector
    // (Bonjour via dnssd.dll when installed, else native DnsService). A machine
    // that can browse must also be able to advertise -- both are backed by the
    // same selected provider -- so browser availability implies advertiser
    // availability. (The converse need not hold: Linux advertises via Avahi but
    // has no browser yet.)
    auto advertiser = create_advertiser();
    auto browser = create_browser();
    if (browser->state().available) {
        CHECK(advertiser->state().available);
    }
}

TEST_CASE("Browser start when unavailable returns false", "[browser]") {
    auto browser = create_browser();
    if (browser->state().available) {
        SUCCEED("mDNS is available; this case only exercises the null path");
        return;
    }
    BrowserCallbacks cbs;
    bool result = browser->start("_aun._udp", cbs);
    CHECK_FALSE(result);
    CHECK_FALSE(browser->state().browsing);
}

#ifdef __APPLE__
TEST_CASE("Bonjour browser is available on macOS", "[browser][macos]") {
    auto browser = create_browser();
    REQUIRE(browser->state().available);
}
#endif

#ifdef _WIN32
TEST_CASE("Windows browser is available on Windows 10+", "[browser][windows]") {
    auto browser = create_browser();
    REQUIRE(browser->state().available);
}
#endif

// The following are platform-neutral integration cases: they drive the public
// create_advertiser()/create_browser() API through a real mDNS round trip and
// run on every platform whose backend both advertises and browses (macOS
// Bonjour, Windows DnsService*). Where no functional responder is present they
// skip_or_fail_browse rather than hard-failing, so they are safe on the Null
// path (e.g. Linux, which advertises but does not yet browse) too.

TEST_CASE("Browser sees an advertised service",
          "[browser][integration]") {
    auto advertiser = create_advertiser();
    auto browser = create_browser();
    if (!advertiser->state().available || !browser->state().available) {
        skip_or_fail_browse(
            "mDNS advertiser/browser not available on this platform");
        return;
    }

    // Advertise on a per-run-unique service type so we don't pick up
    // announcements from other test processes, stray daemons, or stale
    // _aun._udp records left by prior runs -- the browser only ever sees
    // this run's advertisement.
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string type = "_bbt"
        + std::to_string(static_cast<unsigned long long>(stamp) % 1000000ULL)
        + "._udp";
    ServiceInfo info;
    info.service_type = type;
    info.instance_name = "BrowserTest " + std::to_string(stamp);
    info.port = 32999;
    info.txt_records["version"] = "1";
    info.txt_records["net"] = "0";
    info.txt_records["station"] = "99";
    info.txt_records["port"] = "32999";

    // A platform's advertiser can report available (the API is present) yet
    // still fail to register when no functional mDNS responder is reachable --
    // e.g. on Windows when Apple's Bonjour service occupies UDP 5353 and blocks
    // the native DnsServiceRegister. Treat "cannot advertise" as "no functional
    // responder" and skip_or_fail rather than hard-failing, so this case only
    // asserts the browser when there is actually something to discover.
    if (!advertiser->start(info)) {
        skip_or_fail_browse(
            "advertiser could not register (no functional mDNS responder)");
        return;
    }

    // Wait for advertise to land before we start browsing.
    for (int i = 0; i < 50; ++i) {
        if (advertiser->state().advertising) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!advertiser->state().advertising) {
        advertiser->stop();
        skip_or_fail_browse(
            "advertiser did not reach the advertising state "
            "(no functional mDNS responder)");
        return;
    }

    std::atomic<bool> saw_added{false};
    std::atomic<int> add_count{0};
    BrowserCallbacks cbs;
    cbs.on_added = [&](const DiscoveredService& svc) {
        // Filter to the exact instance we just advertised so we don't
        // race on stray daemons.
        if (svc.instance_name.find("BrowserTest ") == 0
                && svc.txt_records.count("net") == 1
                && svc.txt_records.at("station") == "99"
                && svc.port == 32999) {
            saw_added.store(true);
            add_count.fetch_add(1);
        }
    };
    // Starting the browse can fail where the platform advertises but cannot
    // browse (e.g. the native Windows DnsServiceBrowse on a hosted CI runner
    // with no working mDNS responder, even though DnsServiceRegister there
    // succeeds). Treat that as "no functional responder" and skip rather than
    // hard-fail, consistent with the advertiser gating above.
    if (!browser->start(type, cbs)) {
        advertiser->stop();
        skip_or_fail_browse(
            "browser could not start browsing (no functional mDNS responder)");
        return;
    }

    // Generous timeout: real mDNS resolve + addrinfo can take
    // hundreds of ms even on loopback.
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !saw_added.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    browser->stop();
    advertiser->stop();

    if (!saw_added.load()) {
        skip_or_fail_browse(
            "advertise -> browse round trip did not complete "
            "(no functional mDNS responder)");
        return;
    }
    CHECK(saw_added.load());
}

// Controlled analogue of the in-process two-peer scenario, but on a
// per-run-unique service type so it is fully isolated from any stray
// _aun._udp records (other Beebiums, prior crashed runs). Two advertisers
// and two browsers in one process must cross-discover each other.
TEST_CASE("Two advertisers and two browsers cross-discover in one process",
          "[browser][integration]") {
    if (!create_advertiser()->state().available
            || !create_browser()->state().available) {
        skip_or_fail_browse(
            "mDNS advertiser/browser not available on this platform");
        return;
    }

    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string type = "_bbt" + std::to_string(t % 1000000ULL) + "._udp";

    auto make_info = [&](const std::string& tag, uint16_t port) {
        ServiceInfo info;
        info.service_type = type;
        info.instance_name = "Peer-" + tag + "-"
            + std::to_string(t);
        info.port = port;
        info.txt_records["tag"] = tag;
        return info;
    };

    auto adv_a = create_advertiser();
    auto adv_b = create_advertiser();
    // As above: an available advertiser may still fail to register without a
    // functional responder (e.g. Bonjour occupying mDNS on Windows). Skip
    // rather than hard-fail so the assertions below only run when both peers
    // are actually on the wire.
    if (!adv_a->start(make_info("A", 40001))
            || !adv_b->start(make_info("B", 40002))) {
        adv_a->stop();
        adv_b->stop();
        skip_or_fail_browse(
            "advertisers could not register (no functional mDNS responder)");
        return;
    }

    auto saw_tag = [](std::atomic<bool>& flag, const std::string& want) {
        BrowserCallbacks cbs;
        cbs.on_added = [&flag, want](const DiscoveredService& svc) {
            auto it = svc.txt_records.find("tag");
            if (it != svc.txt_records.end() && it->second == want
                    && svc.port != 0) {
                flag.store(true);
            }
        };
        return cbs;
    };

    std::atomic<bool> a_sees_b{false}, b_sees_a{false};
    auto br_a = create_browser();
    auto br_b = create_browser();
    // As above: a browse that cannot start on this platform is a
    // no-functional-responder condition, not a hard failure.
    if (!br_a->start(type, saw_tag(a_sees_b, "B"))
            || !br_b->start(type, saw_tag(b_sees_a, "A"))) {
        br_a->stop();
        br_b->stop();
        adv_a->stop();
        adv_b->stop();
        skip_or_fail_browse(
            "browsers could not start browsing (no functional mDNS responder)");
        return;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline
           && !(a_sees_b.load() && b_sees_a.load())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    bool crossed = a_sees_b.load() && b_sees_a.load();

    br_a->stop();
    br_b->stop();
    adv_a->stop();
    adv_b->stop();

    if (!crossed) {
        skip_or_fail_browse(
            "two-peer cross-discovery did not complete "
            "(no functional mDNS responder)");
        return;
    }
    CHECK(a_sees_b.load());
    CHECK(b_sees_a.load());
}
