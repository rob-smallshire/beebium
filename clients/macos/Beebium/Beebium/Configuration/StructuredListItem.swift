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

/// The item_format template of a structured `is_list` parameter, and the pure
/// rules that turn one item between its three forms: the object the client
/// writes to the draft, the field strings an editor shows and commits, and the
/// single display line the row shows.
///
/// Template syntax (agreed with the server): `{key}` placeholders with literal
/// separators between them, e.g. "{net}.{station}@{host}@{port}". A leading
/// field that is not required may be absent together with the separator after
/// it, so "254@10.0.0.5@32768" parses as the same peer with no net.
///
/// A preset item may be an object (the form the client writes) or a legacy
/// string. The string form is parsed via the template only to prefill the
/// editor; an object is rendered via the template only for display. Both are
/// pure and tested in StructuredListItemTests.
struct ItemFormatTemplate: Equatable {
    /// The field keys, in template order.
    let fieldKeys: [String]
    /// The literal before the first field (usually empty).
    let prefix: String
    /// The literal after the last field (usually empty).
    let suffix: String
    /// The literal between consecutive fields; count is fieldKeys.count - 1, so
    /// separators[i] sits between fieldKeys[i] and fieldKeys[i + 1].
    let separators: [String]

    init(_ template: String) {
        var keys: [String] = []
        var literals: [String] = []          // literal preceding each field, in order
        var literal = ""
        var i = template.startIndex
        while i < template.endIndex {
            let ch = template[i]
            if ch == "{", let close = template[i...].firstIndex(of: "}") {
                let key = String(template[template.index(after: i)..<close])
                keys.append(key)
                literals.append(literal)
                literal = ""
                i = template.index(after: close)
            } else {
                literal.append(ch)
                i = template.index(after: i)
            }
        }
        fieldKeys = keys
        prefix = literals.first ?? literal
        separators = Array(literals.dropFirst())
        suffix = keys.isEmpty ? "" : literal
    }
}

/// Pure conversions for one structured list item. Field metadata (type,
/// required, choices) comes from the manifest's `fields`; the template drives
/// the one-line rendering and the legacy-string parse.
enum StructuredListItem {

    // MARK: - Object <-> field strings

    /// The display string of a JSON value for a field (an integer as its
    /// digits, a bool as true/false, a string as itself).
    static func displayString(_ value: JSONValue?) -> String {
        switch value {
        case .string(let s): return s
        case .int(let n):    return String(n)
        case .double(let d): return String(d)
        case .bool(let b):   return b ? "true" : "false"
        case .array, .object, .null, .none: return ""
        }
    }

    /// The field strings of an item object, keyed by field key -- what an editor
    /// shows. A field absent from the object is absent here.
    static func fieldStrings(fromObject object: [String: JSONValue],
                             fields: [ExtensionListField]) -> [String: String] {
        var out: [String: String] = [:]
        for field in fields where object[field.key] != nil {
            out[field.key] = displayString(object[field.key])
        }
        return out
    }

    /// An item object from field strings, typed per the fields: an integer field
    /// becomes a JSON number, a boolean a JSON bool, everything else a string. A
    /// field that is not required and empty is left out (the server records only
    /// what was given); a string that will not parse as its integer is kept
    /// verbatim so the user's input is not lost and resolve-preset can flag it.
    static func object(fromFieldStrings strings: [String: String],
                       fields: [ExtensionListField]) -> [String: JSONValue] {
        var out: [String: JSONValue] = [:]
        for field in fields {
            guard let raw = strings[field.key] else { continue }
            if raw.isEmpty && !field.required { continue }
            switch field.type {
            case "integer":
                let trimmed = raw.trimmingCharacters(in: .whitespaces)
                if let n = Int(trimmed) { out[field.key] = .int(n) }
                else { out[field.key] = .string(raw) }
            case "boolean":
                out[field.key] = .bool(raw == "true" || raw == "1")
            default:
                out[field.key] = .string(raw)
            }
        }
        return out
    }

    // MARK: - Display (object -> one line)

    /// The one-line rendering of an item object through the template. A leading
    /// non-required field with no value is dropped together with the separator
    /// after it.
    static func render(object: [String: JSONValue], template: ItemFormatTemplate,
                       fields: [ExtensionListField]) -> String {
        render(strings: fieldStrings(fromObject: object, fields: fields),
               template: template, fields: fields)
    }

    static func render(strings: [String: String], template: ItemFormatTemplate,
                       fields: [ExtensionListField]) -> String {
        guard !template.fieldKeys.isEmpty else { return template.prefix + template.suffix }
        var out = template.prefix
        var leadingDropped = false
        for (i, key) in template.fieldKeys.enumerated() {
            let value = strings[key] ?? ""
            if i == 0 && isLeadingOptional(template: template, fields: fields) && value.isEmpty {
                leadingDropped = true
                continue
            }
            if i > 0 && !(leadingDropped && i == 1) {
                out += template.separators[i - 1]
            }
            leadingDropped = false
            out += value
        }
        return out + template.suffix
    }

    // MARK: - Legacy string -> field strings

    /// Parse a legacy string item into field strings, honouring the leading
    /// non-required field that may be absent with its separator. Used only to
    /// prefill the editor when a preset still carries the string form; the
    /// client writes objects thereafter.
    static func parse(string: String, template: ItemFormatTemplate,
                      fields: [ExtensionListField]) -> [String: String] {
        guard !template.fieldKeys.isEmpty else { return [:] }
        var remainder = string
        if !template.prefix.isEmpty, remainder.hasPrefix(template.prefix) {
            remainder.removeFirst(template.prefix.count)
        }

        let leadingPresent = leadingFieldPresent(in: remainder, template: template, fields: fields)
        let keys = leadingPresent ? template.fieldKeys : Array(template.fieldKeys.dropFirst())
        let seps = leadingPresent ? template.separators : Array(template.separators.dropFirst())

        var out: [String: String] = [:]
        var rest = Substring(remainder)
        for (k, key) in keys.enumerated() {
            if k < keys.count - 1 {
                let sep = seps[k]
                if !sep.isEmpty, let r = rest.range(of: sep) {
                    out[key] = String(rest[..<r.lowerBound])
                    rest = rest[r.upperBound...]
                } else {
                    out[key] = String(rest)
                    rest = Substring()
                    return out
                }
            } else {
                var last = String(rest)
                if !template.suffix.isEmpty, last.hasSuffix(template.suffix) {
                    last.removeLast(template.suffix.count)
                }
                out[key] = last
            }
        }
        return out
    }

    /// An item object parsed from a legacy string.
    static func object(fromString string: String, template: ItemFormatTemplate,
                       fields: [ExtensionListField]) -> [String: JSONValue] {
        object(fromFieldStrings: parse(string: string, template: template, fields: fields),
               fields: fields)
    }

    // MARK: - Leading-optional rule

    /// Whether the template's first field is a leading field that may be absent:
    /// it is the first placeholder, has an empty prefix before it, and its field
    /// is not required.
    static func isLeadingOptional(template: ItemFormatTemplate,
                                  fields: [ExtensionListField]) -> Bool {
        guard let first = template.fieldKeys.first, template.prefix.isEmpty,
              template.fieldKeys.count >= 2 else { return false }
        return fields.first { $0.key == first }?.required == false
    }

    /// Whether a leading optional field is present in the input: its trailing
    /// separator occurs before the next separator (or, when there is no next
    /// separator, occurs at all). A required leading field is always present.
    private static func leadingFieldPresent(in input: String, template: ItemFormatTemplate,
                                            fields: [ExtensionListField]) -> Bool {
        guard isLeadingOptional(template: template, fields: fields) else { return true }
        let sep1 = template.separators[0]
        guard let i1 = input.range(of: sep1)?.lowerBound else { return false }
        if template.separators.count >= 2 {
            let sep2 = template.separators[1]
            if let i2 = input.range(of: sep2)?.lowerBound { return i1 < i2 }
        }
        return true
    }
}
