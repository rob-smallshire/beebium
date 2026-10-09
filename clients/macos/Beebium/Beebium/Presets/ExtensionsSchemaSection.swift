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

    enum CodingKeys: String, CodingKey {
        case type
        case attachmentPoints = "attachment_points"
        case bundledImages = "bundled_images"
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

    /// A display name: the image's filename stem (there is no nicer name).
    var displayName: String {
        (image as NSString).deletingPathExtension
    }

    /// Whether this image may be offered for `extension`.
    func availableTo(extensionName name: String) -> Bool {
        self.extensionName == nil || self.extensionName == name
    }

    enum CodingKeys: String, CodingKey {
        case image
        case sizeBytes = "size_bytes"
        case extensionName = "extension"
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
    var minOccupancy: Int? = nil
    /// Maximum extensions on this point; nil means unbounded (or absent).
    var maxOccupancy: Int? = nil

    enum CodingKeys: String, CodingKey {
        case id
        case providedBy = "provided_by"
        case extensions
        case displayName = "display_name"
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
    /// Bundled disc-image masters offered in the image chooser.
    let bundledMasters: [BundledImage]

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
            // "hard-disc" is the storage kind SCSI discs draw masters from; it
            // matches the disc image parameter's declared `bundled` kind. Filter
            // to images available to the disc extension (hard-disc masters are
            // shared, so this keeps them all).
            self.bundledMasters = (schema.bundledImages?["hard-disc"] ?? [])
                .filter { $0.availableTo(extensionName: disc) }
            return
        }
        return nil
    }
}
