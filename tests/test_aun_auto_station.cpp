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

// Live, hermetic tests for AUN automatic station selection (issue #67):
// AunEconetTransportExtension::select_auto_station browses `_aun._udp`,
// gathers the numbers in use (discovered, map-file and launch peers -- not
// subnet guesses), takes the lowest free one, and settles a same-second race
// so instances launched together land on distinct numbers.
//
// Each test uses a fresh per-process service type (aun_unique_service_type)
// so a build box's real AUN peers, and other test binaries under ctest -j,
// cannot contaminate it. Tagged [.mdns]: skipped where no responder is present.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "test_aun_helpers.hpp"

#include <beebium/discovery/Advertiser.hpp>
#include <beebium/discovery/Browser.hpp>
#include <beebium/econet/StationSelection.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace beebium;
using namespace std::chrono_literals;

namespace {

using Status = EconetTransportExtension::AutoStationOutcome::Status;

bool platform_supports_mdns() {
    auto adv = discovery::create_advertiser();
    auto br = discovery::create_browser();
    return adv->state().available && br->state().available;
}

// Selection timings sized for real mDNS propagation (first resolve can take
// about a second, and is variable). Larger than the launch defaults: a live
// test must give announcements time to cross between instances.
constexpr auto kMinObserve = 6000ms;  // observe before settling (mDNS is slow)
constexpr auto kQuiet = 1000ms;       // quiet period after the last move
constexpr auto kBudget = 15000ms;     // hard cap on the whole selection

std::unique_ptr<AunEconetTransportExtension> make_auto_ext(
    const std::string& service_type, const std::string& uuid,
    std::map<std::string, std::string> extra = {}) {
    auto ext = std::make_unique<AunEconetTransportExtension>();
    ext->set_discovery_service_type(service_type);
    ext->set_auto_station_timings_for_test(kMinObserve, kQuiet, kBudget);
    std::map<std::string, std::string> config{
        {"port", "0"}, {"net", "0"}, {"map-file", "none"}, {"machine_uuid", uuid}};
    for (auto& [k, v] : extra) {
        config[k] = v;
    }
    ext->set_config(std::move(config));
    return ext;
}

}  // namespace

TEST_CASE("AUN auto station: map-file peers at 80 and 81 push the choice to 82",
          "[.mdns][aun][auto-station]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    std::random_device rd;
    auto map_filepath = std::filesystem::temp_directory_path() /
                        ("beebium-auto-map-" + std::to_string(rd()) + ".json");
    {
        std::ofstream out(map_filepath, std::ios::binary | std::ios::trunc);
        out << R"({"peers":[
            {"net":0,"station":80,"host":"127.0.0.1","port":10000,"label":"a"},
            {"net":0,"station":81,"host":"127.0.0.1","port":10001,"label":"b"}
        ],"subnets":[]})";
    }

    auto ext = make_auto_ext(aun_unique_service_type(), "uuid-mapfile",
                             {{"map-file", map_filepath.string()}});
    auto outcome = ext->select_auto_station(econet::StationRange{80, 253});

    CHECK(outcome.status == Status::Selected);
    CHECK(int(outcome.station) == 82);

    std::error_code ec;
    std::filesystem::remove(map_filepath, ec);
}

TEST_CASE("AUN auto station: a discovered station 80 is taken, so auto takes 81",
          "[.mdns][aun][auto-station]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    const std::string svc_type = aun_unique_service_type();

    // A real station 80 on the same service type: bring its backend up so it
    // announces a usable endpoint the auto instance will discover. Its identity
    // sorts BEFORE the auto instance's so that, even if both carry the same
    // whole-second `since`, the #147 tie-break makes the fixed station the
    // incumbent and the auto instance the newcomer that must move.
    auto fixed = std::make_unique<AunEconetTransportExtension>();
    fixed->set_discovery_service_type(svc_type);
    fixed->set_config({{"port", "0"}, {"net", "0"}, {"map-file", "none"},
                       {"machine_uuid", "uuid-aaa-fixed-80"}});
    auto backend = fixed->create_backend(80);
    REQUIRE(backend != nullptr);
    REQUIRE(backend->is_connected());

    auto ext = make_auto_ext(svc_type, "uuid-zzz-auto");
    auto outcome = ext->select_auto_station(econet::StationRange{80, 253});

    CHECK(outcome.status == Status::Selected);
    CHECK(int(outcome.station) == 81);
}

TEST_CASE("AUN auto station: three instances launched together end on distinct numbers",
          "[.mdns][aun][auto-station]") {
    if (!platform_supports_mdns()) {
        SKIP("mDNS responder not available on this platform");
    }
    const std::string svc_type = aun_unique_service_type();

    std::vector<std::unique_ptr<AunEconetTransportExtension>> exts;
    for (int i = 0; i < 3; ++i) {
        exts.push_back(
            make_auto_ext(svc_type, "uuid-race-" + std::to_string(i)));
    }

    // Start the three a couple of seconds apart, as three clients launched in
    // quick succession are: each keeps its claim up continuously, so a later
    // instance reliably discovers the earlier ones within its observation
    // window and climbs past them. uuid-race-0 sorts first, so it is the
    // incumbent at each contested number -- the ordering is deterministic.
    std::vector<std::uint8_t> chosen(3, 0);
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&, i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000 * i));
            auto outcome =
                exts[i]->select_auto_station(econet::StationRange{80, 253});
            chosen[i] = outcome.station;
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    std::set<std::uint8_t> distinct(chosen.begin(), chosen.end());
    INFO("chosen = " << int(chosen[0]) << ", " << int(chosen[1]) << ", "
                     << int(chosen[2]));
    CHECK(distinct.size() == 3);
    for (auto s : chosen) {
        CHECK(int(s) >= 80);
        CHECK(int(s) <= 253);
    }
}
