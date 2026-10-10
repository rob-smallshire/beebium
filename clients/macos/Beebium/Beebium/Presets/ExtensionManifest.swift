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

/// One configuration parameter an extension takes, as the extension catalogue
/// reports it.
///
/// describe-preset-schema says where things attach and, in its catalogue, what
/// each extension takes. The generic parameter form (ExtensionParameterForm) is
/// driven by these, so it adapts to any extension without the client hard-coding
/// its keys.
struct ExtensionParameter: Codable, Identifiable, Equatable {
    /// A permitted value for a choice parameter: the value written back and the
    /// label shown.
    struct Choice: Codable, Equatable, Identifiable {
        let value: String
        let label: String
        var id: String { value }
    }

    let key: String
    /// "string", "integer", "boolean" or "filepath".
    let type: String
    let isList: Bool
    let required: Bool
    /// The default, always as a string (e.g. "19200", "" for none).
    let defaultValue: String
    /// Non-empty when this parameter names a bundled-image kind (e.g. the SCSI
    /// disc image's "hard-disc").
    let bundled: String
    /// Whether the value must be unique across instances on a bus (e.g. scsi-id).
    let unique: Bool
    /// Non-empty when this parameter references a provider instance of the named
    /// point (e.g. adapter-id references a "scsi" provider).
    let providerOf: String
    let description: String
    /// Human label; may be empty on older servers (then the key is used).
    var label: String = ""
    /// Display/declaration order; nil when unspecified.
    var position: Int?
    /// Bounds, integer parameters only.
    var minimum: Int?
    var maximum: Int?
    /// Permitted values, or empty when the value is free.
    var choices: [Choice] = []
    /// Other parameter keys that cannot be set together with this one; when one
    /// of a pair has a value, the other is disabled.
    var excludes: [String] = []
    /// A source of suggestions for a free-text value, e.g. "serial-ports"; empty
    /// when none.
    var choicesFrom: String = ""

    var id: String { key }

    /// The human label for the control: the explicit label, else the key.
    var displayLabel: String { label.isEmpty ? key : label }

    enum CodingKeys: String, CodingKey {
        case key, type, required, bundled, unique, description, label, choices, position, excludes
        case isList = "is_list"
        case defaultValue = "default"
        case providerOf = "provider_of"
        case minimum = "min"
        case maximum = "max"
        case choicesFrom = "choices_from"
    }

    // Decoded explicitly (not synthesised) so a parameter object missing any
    // field but key/type still decodes -- the synthesised Decodable requires
    // every non-optional key even when it has a default. Only key and type are
    // mandatory; the rest tolerate absence, so older servers (or trimmed
    // fixtures) still parse.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        key = try c.decode(String.self, forKey: .key)
        type = try c.decode(String.self, forKey: .type)
        isList = try c.decodeIfPresent(Bool.self, forKey: .isList) ?? false
        required = try c.decodeIfPresent(Bool.self, forKey: .required) ?? false
        defaultValue = try c.decodeIfPresent(String.self, forKey: .defaultValue) ?? ""
        bundled = try c.decodeIfPresent(String.self, forKey: .bundled) ?? ""
        unique = try c.decodeIfPresent(Bool.self, forKey: .unique) ?? false
        providerOf = try c.decodeIfPresent(String.self, forKey: .providerOf) ?? ""
        description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        label = try c.decodeIfPresent(String.self, forKey: .label) ?? ""
        position = try c.decodeIfPresent(Int.self, forKey: .position)
        minimum = try c.decodeIfPresent(Int.self, forKey: .minimum)
        maximum = try c.decodeIfPresent(Int.self, forKey: .maximum)
        choices = try c.decodeIfPresent([Choice].self, forKey: .choices) ?? []
        excludes = try c.decodeIfPresent([String].self, forKey: .excludes) ?? []
        choicesFrom = try c.decodeIfPresent(String.self, forKey: .choicesFrom) ?? ""
    }
}

/// An extension's manifest: the parameters it takes, in declared order.
///
/// Built from the model's extension catalogue (see
/// `ExtensionsSchemaSection.manifest(forExtension:)`), which carries each
/// extension's parameters inline -- so the Configurator needs no per-extension
/// round-trip to assemble one.
struct ExtensionManifest: Equatable {
    let name: String
    let parameters: [ExtensionParameter]
}
