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

import Foundation

/// One configuration parameter an extension takes, from `describe-extension`.
///
/// describe-preset-schema says where things attach; describe-extension says what
/// they take. The generic parameter form (ExtensionParameterForm) is driven by
/// these, so it adapts to any extension without the client hard-coding its keys.
struct ExtensionParameter: Codable, Identifiable, Equatable {
    let key: String
    /// "string", "integer", "boolean" or "filepath".
    let type: String
    let isList: Bool
    let required: Bool
    /// The default, always as a string (e.g. "19200", "" for none).
    let defaultValue: String
    /// Non-empty when this parameter names a bundled-image kind (e.g. the SCSI
    /// disc image's "hard-disc"); the Storage hard-disc UI owns those.
    let bundled: String
    /// Whether the value must be unique across instances on a bus (e.g. scsi-id).
    let unique: Bool
    /// Non-empty when this parameter references a provider instance of the named
    /// point (e.g. adapter-id references a "scsi" provider).
    let providerOf: String
    let description: String

    // The following arrive with describe-extension --json (Stage 2 server work)
    // and default to "unconstrained" until then, so the form works against the
    // current schema and gains richer controls when they land.
    var label: String?
    var minimum: Double?
    var maximum: Double?
    var choices: [String]?
    /// A renderer hint, e.g. "path"/"device"; maps to a control flavour.
    var displayKind: String?

    var id: String { key }

    /// The human label for the control: the explicit label, else the key.
    var displayLabel: String { (label?.isEmpty == false ? label! : nil) ?? key }

    enum CodingKeys: String, CodingKey {
        case key, type, required, bundled, unique, description, label, choices
        case isList = "is_list"
        case defaultValue = "default"
        case providerOf = "provider_of"
        case minimum = "min"
        case maximum = "max"
        case displayKind = "display_kind"
    }
}

/// An extension's manifest: the parameters it takes, in declared order.
struct ExtensionManifest: Equatable {
    let name: String
    let parameters: [ExtensionParameter]

    /// Parse the JSONL output of `<exe> --format jsonl describe-extension <name>`
    /// (one parameter object per line). A side-effect-free seam, so parsing is
    /// testable without running a server.
    static func parse(name: String, jsonl: String) -> ExtensionManifest {
        var parameters: [ExtensionParameter] = []
        for raw in jsonl.split(whereSeparator: \.isNewline) {
            guard let data = raw.data(using: .utf8),
                  let parameter = try? JSONDecoder().decode(ExtensionParameter.self, from: data) else {
                continue
            }
            parameters.append(parameter)
        }
        return ExtensionManifest(name: name, parameters: parameters)
    }
}
