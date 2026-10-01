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

#ifndef BEEBIUM_TESTS_TEST_AUN_HELPERS_HPP
#define BEEBIUM_TESTS_TEST_AUN_HELPERS_HPP

// Shared helper for AUN tests that bring up a real transport. Any test that
// calls AunEconetTransportExtension::create_backend starts a live mDNS
// announcer and subscriber on the default "_aun._udp" type. If anything else
// on this host or the LAN announces that type -- a developer's running Beebium
// app, a server, a colleague's machine -- the subscriber adopts it and the
// test sees extra discovered peers (issue #145). Giving each test its own
// unique service type isolates it: nothing else answers for that type, so the
// peer set stays exactly what the test configured, while real discovery is
// still exercised. Mirrors the per-test type the [.mdns] e2e tests use.
//
// Usage: ext.set_discovery_service_type(aun_unique_service_type()); before
// create_backend.

#include <atomic>
#include <chrono>
#include <string>

// A fresh "_bbt...._udp" service type each call, unique within this process and
// unlikely to collide across processes. The service-name label is capped at 15
// characters, which "_bbt" + a time suffix + a counter stays within.
inline std::string aun_unique_service_type() {
    static std::atomic<unsigned> counter{0};
    unsigned n = counter.fetch_add(1, std::memory_order_relaxed);
    auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    return "_bbt" +
           std::to_string(static_cast<unsigned long long>(t) % 100000ULL) +
           std::to_string(n) + "._udp";
}

#endif  // BEEBIUM_TESTS_TEST_AUN_HELPERS_HPP
