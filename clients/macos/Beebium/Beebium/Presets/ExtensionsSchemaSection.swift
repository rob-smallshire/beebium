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

/// The `extensions` section of describe-preset-schema (v2): the model's
/// attachment points, the points some extensions themselves provide, and which
/// extensions can attach to each point.
struct ExtensionsSchemaSection: Codable {
    let type: String   // "extensions"
    let attachmentPoints: [AttachmentPoint]
    /// Bundled disc-image masters by storage kind (e.g. "hard-disc"), offered in
    /// the image chooser. Absent when the model ships none.
    var bundledImages: [String: [BundledImage]]? = nil
    /// The human noun for each bundled-image kind a catalogue parameter can
    /// reference (e.g. "hard-disc" -> "hard disc", "rom" -> "ROM"), so a file
    /// affordance names the kind in the server's words. Absent on an older server.
    var bundledImageKinds: [BundledImageKind]? = nil
    /// Every extension this model can host, each describing itself: its friendly
    /// name, its description, the parameters it takes and the ROMs it ships. The
    /// one place the Configurator reads an extension's metadata from, so it needs
    /// no per-extension describe-extension round-trip. Absent on older servers.
    var catalogue: [ExtensionCatalogueEntry]? = nil

    enum CodingKeys: String, CodingKey {
        case type
        case attachmentPoints = "attachment_points"
        case bundledImages = "bundled_images"
        case bundledImageKinds = "bundled_image_kinds"
        case catalogue
    }

    /// The display noun for each bundled-image kind, by kind id, from the server.
    var bundledKindNames: [String: String] {
        Dictionary(
            (bundledImageKinds ?? []).map { ($0.kind, $0.displayName) },
            uniquingKeysWith: { first, _ in first })
    }

    /// The catalogue entry for an extension, or nil if the model does not host it.
    func entry(forExtension name: String) -> ExtensionCatalogueEntry? {
        catalogue?.first { $0.name == name }
    }

    /// The friendly display name for an extension, falling back to its identifier
    /// when the catalogue has no entry (an older server).
    func displayName(forExtension name: String) -> String {
        entry(forExtension: name)?.displayName ?? name
    }

    /// The description for an extension, or "" when none is known.
    func description(forExtension name: String) -> String {
        entry(forExtension: name)?.description ?? ""
    }

    /// The parameter manifest for an extension, built from the catalogue (ordered
    /// by declared position). Empty when the catalogue has no entry.
    func manifest(forExtension name: String) -> ExtensionManifest {
        guard let entry = entry(forExtension: name) else {
            return ExtensionManifest(name: name, parameters: [])
        }
        let ordered = entry.parameters.enumerated().sorted { lhs, rhs in
            let lp = lhs.element.position ?? Int.max
            let rp = rhs.element.position ?? Int.max
            return lp != rp ? lp < rp : lhs.offset < rhs.offset
        }.map(\.element)
        return ExtensionManifest(name: name, parameters: ordered, groups: entry.groups)
    }
}

/// One extension in a model's catalogue: the extension describing itself, as
/// `describe-preset-schema` reports it. The same object `describe-extension`
/// prints, carried inline so the Configurator needs no second call.
struct ExtensionCatalogueEntry: Codable, Identifiable {
    let name: String
    var displayName: String? = nil
    var description: String = ""
    /// "peripheral", "econet-transport", ...
    var kind: String = ""
    var attachesTo: [String] = []
    var provides: [String] = []
    var parameters: [ExtensionParameter] = []
    /// The ROMs this extension ships, offered as the bundled choices for a
    /// filepath ROM parameter (e.g. a coprocessor's client ROM).
    var roms: [CatalogueRom] = []
    /// Groups of mutually-exclusive parameters, each shown as one choice control.
    var groups: [ExtensionGroup] = []

    var id: String { name }

    enum CodingKeys: String, CodingKey {
        case name, description, kind, parameters, roms, groups
        case displayName = "display_name"
        case attachesTo = "attaches_to"
        case provides
    }

    // Decoded explicitly (not synthesised) so an entry missing any field but the
    // name still decodes -- the synthesised Decodable requires every non-optional
    // key even when it has a default. `source` and `cli_name` are deliberately
    // not read: `source` is a build-dependent path, and the identifier is `name`.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = try c.decode(String.self, forKey: .name)
        displayName = try c.decodeIfPresent(String.self, forKey: .displayName)
        description = try c.decodeIfPresent(String.self, forKey: .description) ?? ""
        kind = try c.decodeIfPresent(String.self, forKey: .kind) ?? ""
        attachesTo = try c.decodeIfPresent([String].self, forKey: .attachesTo) ?? []
        provides = try c.decodeIfPresent([String].self, forKey: .provides) ?? []
        parameters = try c.decodeIfPresent([ExtensionParameter].self, forKey: .parameters) ?? []
        roms = try c.decodeIfPresent([CatalogueRom].self, forKey: .roms) ?? []
        groups = try c.decodeIfPresent([ExtensionGroup].self, forKey: .groups) ?? []
    }
}

/// A ROM an extension ships: a bundled image offered in its ROM chooser.
struct CatalogueRom: Codable, Equatable, Identifiable {
    /// The parameter role this ROM fills (e.g. "client" for a coprocessor).
    let key: String
    let filename: String
    var displayName: String? = nil
    var size: Int? = nil

    var id: String { filename }

    /// The name to show: the friendly name, falling back to the file stem.
    var label: String {
        if let displayName = displayName, !displayName.isEmpty { return displayName }
        return (filename as NSString).deletingPathExtension
    }

    enum CodingKeys: String, CodingKey {
        case key, filename, size
        case displayName = "display_name"
    }
}

/// The human noun for a bundled-image kind (e.g. kind "hard-disc" ->
/// "hard disc"), as describe-preset-schema reports it. Used to name a file
/// affordance in the server's words rather than a client-side table.
struct BundledImageKind: Codable, Equatable {
    let kind: String
    let displayName: String

    enum CodingKeys: String, CodingKey {
        case kind
        case displayName = "display_name"
    }
}

/// A bundled disc-image master. Its bare `image` name goes into an extension's
/// image parameter; the server resolves it to a per-user copy-on-write working
/// copy at launch.
struct BundledImage: Codable, Equatable {
    let image: String
    let sizeBytes: Int?
    /// The extension this image belongs to, or nil when shared across extensions
    /// (e.g. hard-disc masters). An extension's parameter offers images with a
    /// matching name or nil.
    var extensionName: String?
    /// The friendly name from the schema, if any.
    var schemaDisplayName: String? = nil

    /// A display name: the schema's friendly name, falling back to the image's
    /// filename stem when the schema gives none.
    var displayName: String {
        if let name = schemaDisplayName, !name.isEmpty { return name }
        return (image as NSString).deletingPathExtension
    }

    /// Whether this image may be offered for `extension`.
    func availableTo(extensionName name: String) -> Bool {
        self.extensionName == nil || self.extensionName == name
    }

    enum CodingKeys: String, CodingKey {
        case image
        case sizeBytes = "size_bytes"
        case extensionName = "extension"
        case schemaDisplayName = "display_name"
    }
}

/// One attachment point: a place an extension can plug into. A point is either
/// built into the model (`providedBy` nil, e.g. "1mhz-bus", "tube") or provided
/// by an extension that is itself attached somewhere (e.g. "scsi" provided by
/// "acorn-scsi").
struct AttachmentPoint: Codable, Identifiable {
    let id: String
    /// The extension(s) that provide this point, or nil for a model-built point.
    let providedBy: [String]?
    /// The extensions that can attach to this point.
    let extensions: [String]
    var displayName: String? = nil
    /// What this point is, in a user's words (e.g. "The user port, a parallel
    /// connector ..."). nil when the schema gives none. Optional so a point
    /// object without it still decodes (the synthesised Decodable would
    /// otherwise require it).
    var description: String? = nil
    var minOccupancy: Int? = nil
    /// Maximum extensions on this point; nil means unbounded (or absent).
    var maxOccupancy: Int? = nil

    enum CodingKeys: String, CodingKey {
        case id
        case providedBy = "provided_by"
        case extensions
        case displayName = "display_name"
        case description
        case minOccupancy = "min_occupancy"
        case maxOccupancy = "max_occupancy"
    }
}

/// A chain by which a model can host hard discs, discovered from the schema
/// rather than hard-coded: a disc extension attaches to a point that an adapter
/// extension provides, and that adapter attaches to a model-built point.
///
/// For the SCSI chain this resolves to disc "scsi-hard-disc" on point "scsi"
/// provided by adapter "acorn-scsi" attached to "1mhz-bus" -- but nothing here
/// names those, so a different bus/adapter/disc triple is found the same way.
struct HardDiscCapability: Equatable {
    /// The extension that is a hard disc (e.g. "scsi-hard-disc").
    let discExtension: String
    /// The point the discs attach to (e.g. "scsi").
    let discPoint: String
    /// Display name of the disc point (e.g. "SCSI Bus").
    let discPointName: String
    /// The adapter extension that provides `discPoint` (e.g. "acorn-scsi").
    let adapterExtension: String
    /// The model-built point the adapter attaches to (e.g. "1mhz-bus").
    let adapterPoint: String
    /// Maximum discs per adapter (from the disc point's occupancy), or nil if
    /// unbounded.
    let maxDiscsPerAdapter: Int?

    /// Derive the hard-disc capability a model offers, or nil if it has none.
    ///
    /// Looks for a provided point hosting a disc extension whose adapter can, in
    /// turn, attach to a model-built point. The first such chain is used.
    init?(schema: ExtensionsSchemaSection) {
        let points = schema.attachmentPoints
        for point in points {
            guard let providers = point.providedBy, !providers.isEmpty,
                  let disc = point.extensions.first else { continue }
            let adapter = providers[0]
            // The adapter must itself attach to a model-built point.
            guard let host = points.first(where: {
                $0.providedBy == nil && $0.extensions.contains(adapter)
            }) else { continue }
            self.discExtension = disc
            self.discPoint = point.id
            self.discPointName = point.displayName ?? point.id
            self.adapterExtension = adapter
            self.adapterPoint = host.id
            self.maxDiscsPerAdapter = point.maxOccupancy
            return
        }
        return nil
    }
}
