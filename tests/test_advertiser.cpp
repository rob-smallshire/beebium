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

#include <catch2/catch_test_macros.hpp>
#include <beebium/discovery/Advertiser.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

using namespace beebium::discovery;

TEST_CASE("create_advertiser returns non-null", "[advertiser]") {
    auto advertiser = create_advertiser();
    REQUIRE(advertiser != nullptr);
}

TEST_CASE("Advertiser initial state", "[advertiser]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();

    SECTION("Not advertising initially") {
        REQUIRE_FALSE(state.advertising);
    }

    SECTION("Advertised name is empty initially") {
        REQUIRE(state.actual_name.empty());
    }
}

TEST_CASE("Advertiser stop when not started is safe", "[advertiser]") {
    auto advertiser = create_advertiser();
    // Should not crash or throw
    advertiser->stop();
    auto state = advertiser->state();
    REQUIRE_FALSE(state.advertising);
}

// Regression for GitHub #155: restarting a running advertiser must not
// deallocate the DNS-SD service ref while its event thread is still inside a
// DNS-SD call on it. The Bonjour advertiser used to deallocate the ref before
// joining the event thread; macOS 14 tolerated the race but macOS 26 aborts
// the process ("API MISUSE: Resurrection of an object" in libdispatch). The
// trigger in the field is an AUN re-announce on a station change, which calls
// advertiser->start() again -- and start() restarts by calling stop().
//
// This hammers that restart path: one driver repeatedly calls start() with a
// changing ServiceInfo (each start() restarts the live event loop from the
// previous start), interleaved with explicit stop()/start() pairs. It must run
// to completion without crashing, and is clean under ASan/TSan. It needs no
// network peer -- registering the service locally is enough -- but it does need
// a functional local mDNS responder, so it skips where one is absent (e.g. a
// Linux host without avahi-daemon, or Windows without Bonjour).
TEST_CASE("Advertiser rapid restart does not resurrect the service ref",
          "[advertiser][stress]") {
    auto probe = create_advertiser();
    if (!probe->state().available) {
        SKIP("no mDNS advertiser on this platform");
    }
    ServiceInfo warmup;
    warmup.instance_name = "Beebium Restart Probe";
    warmup.service_type = "_beebium._tcp";
    warmup.port = 48875;
    if (!probe->start(warmup)) {
        probe->stop();
        SKIP("mDNS responder not functional in this environment");
    }
    probe->stop();

    auto advertiser = create_advertiser();
    constexpr int kIterations = 200;
    for (int i = 0; i < kIterations; ++i) {
        ServiceInfo info;
        // A changing instance name mirrors a station renumber re-announce.
        info.instance_name = "Beebium Stress " + std::to_string(i);
        info.service_type = "_beebium._tcp";
        info.port = static_cast<uint16_t>(48875 + (i % 7));
        info.txt_records["uuid"] = "stress-" + std::to_string(i);
        info.txt_records["seq"] = std::to_string(i);

        REQUIRE(advertiser->start(info));

        // Let the event loop actually enter DNSServiceProcessResult on this
        // ref before the next start() restarts it -- that is the window the
        // old ordering crashed in. Vary it so some restarts land mid-call.
        if (i % 3 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // Occasionally restart via an explicit stop()/start() pair rather than
        // start()'s implicit restart, to cover both orderings.
        if (i % 5 == 0) {
            advertiser->stop();
        }
    }
    advertiser->stop();
    REQUIRE_FALSE(advertiser->state().advertising);

    // A concurrent driver thread stands in for the gRPC thread issuing the
    // re-announce while the advertiser is already running. Calls on a single
    // advertiser stay serialized (the real re-announce holds discovery_mutex_),
    // so the driver owns the object for the whole burst; the point is that the
    // restart races the advertiser's OWN event thread, not two callers.
    auto driver = create_advertiser();
    std::atomic<bool> go{false};
    std::thread worker([&] {
        while (!go.load()) { /* spin until released */ }
        for (int i = 0; i < kIterations; ++i) {
            ServiceInfo info;
            info.instance_name = "Beebium Worker " + std::to_string(i);
            info.service_type = "_beebium._tcp";
            info.port = 48875;
            driver->start(info);
            if (i % 4 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        driver->stop();
    });
    go.store(true);
    worker.join();
    REQUIRE_FALSE(driver->state().advertising);
}

TEST_CASE("Advertiser start when unavailable returns false", "[advertiser]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();

    if (!state.available) {
        // NullAdvertiser or unavailable platform
        ServiceInfo info;
        info.instance_name = "Test Machine";
        info.port = 48875;

        bool result = advertiser->start(info);
        REQUIRE_FALSE(result);
        REQUIRE_FALSE(advertiser->state().advertising);
    }
}

#ifdef __APPLE__
TEST_CASE("BonjourAdvertiser is available on macOS", "[advertiser][macos]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();
    REQUIRE(state.available);
}

// Helper to wait for advertising to become active (async callback)
static bool wait_for_advertising(Advertiser& advertiser, int timeout_ms = 1000) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (advertiser.state().advertising) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }
    return false;
}

TEST_CASE("BonjourAdvertiser can start and stop", "[advertiser][macos]") {
    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Beebium Test";
    info.port = 48875;
    info.txt_records["uuid"] = "test-uuid-1234";
    info.txt_records["model"] = "model-b";

    SECTION("Start advertising") {
        bool result = advertiser->start(info);
        REQUIRE(result);

        // Wait for async registration callback
        REQUIRE(wait_for_advertising(*advertiser));

        auto state = advertiser->state();
        REQUIRE(state.advertising);
        REQUIRE_FALSE(state.actual_name.empty());
    }

    SECTION("Stop advertising") {
        advertiser->start(info);
        wait_for_advertising(*advertiser);
        advertiser->stop();

        auto state = advertiser->state();
        REQUIRE_FALSE(state.advertising);
    }

    SECTION("Restart advertising") {
        advertiser->start(info);
        wait_for_advertising(*advertiser);
        advertiser->stop();

        bool result = advertiser->start(info);
        REQUIRE(result);
        REQUIRE(wait_for_advertising(*advertiser));
    }
}
#endif

#ifdef _WIN32
TEST_CASE("WindowsAdvertiser is available on Windows 10+", "[advertiser][windows]") {
    auto advertiser = create_advertiser();
    auto state = advertiser->state();
    // The API is available, though the service may not be running in all environments
    REQUIRE(state.available);
}

// Helper to wait for advertising to become active (async callback)
static bool wait_for_advertising_windows(Advertiser& advertiser, int timeout_ms = 2000) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (advertiser.state().advertising) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }
    return false;
}

// Helper to check if mDNS is functional (may not be on CI environments)
static bool is_mdns_functional() {
    auto advertiser = create_advertiser();
    ServiceInfo info;
    info.instance_name = "mDNS Test";
    info.port = 12345;
    bool result = advertiser->start(info);
    advertiser->stop();
    return result;
}

TEST_CASE("WindowsAdvertiser can start and stop", "[advertiser][windows]") {
    if (!is_mdns_functional()) {
        SKIP("mDNS service not available in this environment");
    }

    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Beebium Test";
    info.port = 48875;
    info.txt_records["uuid"] = "test-uuid-1234";
    info.txt_records["model"] = "model-b";

    SECTION("Start advertising") {
        bool result = advertiser->start(info);
        REQUIRE(result);

        // Wait for async registration callback
        REQUIRE(wait_for_advertising_windows(*advertiser));

        auto state = advertiser->state();
        REQUIRE(state.advertising);
        REQUIRE_FALSE(state.actual_name.empty());
    }

    SECTION("Stop advertising") {
        advertiser->start(info);
        wait_for_advertising_windows(*advertiser);
        advertiser->stop();

        auto state = advertiser->state();
        REQUIRE_FALSE(state.advertising);
    }

    SECTION("Restart advertising") {
        advertiser->start(info);
        wait_for_advertising_windows(*advertiser);
        advertiser->stop();

        bool result = advertiser->start(info);
        REQUIRE(result);
        REQUIRE(wait_for_advertising_windows(*advertiser));
    }
}

TEST_CASE("WindowsAdvertiser handles empty TXT records", "[advertiser][windows]") {
    if (!is_mdns_functional()) {
        SKIP("mDNS service not available in this environment");
    }

    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Minimal Service";
    info.port = 12345;
    // No TXT records

    bool result = advertiser->start(info);
    REQUIRE(result);
    REQUIRE(wait_for_advertising_windows(*advertiser));

    advertiser->stop();
}
#endif

#ifdef __linux__
// On Linux the advertiser is Avahi-backed when the build linked the Avahi
// implementation (BEEBIUM_HAS_AVAHI) and libavahi-client.so.3 is present at
// runtime; otherwise create_advertiser() returns NullAdvertiser, which the
// platform-agnostic cases above already cover.
//
// Verifying the advertise path additionally needs a running avahi-daemon (with
// a D-Bus system bus). Where Avahi or the daemon is absent these cases skip, so
// a developer without Avahi is not blocked -- except when BEEBIUM_REQUIRE_MDNS
// is set (CI), where the same conditions are a hard failure so the Linux
// advertiser is never shipped untested.

static bool wait_for_advertising_linux(Advertiser& advertiser,
                                       int timeout_ms = 3000) {
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (advertiser.state().advertising) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed += 10;
    }
    return false;
}

// Probe whether Avahi mDNS is actually functional here: the implementation is
// linked and libavahi-client.so.3 loads (state.available), a D-Bus system bus
// is reachable (start succeeds), and avahi-daemon registers the service
// (advertising goes true). Any missing piece means the advertise path cannot be
// exercised on this host.
static bool is_mdns_functional() {
    auto advertiser = create_advertiser();
    if (!advertiser->state().available) {
        return false;
    }
    ServiceInfo info;
    info.instance_name = "Beebium mDNS Probe";
    info.port = 12345;
    if (!advertiser->start(info)) {
        advertiser->stop();
        return false;
    }
    bool advertising = wait_for_advertising_linux(*advertiser);
    advertiser->stop();
    return advertising;
}

// Skip when mDNS is not functional, unless BEEBIUM_REQUIRE_MDNS demands it be
// exercised (CI), in which case turn the skip into a failure so the Linux
// advertiser is never shipped untested.
static void skip_or_fail_mdns(const char* reason) {
    if (std::getenv("BEEBIUM_REQUIRE_MDNS") != nullptr) {
        FAIL(reason);
    } else {
        SKIP(reason);
    }
}

TEST_CASE("AvahiAdvertiser advertises when Avahi is available",
          "[advertiser][linux]") {
    if (!is_mdns_functional()) {
        skip_or_fail_mdns(
            "Avahi mDNS not functional (libavahi, D-Bus, or avahi-daemon absent)");
        return;
    }

    auto advertiser = create_advertiser();

    ServiceInfo info;
    info.instance_name = "Beebium Test";
    info.port = 48875;
    info.txt_records["uuid"] = "test-uuid-1234";
    info.txt_records["model"] = "model-b";

    SECTION("Start advertising") {
        REQUIRE(advertiser->start(info));
        REQUIRE(wait_for_advertising_linux(*advertiser));

        auto state = advertiser->state();
        REQUIRE(state.advertising);
        REQUIRE_FALSE(state.actual_name.empty());
        advertiser->stop();
    }

    SECTION("Stop advertising") {
        advertiser->start(info);
        wait_for_advertising_linux(*advertiser);
        advertiser->stop();

        REQUIRE_FALSE(advertiser->state().advertising);
    }

    SECTION("Restart advertising") {
        advertiser->start(info);
        wait_for_advertising_linux(*advertiser);
        advertiser->stop();

        REQUIRE(advertiser->start(info));
        REQUIRE(wait_for_advertising_linux(*advertiser));
        advertiser->stop();
    }
}
#endif  // __linux__
