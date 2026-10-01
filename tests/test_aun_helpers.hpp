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
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// A fresh "_b<base36>._udp" service type each call, unique within this process
// (a counter) AND across processes (the pid). Cross-process uniqueness matters
// because ctest -j runs several AUN test binaries at once: a type built from a
// process-shared clock could collide between them, so two suites would browse
// the same type and discover each other's announcer -- the very contamination
// this isolates against. The pid cannot collide between concurrent live
// processes, so it carries the cross-process guarantee. The DNS-SD service
// name is capped at 15 characters; "_b" + base36(pid) + base36(counter) stays
// well within that.
inline std::string aun_unique_service_type() {
    static std::atomic<unsigned> counter{0};
    unsigned n = counter.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
    unsigned long long pid = static_cast<unsigned long long>(_getpid());
#else
    unsigned long long pid = static_cast<unsigned long long>(getpid());
#endif
    auto base36 = [](unsigned long long v) {
        static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::string s;
        do {
            s.insert(s.begin(), digits[v % 36]);
            v /= 36;
        } while (v != 0);
        return s;
    };
    return "_b" + base36(pid) + "z" + base36(n) + "._udp";
}

#endif  // BEEBIUM_TESTS_TEST_AUN_HELPERS_HPP
