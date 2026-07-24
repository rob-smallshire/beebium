// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

import Foundation

/// A connection request parsed from a `beebium://connect` deep link.
///
/// The scheme lets an external process attach the app to a specific server
/// without going through Bonjour discovery or the Connect dialog:
///
///     beebium://connect?host=127.0.0.1&port=48875
///
/// This is what an automated soak driver uses to point a real frontend at a
/// server it launched on an ephemeral port (which is not advertised over
/// mDNS), so frontend-only freezes can be reproduced with the Metal renderer
/// attached.
struct DeepLinkConnectRequest: Equatable {
    let target: ConnectionTarget
    /// Whether to issue Run() after connecting (a server launched paused with
    /// --wait=api). Off by default: attaching to an already-running server
    /// must not force it to run.
    let needsRun: Bool
    /// Provenance UUID when the caller launched the server itself; nil for a
    /// plain external attach.
    let provenanceUUID: String?
    /// Requested initial sidebar visibility (?sidebar=closed / open); nil leaves
    /// the app default.
    let showSidebar: Bool?
}

enum DeepLink {
    static let scheme = "beebium"
    static let connectHost = "connect"
    static let defaultPort = 48875

    /// Parse a `beebium://connect?host=<host>&port=<port>` URL.
    ///
    /// Returns nil for anything that is not a well-formed connect link:
    /// wrong scheme, wrong action, missing/empty host, or an out-of-range port.
    /// `port` is optional and defaults to \(defaultPort).
    static func parseConnect(_ url: URL) -> DeepLinkConnectRequest? {
        guard let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              components.scheme?.lowercased() == scheme,
              components.host?.lowercased() == connectHost else {
            return nil
        }

        let items = components.queryItems ?? []
        func value(_ name: String) -> String? {
            items.first { $0.name == name }?.value
        }

        guard let host = value("host")?.trimmingCharacters(in: .whitespaces), !host.isEmpty else {
            return nil
        }

        let port: Int
        if let portString = value("port") {
            guard let parsed = Int(portString), (1...65535).contains(parsed) else { return nil }
            port = parsed
        } else {
            port = defaultPort
        }

        let needsRun = value("run").map { ["1", "true", "yes"].contains($0.lowercased()) } ?? false

        let showSidebar: Bool?
        switch value("sidebar")?.lowercased() {
        case "closed", "hidden", "off", "0", "false", "no": showSidebar = false
        case "open", "shown", "on", "1", "true", "yes": showSidebar = true
        default: showSidebar = nil   // absent or unrecognised: leave the default
        }

        return DeepLinkConnectRequest(
            target: ConnectionTarget(host: host, port: port),
            needsRun: needsRun,
            provenanceUUID: value("provenance"),
            showSidebar: showSidebar)
    }
}
