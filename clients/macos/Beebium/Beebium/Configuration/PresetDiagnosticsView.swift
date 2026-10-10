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

/// Renders a list of diagnostics as labelled rows (errors red, warnings orange).
/// The shared presentation every section uses for its own diagnostics.
struct DiagnosticLabels: View {
    let diagnostics: [PresetManager.PresetDiagnostic]
    var font: Font = .caption2

    var body: some View {
        ForEach(Array(diagnostics.enumerated()), id: \.offset) { _, diagnostic in
            Label(diagnostic.message, systemImage: diagnostic.isError
                  ? "exclamationmark.octagon.fill" : "exclamationmark.triangle.fill")
                .font(font)
                .foregroundColor(diagnostic.isError ? .red : .orange)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

extension Array where Element == PresetManager.PresetDiagnostic {
    /// Diagnostics whose path is exactly `path`.
    func atPath(_ path: String) -> [Element] { filter { $0.path == path } }
    /// Diagnostics whose path begins with `prefix`.
    func withPrefix(_ prefix: String) -> [Element] { filter { $0.path.hasPrefix(prefix) } }
    /// Diagnostics on the extensions-array instance at `index`.
    func forExtension(index: Int) -> [Element] {
        filter { PresetDiagnosticLocation.extensionIndex(path: $0.path) == index }
    }
    /// Diagnostics on an extension parameter named `key` (the parameter itself,
    /// not an item within a structured list it holds -- those route by
    /// forListItem / forListItemField).
    func forConfigKey(_ key: String) -> [Element] {
        filter { PresetDiagnosticLocation.configKey(path: $0.path) == key }
    }
    /// Diagnostics on the item at `index` of a structured-list parameter named
    /// `key`, whatever the parent prefix (extensions config or econet transport
    /// parameters) -- including a field within that item.
    func forListItem(key: String, index: Int) -> [Element] {
        filter { PresetDiagnosticLocation.listItem(path: $0.path, key: key)?.index == index }
    }
    /// Diagnostics on a specific `field` of the item at `index` of a
    /// structured-list parameter named `key`.
    func forListItemField(key: String, index: Int, field: String) -> [Element] {
        filter {
            let location = PresetDiagnosticLocation.listItem(path: $0.path, key: key)
            return location?.index == index && location?.field == field
        }
    }
    /// Whether any diagnostic points at an item of the structured-list parameter
    /// named `key` (used to keep a section's own summary from repeating what the
    /// list rows already show).
    func isListItem(key: String) -> Bool {
        contains { PresetDiagnosticLocation.listItem(path: $0.path, key: key) != nil }
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
    /// A deeper path into a structured-list item ("/extensions/2/config/map/0/
    /// host") is not a parameter-level diagnostic, so this returns nil for it --
    /// those route by `listItem` instead.
    static func configKey(path: String) -> String? {
        let components = path.split(separator: "/").map(String.init)
        guard components.count == 4, components[0] == "extensions", components[2] == "config"
        else { return nil }
        return components[3]
    }

    /// The item index, and optional field, a diagnostic points at inside a
    /// structured-list parameter named `key`. Handles both the extensions form
    /// (".../config/<key>/<index>[/<field>]") and the econet transport form
    /// (".../parameters/<key>/<index>[/<field>]"): the parent prefix does not
    /// matter, only that `key` is preceded by "config" or "parameters" and
    /// followed by an integer index. nil when the path is not under an item of
    /// that key.
    static func listItem(path: String, key: String) -> (index: Int, field: String?)? {
        let components = path.split(separator: "/").map(String.init)
        guard let keyIndex = components.firstIndex(of: key), keyIndex > 0,
              components[keyIndex - 1] == "config" || components[keyIndex - 1] == "parameters"
        else { return nil }
        guard keyIndex + 1 < components.count, let index = Int(components[keyIndex + 1])
        else { return nil }
        let field = keyIndex + 2 < components.count ? components[keyIndex + 2] : nil
        return (index, field)
    }
}
