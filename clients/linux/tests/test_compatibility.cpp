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

#include "connection/Compatibility.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Equal protocol fingerprints are compatible", "[connection][protocol]") {
    CHECK(assess_protocol("abc", "abc") == ProtocolVerdict::compatible);
    CHECK(is_compatible(assess_protocol("abc", "abc")));
}

TEST_CASE("Different protocol fingerprints are a mismatch", "[connection][protocol]") {
    CHECK(assess_protocol("abc", "abd") == ProtocolVerdict::mismatch);
    CHECK_FALSE(is_compatible(ProtocolVerdict::mismatch));
}

TEST_CASE("A server reporting no protocol fingerprint counts as a mismatch",
          "[connection][protocol]") {
    CHECK(assess_protocol("", "abc") == ProtocolVerdict::server_reports_none);
    CHECK_FALSE(is_compatible(ProtocolVerdict::server_reports_none));
}

TEST_CASE("A server is local only when both host fingerprints are known and equal",
          "[connection][host]") {
    CHECK(is_server_local("f00d", "f00d"));
    CHECK_FALSE(is_server_local("f00d", "beef"));
}

TEST_CASE("Local-server gating fails closed on an unknown fingerprint",
          "[connection][host]") {
    CHECK_FALSE(is_server_local("", "f00d"));
    CHECK_FALSE(is_server_local("f00d", ""));
    CHECK_FALSE(is_server_local("", ""));
}
