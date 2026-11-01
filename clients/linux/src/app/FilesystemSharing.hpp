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

#ifndef BEEBIUM_LINUX_APP_FILESYSTEM_SHARING_HPP
#define BEEBIUM_LINUX_APP_FILESYSTEM_SHARING_HPP

namespace beebium::linux_client {

// The sandbox this client runs in, if any.
enum class Sandbox { none, flatpak, snap };

// Detected once from the environment: Flatpak's /.flatpak-info, Snap's SNAP
// variable.
Sandbox current_sandbox();

// "The server shares our filesystem" (client specification 3.2): whether this
// client can exchange paths the server can open. The host fingerprints
// matching is the evidence; a sandboxed client fails closed whatever they
// say, since it shares the host's identity but not its view of the files. A
// sandbox's positive evidence for particular paths (a portal-granted shared
// directory) would be decided per path, not here; this client has none yet.
inline bool shares_filesystem(bool fingerprints_match, Sandbox sandbox) {
    return fingerprints_match && sandbox == Sandbox::none;
}

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_FILESYSTEM_SHARING_HPP
