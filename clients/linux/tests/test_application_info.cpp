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

#include "app/ApplicationInfo.hpp"

#include "beebium/service/ProtocolFingerprint.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("The client carries the servers' protocol fingerprint", "[app]") {
    CHECK(client_protocol_fingerprint() == beebium::service::PROTOCOL_FINGERPRINT);
    CHECK(client_protocol_fingerprint().size() == 64);
}

TEST_CASE("The version text names the version and the protocol", "[app]") {
    const std::string text = version_text();
    CHECK(text.find(std::string(client_version())) != std::string::npos);
    CHECK(text.find(std::string(client_protocol_fingerprint())) != std::string::npos);
}
