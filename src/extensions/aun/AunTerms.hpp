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

#ifndef BEEBIUM_AUN_TERMS_HPP
#define BEEBIUM_AUN_TERMS_HPP

// The words for the parts of an AUN peer and subnet rule, written for someone
// who does not already know Econet or AUN. The transport's manifest gives
// them to its map and subnet fields, which a configuration UI shows before
// launch, and the Network sidebar's editors show them on a running machine,
// so the two say the same.

namespace beebium::aun_terms {

struct Term {
    const char* label;
    const char* description;
};

inline constexpr Term kPeerNet{
    "Econet net",
    "The Econet net the station is on. Net 0 means this machine's own net."};

inline constexpr Term kPeerStation{
    "Econet station",
    "The station's number on its net, from 1 to 254; 254 is the usual file server."};

inline constexpr Term kPeerHost{
    "AUN host (IP address or name)",
    "The IPv4 address or DNS name of the computer running the AUN station. A name is looked "
    "up on the computer running this emulator; one that does not resolve shows the peer as "
    "unreachable."};

inline constexpr Term kPeerPort{
    "AUN UDP port",
    "The UDP port the station listens on. Real AUN machines and bridges use 32768. Several "
    "Beebium instances on one computer each need a different port."};

inline constexpr Term kSubnetNet{
    "Econet net",
    "The Econet net number this subnet stands for. AUN convention numbers these from 128 "
    "upwards; net 0 is this machine's own net and cannot be a subnet rule."};

inline constexpr Term kSubnetAddress{
    "IPv4 subnet (a.b.c.0/24)",
    "The subnet whose addresses are the stations of this net: the machine at a.b.c.40 is "
    "station 40."};

}  // namespace beebium::aun_terms

#endif  // BEEBIUM_AUN_TERMS_HPP
