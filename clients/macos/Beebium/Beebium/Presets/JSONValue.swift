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

/// An arbitrary JSON value, preserved verbatim.
///
/// `MachineDraft` and its sub-documents model the preset keys they understand as
/// typed fields and keep every other key as a `JSONValue` in an `extra` bag, so a
/// preset round-trips without loss even as the server grows new keys. Integers
/// are kept distinct from doubles so `scsi-id: 0` does not re-encode as `0.0`.
enum JSONValue: Equatable {
    case null
    case bool(Bool)
    case int(Int)
    case double(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])
}

extension JSONValue: Codable {
    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        if container.decodeNil() {
            self = .null
        } else if let value = try? container.decode(Bool.self) {
            self = .bool(value)
        } else if let value = try? container.decode(Int.self) {
            self = .int(value)
        } else if let value = try? container.decode(Double.self) {
            self = .double(value)
        } else if let value = try? container.decode(String.self) {
            self = .string(value)
        } else if let value = try? container.decode([JSONValue].self) {
            self = .array(value)
        } else if let value = try? container.decode([String: JSONValue].self) {
            self = .object(value)
        } else {
            throw DecodingError.dataCorruptedError(
                in: container, debugDescription: "Unsupported JSON value")
        }
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        switch self {
        case .null:          try container.encodeNil()
        case .bool(let v):   try container.encode(v)
        case .int(let v):    try container.encode(v)
        case .double(let v): try container.encode(v)
        case .string(let v): try container.encode(v)
        case .array(let v):  try container.encode(v)
        case .object(let v): try container.encode(v)
        }
    }
}

/// A `CodingKey` that accepts any string key, so a keyed container can be read
/// and written with keys not known at compile time (the `extra` bag).
struct JSONCodingKey: CodingKey, ExpressibleByStringLiteral {
    var stringValue: String
    var intValue: Int? { nil }

    init(stringValue: String) { self.stringValue = stringValue }
    init?(intValue: Int) { nil }
    init(stringLiteral value: String) { self.stringValue = value }
}

extension KeyedDecodingContainer where Key == JSONCodingKey {
    func decodeIfPresent(_ type: String.Type, forKey key: String) throws -> String? {
        try decodeIfPresent(type, forKey: JSONCodingKey(stringValue: key))
    }
    func decodeIfPresent(_ type: Bool.Type, forKey key: String) throws -> Bool? {
        try decodeIfPresent(type, forKey: JSONCodingKey(stringValue: key))
    }
    func decodeIfPresent(_ type: Int.Type, forKey key: String) throws -> Int? {
        try decodeIfPresent(type, forKey: JSONCodingKey(stringValue: key))
    }
    func decode(_ type: String.Type, forKey key: String) throws -> String {
        try decode(type, forKey: JSONCodingKey(stringValue: key))
    }
    func decode(_ type: Int.Type, forKey key: String) throws -> Int {
        try decode(type, forKey: JSONCodingKey(stringValue: key))
    }
    func decodeIfPresent<T: Decodable>(_ type: T.Type, forKey key: String) throws -> T? {
        try decodeIfPresent(type, forKey: JSONCodingKey(stringValue: key))
    }

    /// Collect every key not in `known` as a `JSONValue`, preserving unmodelled
    /// keys so they survive a round-trip.
    func decodeExtra(excluding known: Set<String>) throws -> [String: JSONValue] {
        var extra: [String: JSONValue] = [:]
        for key in allKeys where !known.contains(key.stringValue) {
            extra[key.stringValue] = try decode(JSONValue.self, forKey: key)
        }
        return extra
    }
}

extension KeyedEncodingContainer where Key == JSONCodingKey {
    mutating func encodeIfPresent(_ value: String?, forKey key: String) throws {
        try encodeIfPresent(value, forKey: JSONCodingKey(stringValue: key))
    }
    mutating func encodeIfPresent(_ value: Bool?, forKey key: String) throws {
        try encodeIfPresent(value, forKey: JSONCodingKey(stringValue: key))
    }
    mutating func encodeIfPresent(_ value: Int?, forKey key: String) throws {
        try encodeIfPresent(value, forKey: JSONCodingKey(stringValue: key))
    }
    mutating func encode(_ value: String, forKey key: String) throws {
        try encode(value, forKey: JSONCodingKey(stringValue: key))
    }
    mutating func encode(_ value: Int, forKey key: String) throws {
        try encode(value, forKey: JSONCodingKey(stringValue: key))
    }
    mutating func encode<T: Encodable>(_ value: T, forKey key: String) throws {
        try encode(value, forKey: JSONCodingKey(stringValue: key))
    }
    mutating func encodeIfPresent<T: Encodable>(_ value: T?, forKey key: String) throws {
        try encodeIfPresent(value, forKey: JSONCodingKey(stringValue: key))
    }

    /// Re-emit the preserved unmodelled keys.
    mutating func encodeExtra(_ extra: [String: JSONValue]) throws {
        for (key, value) in extra {
            try encode(value, forKey: JSONCodingKey(stringValue: key))
        }
    }
}
