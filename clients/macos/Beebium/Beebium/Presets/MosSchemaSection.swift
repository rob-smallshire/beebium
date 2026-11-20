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

/// The `mos_image_uri` section of describe-preset-schema: the machine's MOS
/// (operating-system) ROM. A single value -- a ROM name on the search path or a
/// path -- whose default is the model's own MOS. The Processor section presents
/// it as one field (the current MOS, with Browse and Use Default), the preset
/// key being the equivalent of `--mos`.
struct MosSchemaSection: Codable {
    let type: String            // "mos_image_uri"
    /// The friendly field name, e.g. "MOS ROM".
    var label: String = "MOS ROM"
    /// The model's own MOS: what the field shows when the preset sets none.
    let `default`: String
    var description: String = ""

    enum CodingKeys: String, CodingKey {
        case type, label, description
        case `default`
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        type = try c.decode(String.self, forKey: .type)
        label = try c.decodeIfPresent(String.self, forKey: .label) ?? "MOS ROM"
        `default` = try c.decodeIfPresent(String.self, forKey: .default) ?? ""
        description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
    }
}
