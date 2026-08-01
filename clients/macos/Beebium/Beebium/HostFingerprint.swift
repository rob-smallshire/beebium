// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

import CryptoKit
import Darwin
import Foundation

/// This host, as an opaque token comparable with the one a server reports in
/// `SystemInfo.host_fingerprint`.
///
/// The two must be derived the same way, so the recipe here mirrors
/// `src/service/include/beebium/service/HostFingerprint.hpp`: the platform's
/// host identifier, prefixed with a domain string, hashed with SHA-256 and
/// rendered as lowercase hex. Changing either side alone makes every server
/// look as though it is somewhere else.
enum HostFingerprint {
    /// Must match `HOST_FINGERPRINT_DOMAIN` on the server.
    private static let domain = "beebium-host-v1:"

    /// The fingerprint of the host this app is running on, or nil if macOS
    /// declines to identify it. Computed once: it cannot change while the
    /// process lives.
    static let current: String? = compute()

    /// Whether a fingerprint reported by a server names this same host.
    ///
    /// An unknown value on either side is not a match. Treating two unknowns
    /// as equal would enable exactly the features that break when the hosts
    /// differ, in the case where we know least about them.
    static func isThisHost(_ fingerprint: String) -> Bool {
        guard !fingerprint.isEmpty, let current, !current.isEmpty else {
            return false
        }
        return fingerprint == current
    }

    private static func compute() -> String? {
        var uuid = uuid_t(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        // A null timeout means do not wait; the value is available at once.
        var noWait = timespec(tv_sec: 0, tv_nsec: 0)
        let result = withUnsafeMutablePointer(to: &uuid) { uuidPointer in
            uuidPointer.withMemoryRebound(to: UInt8.self, capacity: 16) { bytes in
                gethostuuid(bytes, &noWait)
            }
        }
        guard result == 0 else {
            NSLog("[HostFingerprint] gethostuuid failed: \(errno)")
            return nil
        }

        var text = [CChar](repeating: 0, count: 37)
        withUnsafePointer(to: &uuid) { uuidPointer in
            uuidPointer.withMemoryRebound(to: UInt8.self, capacity: 16) { bytes in
                uuid_unparse_lower(bytes, &text)
            }
        }
        guard let identifier = String(validatingUTF8: text) else {
            return nil
        }

        let digest = SHA256.hash(data: Data((domain + identifier).utf8))
        return digest.map { String(format: "%02x", $0) }.joined()
    }
}
