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

import SwiftUI

/// Shows resolve-preset diagnostics (errors and warnings) for the draft being
/// configured. A general mechanism: every configuration section's problems come
/// back from the one resolver as diagnostics with a JSON-pointer `path`, and the
/// dialog blocks launching while any error stands.
struct PresetDiagnosticsView: View {
    let diagnostics: [PresetManager.PresetDiagnostic]

    var body: some View {
        if !diagnostics.isEmpty {
            VStack(alignment: .leading, spacing: 4) {
                // Errors first, then warnings; stable within each.
                ForEach(Array(ordered.enumerated()), id: \.offset) { _, diagnostic in
                    row(diagnostic)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var ordered: [PresetManager.PresetDiagnostic] {
        diagnostics.filter(\.isError) + diagnostics.filter { !$0.isError }
    }

    private func row(_ diagnostic: PresetManager.PresetDiagnostic) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Image(systemName: diagnostic.isError
                  ? "exclamationmark.octagon.fill"
                  : "exclamationmark.triangle.fill")
                .foregroundColor(diagnostic.isError ? .red : .orange)
                .font(.caption)
            VStack(alignment: .leading, spacing: 1) {
                Text(diagnostic.message)
                    .font(.caption)
                    .foregroundColor(.primary)
                    .fixedSize(horizontal: false, vertical: true)
                if let location = PresetDiagnosticLocation.describe(path: diagnostic.path) {
                    Text(location)
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
        }
    }
}

/// Maps a diagnostic's JSON-pointer path to a human location, and classifies it
/// to the configuration section it belongs to, so a section can show (or
/// highlight) only its own problems. One place, since every section needs it.
enum PresetDiagnosticLocation {
    /// A human description of where a diagnostic points, or nil for a
    /// whole-document diagnostic (path "").
    static func describe(path: String) -> String? {
        let components = path.split(separator: "/").map(String.init)
        guard let first = components.first else { return nil }
        switch first {
        case "storage":
            if components.count >= 3, components[1] == "floppy_drives", let n = Int(components[2]) {
                return "Floppy \(n)"
            }
            return "Storage"
        case "sideways_bank":
            if components.count >= 3, components[1] == "slots", let n = Int(components[2]) {
                return "Sideways slot \(n)"
            }
            return "Sideways ROM / RAM"
        case "extensions":
            if components.count >= 2, let n = Int(components[1]) {
                return "Extension \(n)"
            }
            return "Extensions"
        case "econet":
            return "Econet"
        default:
            return first
        }
    }

    /// The top-level preset key a diagnostic belongs to (first path component),
    /// or nil for a whole-document diagnostic. Lets a section filter the
    /// diagnostics it is responsible for.
    static func topLevelKey(path: String) -> String? {
        path.split(separator: "/").first.map(String.init)
    }

    /// The extensions-array index a diagnostic points at, from a path like
    /// "/extensions/2" or "/extensions/2/config/scsi-id"; nil if it is not an
    /// extensions diagnostic. Lets a hard-disc row show its own problems.
    static func extensionIndex(path: String) -> Int? {
        let components = path.split(separator: "/").map(String.init)
        guard components.count >= 2, components[0] == "extensions" else { return nil }
        return Int(components[1])
    }

    /// The config key a diagnostic points at, from a path like
    /// "/extensions/2/config/baud"; nil if it is not a per-parameter diagnostic.
    static func configKey(path: String) -> String? {
        let components = path.split(separator: "/").map(String.init)
        guard components.count >= 4, components[0] == "extensions", components[2] == "config"
        else { return nil }
        return components[3]
    }
}
