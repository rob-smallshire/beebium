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

#ifndef BEEBIUM_HOST_IDENTITY_HOST_FINGERPRINT_HPP
#define BEEBIUM_HOST_IDENTITY_HOST_FINGERPRINT_HPP

#include "beebium/host_identity/HostIdentifier.hpp"

#include <openssl/sha.h>

#include <array>
#include <string>
#include <string_view>

namespace beebium::host_identity {

// Domain separation, so this digest can only ever be compared against another
// host fingerprint and never collides with some other hash of the same input.
// Both ends of the wire must use this exact string, so it is part of the
// protocol: changing it makes every peer look like a different host.
inline constexpr char HOST_FINGERPRINT_DOMAIN[] = "beebium-host-v1:";

// The fingerprint of a given host identifier: SHA-256 over the domain prefix
// and the identifier, as lowercase hex. Separate from host_fingerprint() so the
// recipe can be checked against known values on any host.
inline std::string host_fingerprint_of(std::string_view identifier) {
    std::string input(HOST_FINGERPRINT_DOMAIN);
    input.append(identifier);
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(),
           digest.data());

    static constexpr char HEX[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (unsigned char byte : digest) {
        out.push_back(HEX[byte >> 4]);
        out.push_back(HEX[byte & 0x0F]);
    }
    return out;
}

// A stable, opaque token identifying this host, for comparison only.
//
// Derived from the platform's host identifier, but hashed rather than sent as
// it stands. The underlying value is a hardware or OS-installation identifier
// that outlives Beebium and identifies the machine to anything else that asks
// for it; a server answers this to any client that connects, so it should not
// be the means by which that identifier travels a network. A digest compares
// exactly as well.
//
// Returns an empty string if the host will not identify itself. Callers must
// read that as "unknown", never as "matches another empty one".
inline std::string host_fingerprint() {
    const auto identifier = beebium::platform::host_identifier();
    if (!identifier) {
        return {};
    }
    return host_fingerprint_of(*identifier);
}

}  // namespace beebium::host_identity

#endif  // BEEBIUM_HOST_IDENTITY_HOST_FINGERPRINT_HPP
