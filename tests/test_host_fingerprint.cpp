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

// The host fingerprint recipe, shared by servers and clients. Both ends must
// derive it byte for byte alike or every peer reads as another host, so the
// recipe is pinned to known digests rather than only to itself.

#include "beebium/host_identity/HostFingerprint.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using beebium::host_identity::host_fingerprint;
using beebium::host_identity::host_fingerprint_of;

namespace {

bool is_lowercase_sha256_hex(const std::string& text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

}  // namespace

TEST_CASE("host_fingerprint_of hashes the domain prefix and the identifier",
          "[host_identity]") {
    // Computed independently: printf '<input>' | shasum -a 256
    CHECK(host_fingerprint_of("0123456789abcdef0123456789abcdef") ==
          "15914c4f37b82c1b291e342373d4bf6dc4bec5fbdfb4381ae1ddb818df06b8d0");
    CHECK(host_fingerprint_of("") ==
          "fddd92f8a9271a5a1e6688f13ec6d0aa6d1a6fef9ed442f855b0fe8a8b03eeda");
}

TEST_CASE("host_fingerprint is the fingerprint of this host's identifier",
          "[host_identity]") {
    const auto identifier = beebium::platform::host_identifier();
    // Every supported platform has somewhere to look; nullopt here means the
    // lookup broke, not that the OS declined.
    REQUIRE(identifier.has_value());

    const auto fingerprint = host_fingerprint();
    CHECK(is_lowercase_sha256_hex(fingerprint));
    CHECK(fingerprint == host_fingerprint_of(*identifier));
    CHECK(fingerprint == host_fingerprint());
}
