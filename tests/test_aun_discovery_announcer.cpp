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

// Unit tests for AunDiscoveryAnnouncer. Use a fake Advertiser that
// captures the ServiceInfo without touching the platform mDNS
// responder so the tests are deterministic and CI-portable.

#include <catch2/catch_test_macros.hpp>

#include "AunDiscoveryAnnouncer.hpp"
#include <beebium/discovery/Advertiser.hpp>

#include <memory>
#include <utility>

using namespace beebium;
using namespace beebium::discovery;

namespace {

// Observable state held in the test, not in the FakeAdvertiser, so it
// outlives the announcer's destruction of the advertiser.
struct FakeState {
    ServiceInfo last_info;
    int start_count = 0;
    int stop_count = 0;
    bool advertising = false;
    bool fail_start = false;
};

class FakeAdvertiser final : public Advertiser {
public:
    explicit FakeAdvertiser(FakeState* s) : s_(s) {}

    bool start(const ServiceInfo& info) override {
        s_->last_info = info;
        s_->start_count++;
        if (s_->fail_start) {
            s_->advertising = false;
            return false;
        }
        s_->advertising = true;
        return true;
    }

    void stop() override {
        s_->stop_count++;
        s_->advertising = false;
    }

    AdvertiserState state() const override {
        return AdvertiserState{
            .available = true,
            .advertising = s_->advertising,
            .actual_name = "captured",
        };
    }

private:
    FakeState* s_;
};

}  // namespace

TEST_CASE("AunDiscoveryAnnouncer: build_service_info populates schema",
          "[aun][discovery][announcer]") {
    FakeState s;
    AunDiscoveryAnnouncer announcer(/*net=*/3, /*stn=*/254, /*port=*/32768,
                                    "beebium", "1.2.3",
                                    "11111111-2222-3333-4444-555555555555",
                                    std::make_unique<FakeAdvertiser>(&s));
    auto info = announcer.build_service_info();
    CHECK(info.service_type == "_aun._udp");
    CHECK(info.port == 32768);
    CHECK(info.instance_name == "Beebium 3.254");
    REQUIRE(info.txt_records.count("version") == 1);
    CHECK(info.txt_records.at("version") == "1");
    CHECK(info.txt_records.at("net") == "3");
    CHECK(info.txt_records.at("station") == "254");
    CHECK(info.txt_records.at("port") == "32768");
    CHECK(info.txt_records.at("impl") == "beebium");
    CHECK(info.txt_records.at("impl-version") == "1.2.3");
    CHECK(info.txt_records.at("impl-identity")
          == "11111111-2222-3333-4444-555555555555");
    // since is the bind-time stamp (#147): present, a non-negative integer.
    REQUIRE(info.txt_records.count("since") == 1);
    CHECK(info.txt_records.at("since").find_first_not_of("0123456789") ==
          std::string::npos);
    CHECK(std::stoll(info.txt_records.at("since")) > 0);
}

TEST_CASE("AunDiscoveryAnnouncer: set_since overrides the advertised since; "
          "set_local_station is a fresh claim the transport restamps",
          "[aun][discovery][announcer]") {
    FakeState s;
    AunDiscoveryAnnouncer announcer(/*net=*/0, /*stn=*/80, /*port=*/32768,
                                    "beebium", "1.2.3", "",
                                    std::make_unique<FakeAdvertiser>(&s));
    announcer.set_since(1000);
    CHECK(announcer.since() == 1000);
    CHECK(announcer.build_service_info().txt_records.at("since") == "1000");
    // A station change is a fresh claim: the transport stamps a new since and
    // the announcer advertises it.
    announcer.set_since(2000);
    announcer.set_local_station(81);
    CHECK(announcer.build_service_info().txt_records.at("since") == "2000");
}

TEST_CASE("AunDiscoveryAnnouncer: set_local_station re-announces with the new station",
          "[aun][discovery][announcer]") {
    FakeState s;
    AunDiscoveryAnnouncer announcer(/*net=*/0, /*stn=*/80, /*port=*/32768,
                                    "beebium", "1.2.3", "",
                                    std::make_unique<FakeAdvertiser>(&s));
    REQUIRE(announcer.start());
    CHECK(s.last_info.instance_name == "Beebium 0.80");
    CHECK(s.last_info.txt_records.at("station") == "80");

    // The guest's station changed to 81: update, then re-publish. start() is
    // idempotent -- it withdraws the old station's announcement first.
    announcer.set_local_station(81);
    REQUIRE(announcer.start());
    CHECK(s.last_info.instance_name == "Beebium 0.81");
    CHECK(s.last_info.txt_records.at("station") == "81");
    // Re-published (the Advertiser::start contract withdraws the old name and
    // publishes the new one).
    CHECK(s.start_count == 2);
}

TEST_CASE("AunDiscoveryAnnouncer: omits empty optional TXT entries",
          "[aun][discovery][announcer]") {
    FakeState s;
    AunDiscoveryAnnouncer announcer(/*net=*/0, /*stn=*/1, /*port=*/40000,
                                    "", "", "",
                                    std::make_unique<FakeAdvertiser>(&s));
    auto info = announcer.build_service_info();
    CHECK(info.txt_records.count("impl") == 0);
    CHECK(info.txt_records.count("impl-version") == 0);
    CHECK(info.txt_records.count("impl-identity") == 0);
    // Mandatory schema entries still present.
    CHECK(info.txt_records.at("version") == "1");
    CHECK(info.txt_records.at("net") == "0");
    CHECK(info.txt_records.at("station") == "1");
    CHECK(info.txt_records.at("port") == "40000");
}

TEST_CASE("AunDiscoveryAnnouncer: impl-identity is independent of impl-version",
          "[aun][discovery][announcer]") {
    // impl-identity present, impl-version empty: advertise identity
    // alone (a vendor that doesn't track versions still gets a stable
    // per-instance handle).
    FakeState s;
    AunDiscoveryAnnouncer announcer(0, 1, 40000, "beebium", "",
                                    "deadbeef-feed-face-1234-cafebabe5678",
                                    std::make_unique<FakeAdvertiser>(&s));
    auto info = announcer.build_service_info();
    CHECK(info.txt_records.count("impl-version") == 0);
    CHECK(info.txt_records.at("impl-identity")
          == "deadbeef-feed-face-1234-cafebabe5678");
    CHECK(info.txt_records.at("impl") == "beebium");
}

TEST_CASE("AunDiscoveryAnnouncer: start invokes advertiser with ServiceInfo",
          "[aun][discovery][announcer]") {
    FakeState s;
    AunDiscoveryAnnouncer announcer(5, 200, 32769, "beebium", "0.1", "",
                                    std::make_unique<FakeAdvertiser>(&s));
    REQUIRE(announcer.start());

    CHECK(s.start_count == 1);
    CHECK(s.last_info.service_type == "_aun._udp");
    CHECK(s.last_info.port == 32769);
    CHECK(s.last_info.txt_records.at("net") == "5");
    CHECK(s.last_info.txt_records.at("station") == "200");
    CHECK(announcer.is_advertising());
}

TEST_CASE("AunDiscoveryAnnouncer: failure propagates as false",
          "[aun][discovery][announcer]") {
    FakeState s;
    s.fail_start = true;

    AunDiscoveryAnnouncer announcer(0, 1, 32768, "beebium", "0.1", "",
                                    std::make_unique<FakeAdvertiser>(&s));
    CHECK_FALSE(announcer.start());
    CHECK(s.start_count == 1);
    CHECK_FALSE(announcer.is_advertising());
}

TEST_CASE("AunDiscoveryAnnouncer: destructor stops advertising",
          "[aun][discovery][announcer]") {
    FakeState s;
    {
        AunDiscoveryAnnouncer announcer(0, 1, 32768, "beebium", "0.1", "",
                                        std::make_unique<FakeAdvertiser>(&s));
        REQUIRE(announcer.start());
        REQUIRE(s.advertising);
    }
    // After destruction the advertiser saw a stop().
    CHECK(s.stop_count >= 1);
    CHECK_FALSE(s.advertising);
}
