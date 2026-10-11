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
    let secondaryAttachments: [String]
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

/// Display-name lookup for built-in extension points -- the parent
/// sockets things plug into (1mhz-bus, user-port, ...). Unlike
/// extension types, these names are not declared in any manifest;
/// they're framework-level concepts owned by the server core, so it's
/// appropriate for the client to spell them out.
///
/// Returning the slug unchanged for unknown keys keeps the view
/// honest: a future extension point we haven't named here still
/// renders, just less prettily.
enum PeripheralLabels {
    private static let extensionPointNames: [String: String] = [
        "1mhz-bus": "1 MHz Bus",
        "user-port": "User Port",
        "tube": "Coprocessor",
        "scsi": "SCSI Bus",
        "analogue-port": "Analogue Port",
        "printer-port": "Printer Port",
        "serial-port": "Serial Port",
    ]

    static func extensionPoint(_ name: String) -> String {
        extensionPointNames[name] ?? name
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

/// Hierarchical view of the configured peripherals, rooted at the built-in
/// extension points (1mhz-bus, user-port, tube, ...).
///
/// `groups` is ordered alphabetically by extension point name for stable
/// display; nodes within a group preserve the order returned by the server
/// (which corresponds to command-line / preset configuration order).
///
/// `orphans` is a defensive bucket for extensions with an empty
/// `attaches_to`; the server's PeripheralExtension contract requires at
/// least one attachment so in practice it stays empty.
struct PeripheralTree: Equatable {

    struct ExtensionPointGroup: Equatable, Identifiable {
        let extensionPoint: String
        let nodes: [PeripheralNode]
        var id: String { extensionPoint }
    }

    let groups: [ExtensionPointGroup]
    let orphans: [PeripheralNode]

    static func build(from extensions: [Beebium_ExtensionInfo]) -> PeripheralTree {
        if extensions.isEmpty {
            return PeripheralTree(groups: [], orphans: [])
        }

        // Map each extension point provided by some loaded extension to
        // the id of that provider. Used to decide whether a child's
        // primary attachment resolves to another extension (parent-child
        // nesting) or to a built-in extension point (root group).
        var providerIdByPoint: [String: String] = [:]
        for ext in extensions {
            for point in ext.provides {
                providerIdByPoint[point] = ext.id
            }
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
                provides: ext.provides,
                hasUI: ext.hasUi_p,
                storageDevices: ext.storageDevices.map(makeStorageDevice),
                children: []
            )
        }

        // Classify each extension as either a child of another loaded
        // extension or a root under some extension point name. Empty
        // `attaches_to` is treated as orphan rather than indexed under
        // an empty group name.
        var childIdsByParentId: [String: [String]] = [:]
        var rootIdsByPoint: [String: [String]] = [:]
        var orphanIds: [String] = []

        for ext in extensions {
            guard let primary = ext.attachesTo.first, !primary.isEmpty else {
                orphanIds.append(ext.id)
                continue
            }
            if let parentId = providerIdByPoint[primary] {
                childIdsByParentId[parentId, default: []].append(ext.id)
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

        let sortedPoints = rootIdsByPoint.keys.sorted()
        let groups = sortedPoints.map { point -> ExtensionPointGroup in
            let ids = rootIdsByPoint[point] ?? []
            return ExtensionPointGroup(
                extensionPoint: point,
                nodes: ids.map { attachChildren(to: $0) }
            )
        }

        let orphans = orphanIds.compactMap { nodesById[$0] }

        return PeripheralTree(groups: groups, orphans: orphans)
    }
}
