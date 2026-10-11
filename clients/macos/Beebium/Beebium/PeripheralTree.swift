// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// One configured peripheral extension and its descendants.
///
/// `primaryAttachment` names the extension point we mounted this node
/// under (the first entry in its `attaches_to` list). `secondaryAttachments`
/// holds any further extension points the device also occupies -- used
/// for multi-port devices like the Voltmace Delta 14B/1, which plugs
/// into the user port and the analogue port simultaneously. The node
/// is rendered once (under the primary) with the secondaries shown as
/// badges, so the conceptual DAG collapses cleanly to a tree.
/// One storage device published by a peripheral extension. Surfaced
/// in the Storage sidebar below the machine's built-in floppy drives.
/// Activity LEDs continue to flow through IndicatorService; this
/// struct just names the indicator so the view layer knows which one
/// to subscribe to.
struct PeripheralStorageDevice: Identifiable, Hashable {
    enum Kind: Hashable {
        /// Non-removable: no eject affordance, non-editable path.
        case fixed
        /// User-swappable media: future UIs add mount/eject.
        case removable
    }

    let id: String
    /// Storage-context name, e.g. "Hard Disc Drive 0", "Floppy Disc
    /// Drive 1". See the proto comment on StorageDevice.name for the
    /// numbering convention and why this is distinct from the owning
    /// extension's label.
    let name: String
    let kind: Kind
    let mediaType: String
    let backingPath: String
    let activityIndicatorName: String
}

struct PeripheralNode: Identifiable, Hashable {
    let id: String
    let name: String
    let label: String
    let description: String
    let config: [String: String]
    let primaryAttachment: String
    /// The further attachment-point ids this device also occupies (slugs), e.g.
    /// "analogue-port" for a device that also plugs into the user port.
    let secondaryAttachments: [String]
    /// The display names of `secondaryAttachments`, resolved from the server's
    /// attachment points at build time, so the view names a secondary point
    /// without re-deriving it (falls back to the slug when unnamed).
    let secondaryAttachmentLabels: [String]
    let provides: [String]
    /// True when the extension implements an ExtensionUi panel (i.e.
    /// the server's Extension::ui() returned non-null). Drives whether
    /// the sidebar opens an ExtensionUiService.SubscribeView stream
    /// for this node; for `false`, the row is just the static label.
    let hasUI: Bool
    /// Storage devices the extension publishes (empty for the vast
    /// majority). Used by the Storage sidebar to render rows below
    /// the floppy drives. One extension may publish several devices
    /// -- a future Opus Challenger would expose its floppy drive
    /// and RAM disc here as two entries on the same node.
    let storageDevices: [PeripheralStorageDevice]
    var children: [PeripheralNode]

    /// Best human-readable name for the default (no-ExtensionUi) row.
    ///
    /// The server's Extension::label() already implements the right
    /// fallback chain (explicit per-instance label > manifest
    /// display_name > id), so the client trusts whatever it receives
    /// in `label`. The humaniser is a defensive last resort for the
    /// degenerate case of a manifest with no display_name and an
    /// instance with no explicit label and no id -- which the server
    /// contract should never produce, but no point crashing if it
    /// somehow does.
    var displayName: String {
        if !label.isEmpty && label != id {
            return label
        }
        return PeripheralNameFormatter.humanise(name)
    }
}

/// Bridge from the wire-level `Beebium_StorageDevice` proto to the
/// view-friendly `PeripheralStorageDevice` value type, mapping the
/// proto enum to the native Swift enum.
private func makeStorageDevice(_ proto: Beebium_StorageDevice) -> PeripheralStorageDevice {
    let kind: PeripheralStorageDevice.Kind
    switch proto.kind {
    case .removable:
        kind = .removable
    case .fixed, .UNRECOGNIZED:
        kind = .fixed
    }
    return PeripheralStorageDevice(
        id: proto.id,
        name: proto.name,
        kind: kind,
        mediaType: proto.mediaType,
        backingPath: proto.backingPath,
        activityIndicatorName: proto.activityIndicatorName
    )
}

/// Title-cases a manifest name slug (kebab-case) into a human-friendly
/// form, upper-casing well-known acronyms. Used as the last-resort
/// fallback when an extension has no explicit label and isn't in the
/// client's known-type table.
enum PeripheralNameFormatter {
    private static let acronyms: Set<String> = [
        "scsi", "rtc", "usb", "aun", "adfs", "dfs", "nfs",
        "rom", "ram", "led", "vdu", "fdc", "ide", "spi",
        "i2c", "uart", "io", "rs423", "bbc",
    ]

    static func humanise(_ slug: String) -> String {
        slug.split(separator: "-").map { token -> String in
            let lower = token.lowercased()
            if acronyms.contains(lower) {
                return lower.uppercased()
            }
            return lower.prefix(1).uppercased() + lower.dropFirst()
        }.joined(separator: " ")
    }
}

/// Hierarchical view of the configured peripherals, rooted at the machine's own
/// attachment points (1mhz-bus, user-port, tube, ...).
///
/// `groups` follows the server's attachment-point presentation order (the order
/// `ListExtensions` returns `attachment_points` in: the machine's own connectors
/// first, then provided points); nodes within a group preserve the order the
/// server returned the extensions in (command-line / preset configuration order).
///
/// `orphans` is a defensive bucket for extensions with an empty
/// `attaches_to`; the server's PeripheralExtension contract requires at
/// least one attachment so in practice it stays empty.
struct PeripheralTree: Equatable {

    struct ExtensionPointGroup: Equatable, Identifiable {
        /// The attachment-point id (slug), e.g. "serial-port"; still used where a
        /// view keys on a specific point.
        let extensionPoint: String
        /// The point's display name from the server, falling back to the slug.
        let displayName: String
        let nodes: [PeripheralNode]
        var id: String { extensionPoint }
    }

    let groups: [ExtensionPointGroup]
    let orphans: [PeripheralNode]
    /// Display name for every attachment point the server reported, by id, so a
    /// node's secondary attachments can be named too; falls back to the slug.
    let pointDisplayNames: [String: String]

    static func build(from extensions: [Beebium_ExtensionInfo],
                      attachmentPoints: [Beebium_AttachmentPointInfo] = []) -> PeripheralTree {
        // The display name for each point, by id, and the points' presentation
        // order -- both straight from the server, replacing a client-side name
        // table and an alphabetical sort.
        let pointDisplayNames = Dictionary(
            attachmentPoints.map { ($0.id, $0.displayName.isEmpty ? $0.id : $0.displayName) },
            uniquingKeysWith: { first, _ in first })
        func pointName(_ point: String) -> String { pointDisplayNames[point] ?? point }

        if extensions.isEmpty {
            return PeripheralTree(groups: [], orphans: [], pointDisplayNames: pointDisplayNames)
        }

        // Build the leaf-less nodes and index by id so the second pass
        // can hang children off them in any input order.
        var nodesById: [String: PeripheralNode] = [:]
        for ext in extensions {
            let primary = ext.attachesTo.first ?? ""
            let secondary = ext.attachesTo.count > 1
                ? Array(ext.attachesTo.dropFirst())
                : []
            nodesById[ext.id] = PeripheralNode(
                id: ext.id,
                name: ext.name,
                label: ext.label.isEmpty ? ext.id : ext.label,
                description: ext.description_p,
                config: ext.config,
                primaryAttachment: primary,
                secondaryAttachments: secondary,
                secondaryAttachmentLabels: secondary.map(pointName),
                provides: ext.provides,
                hasUI: ext.hasUi_p,
                storageDevices: ext.storageDevices.map(makeStorageDevice),
                children: []
            )
        }

        // Classify each extension as either a child of the instance the SERVER
        // attached it to, or a root under one of the machine's own connectors.
        // The provider comes from ExtensionInfo.attachments[].provider_id -- the
        // instance the server actually chose (by a provider_of parameter, else
        // the last provider loaded). An empty provider means a machine connector.
        // This reads the server's decision rather than re-deriving it, which a
        // previous "whoever last provides this point" guess got wrong when two
        // adapters provided the same kind of point (e.g. two SCSI host adapters).
        // Empty `attaches_to` is treated as orphan rather than indexed under an
        // empty group name.
        var childIdsByParentId: [String: [String]] = [:]
        var rootIdsByPoint: [String: [String]] = [:]
        var orphanIds: [String] = []

        for ext in extensions {
            guard let primary = ext.attachesTo.first, !primary.isEmpty else {
                orphanIds.append(ext.id)
                continue
            }
            let providerId = ext.attachments.first { $0.point == primary }?.providerID ?? ""
            if !providerId.isEmpty {
                childIdsByParentId[providerId, default: []].append(ext.id)
            } else {
                rootIdsByPoint[primary, default: []].append(ext.id)
            }
        }

        // Recursively walk from each root, attaching children. The
        // server's topological init order guarantees no cycles, but the
        // recursion would terminate naturally even without that
        // guarantee because a node's children are looked up by its id,
        // which never recurses back to itself.
        func attachChildren(to nodeId: String) -> PeripheralNode {
            var node = nodesById[nodeId]!
            let childIds = childIdsByParentId[nodeId, default: []]
            node.children = childIds.map { attachChildren(to: $0) }
            return node
        }

        // Order the groups by the server's attachment-point order; any point the
        // server did not describe (shouldn't happen) sorts after, by id, so the
        // result stays deterministic.
        let orderByPoint = attachmentPoints.enumerated().reduce(into: [String: Int]()) {
            $0[$1.element.id] = $1.offset
        }
        let orderedPoints = rootIdsByPoint.keys.sorted {
            (orderByPoint[$0] ?? Int.max, $0) < (orderByPoint[$1] ?? Int.max, $1)
        }
        let groups = orderedPoints.map { point -> ExtensionPointGroup in
            let ids = rootIdsByPoint[point] ?? []
            return ExtensionPointGroup(
                extensionPoint: point,
                displayName: pointName(point),
                nodes: ids.map { attachChildren(to: $0) }
            )
        }

        let orphans = orphanIds.compactMap { nodesById[$0] }

        return PeripheralTree(groups: groups, orphans: orphans,
                              pointDisplayNames: pointDisplayNames)
    }
}
