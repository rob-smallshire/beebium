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
    /// A permitted value for a choice parameter: the value written back, the
    /// label shown, and a description explaining the choice (shown beneath the
    /// picker for the selected choice, as the storage FDC does).
    struct Choice: Codable, Equatable, Identifiable {
        let value: String
        let label: String
        var description: String = ""
        var id: String { value }

        enum CodingKeys: String, CodingKey {
            case value, label, description
        }

        // Only value and label are required; description tolerates absence so an
        // older server (or a trimmed fixture) that omits it still decodes.
        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            value = try c.decode(String.self, forKey: .value)
            label = try c.decode(String.self, forKey: .label)
            description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        }

        init(value: String, label: String, description: String = "") {
            self.value = value
            self.label = label
            self.description = description
        }
    }

    let key: String
    /// "string", "integer", "boolean" or "filepath".
    let type: String
    let isList: Bool
    let required: Bool
    /// The default, always as a string (e.g. "19200", "" for none). For a
    /// host-resolved default (see `defaultFrom`) this stays "" so an exact-record
    /// preset writes nothing; `effectiveDefault` carries the value in force.
    let defaultValue: String
    /// Names a host-resolved default, the way `choicesFrom` names a host
    /// enumeration (e.g. "aun-map-file" for the AUN map file); "" when the
    /// default is the plain `defaultValue`. A name this client does not know is
    /// treated as none.
    var defaultFrom: String = ""
    /// The value the server would use on this host when none is given. Equals
    /// `defaultValue` for every parameter except a host-resolved one (where it is
    /// the resolved value, e.g. the per-user map file path). Empty only on an
    /// older server that does not report it; use `defaultInForce`, which falls
    /// back to `defaultValue`.
    var effectiveDefault: String = ""
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
    /// For an `is_list` parameter of structured records (e.g. the AUN map's
    /// peers), the fields of one item, in display order. Empty for a plain list
    /// of strings. A field mirrors a parameter's own keys but is a flat leaf
    /// (see ExtensionListField), so the parameter type stays non-recursive.
    var fields: [ExtensionListField] = []
    /// The template that renders one structured item as a single line, e.g.
    /// "{net}.{station}@{host}@{port}"; empty when the parameter has no fields.
    /// `{key}` placeholders with literal separators between them; a leading
    /// non-required field may be omitted together with the separator after it.
    var itemFormat: String = ""

    var id: String { key }

    /// The human label for the control: the explicit label, else the key.
    var displayLabel: String { label.isEmpty ? key : label }

    enum CodingKeys: String, CodingKey {
        case key, type, required, bundled, unique, description, label, choices, position, excludes, fields
        case isList = "is_list"
        case defaultValue = "default"
        case defaultFrom = "default_from"
        case effectiveDefault = "effective_default"
        case providerOf = "provider_of"
        case minimum = "min"
        case maximum = "max"
        case choicesFrom = "choices_from"
        case itemFormat = "item_format"
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
        defaultFrom = try c.decodeIfPresent(String.self, forKey: .defaultFrom) ?? ""
        effectiveDefault = try c.decodeIfPresent(String.self, forKey: .effectiveDefault) ?? ""
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
        fields = try c.decodeIfPresent([ExtensionListField].self, forKey: .fields) ?? []
        itemFormat = try c.decodeIfPresent(String.self, forKey: .itemFormat) ?? ""
    }

    /// Whether this parameter is a list of structured records (an `is_list`
    /// parameter that declares `fields`), as opposed to a plain list of strings.
    var isStructuredList: Bool { isList && !fields.isEmpty }

    /// The default value to show as a real value in a field that has none given:
    /// the server's effective default when reported (equal to `defaultValue`
    /// except for a host-resolved default), falling back to `defaultValue` on an
    /// older server that does not report it.
    var defaultInForce: String { effectiveDefault.isEmpty ? defaultValue : effectiveDefault }
}

/// One field of a structured `is_list` item (e.g. a peer's net, station, host,
/// port in the AUN map). Its JSON mirrors a parameter's own keys -- key, label,
/// description, type, required, default, min, max, choices -- but a field is a
/// leaf: it never itself carries fields, so this is a distinct, non-recursive
/// type rather than a reused ExtensionParameter. (A struct that stored an array
/// of its own type would be a recursive value type; keeping the field type flat
/// avoids that hazard entirely.)
struct ExtensionListField: Codable, Identifiable, Equatable {
    let key: String
    /// "string", "integer" or "boolean" (a field has no filepath type).
    let type: String
    var label: String = ""
    var description: String = ""
    var required: Bool = false
    /// The default, always as a string ("" for none), as for a parameter.
    var defaultValue: String = ""
    /// Bounds for an integer field; nil when unbounded.
    var minimum: Int?
    var maximum: Int?
    /// Permitted values, or empty when the value is free.
    var choices: [ExtensionParameter.Choice] = []

    var id: String { key }

    /// The human label for the control: the explicit label, else the key.
    var displayLabel: String { label.isEmpty ? key : label }

    enum CodingKeys: String, CodingKey {
        case key, type, label, description, required, choices
        case defaultValue = "default"
        case minimum = "min"
        case maximum = "max"
    }

    // Only key and type are mandatory; the rest tolerate absence (and min/max
    // tolerate an explicit null), mirroring ExtensionParameter's own decoder.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        key = try c.decode(String.self, forKey: .key)
        type = try c.decode(String.self, forKey: .type)
        label = try c.decodeIfPresent(String.self, forKey: .label) ?? ""
        description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        required = try c.decodeIfPresent(Bool.self, forKey: .required) ?? false
        defaultValue = try c.decodeIfPresent(String.self, forKey: .defaultValue) ?? ""
        minimum = try c.decodeIfPresent(Int.self, forKey: .minimum)
        maximum = try c.decodeIfPresent(Int.self, forKey: .maximum)
        choices = try c.decodeIfPresent([ExtensionParameter.Choice].self, forKey: .choices) ?? []
    }

    init(key: String, type: String, label: String = "", description: String = "",
         required: Bool = false, defaultValue: String = "",
         minimum: Int? = nil, maximum: Int? = nil, choices: [ExtensionParameter.Choice] = []) {
        self.key = key
        self.type = type
        self.label = label
        self.description = description
        self.required = required
        self.defaultValue = defaultValue
        self.minimum = minimum
        self.maximum = maximum
        self.choices = choices
    }
}

/// A group of mutually-exclusive parameters an extension offers as one choice
/// (e.g. acorn-rtc's "Clock start": host time, a start time, or an offset).
///
/// Rendered as a single control: the `none` option (when the group has a
/// "nothing given" state) followed by one option per member parameter, in
/// `parameters` order. Choosing an option reveals only that member's field and
/// clears the others in the draft; choosing `none` clears them all and writes
/// nothing. The members still list each other in their `excludes`, so the
/// client's exclusion handling needs no special case.
struct ExtensionGroup: Codable, Equatable, Identifiable {
    /// The "nothing given" option of a group, when it has one.
    struct NoneOption: Codable, Equatable {
        var label: String = ""
        var description: String = ""

        init(label: String = "", description: String = "") {
            self.label = label
            self.description = description
        }

        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            label = try c.decodeIfPresent(String.self, forKey: .label) ?? ""
            description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        }

        enum CodingKeys: String, CodingKey { case label, description }
    }

    let key: String
    var label: String = ""
    var description: String = ""
    /// The member parameter keys, in the order their options are shown.
    var parameters: [String] = []
    /// The "nothing given" option, or nil when every state sets a member.
    var none: NoneOption? = nil

    var id: String { key }

    enum CodingKeys: String, CodingKey { case key, label, description, parameters, none }

    // Only key is required; the rest tolerate absence.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        key = try c.decode(String.self, forKey: .key)
        label = try c.decodeIfPresent(String.self, forKey: .label) ?? ""
        description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        parameters = try c.decodeIfPresent([String].self, forKey: .parameters) ?? []
        none = try c.decodeIfPresent(NoneOption.self, forKey: .none)
    }

    init(key: String, label: String = "", description: String = "",
         parameters: [String] = [], none: NoneOption? = nil) {
        self.key = key
        self.label = label
        self.description = description
        self.parameters = parameters
        self.none = none
    }
}

/// An extension's manifest: the parameters it takes, in declared order, and the
/// groups of mutually-exclusive parameters among them.
///
/// Built from the model's extension catalogue (see
/// `ExtensionsSchemaSection.manifest(forExtension:)`), which carries each
/// extension's parameters inline -- so the Configurator needs no per-extension
/// round-trip to assemble one.
struct ExtensionManifest: Equatable {
    let name: String
    let parameters: [ExtensionParameter]
    var groups: [ExtensionGroup] = []

    /// The group a parameter belongs to, or nil when it stands alone. A
    /// parameter is in at most one group.
    func group(forParameter key: String) -> ExtensionGroup? {
        groups.first { $0.parameters.contains(key) }
    }
}
