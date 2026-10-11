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

import XCTest
@testable import Beebium

final class PeripheralTreeTests: XCTestCase {

    // MARK: - helpers

    private func info(name: String,
                      id: String,
                      attachesTo: [String],
                      provides: [String] = [],
                      config: [String: String] = [:],
                      label: String? = nil,
                      description: String = "",
                      hasUI: Bool = false,
                      storageDevices: [Beebium_StorageDevice] = [],
                      attachments: [(point: String, provider: String)]? = nil)
                     -> Beebium_ExtensionInfo {
        var info = Beebium_ExtensionInfo()
        info.name = name
        info.id = id
        info.label = label ?? id
        info.description_p = description
        info.attachesTo = attachesTo
        info.provides = provides
        info.config = config
        info.hasUi_p = hasUI
        info.storageDevices = storageDevices
        // The server sends one Attachment per attaches_to, naming the provider it
        // chose (empty for a machine connector). Tests that nest under a provider
        // pass them explicitly; otherwise default to connector attachments.
        let pairs = attachments ?? attachesTo.map { (point: $0, provider: "") }
        info.attachments = pairs.map { pair in
            var a = Beebium_Attachment()
            a.point = pair.point
            a.providerID = pair.provider
            return a
        }
        return info
    }

    private func point(_ id: String, _ displayName: String) -> Beebium_AttachmentPointInfo {
        var info = Beebium_AttachmentPointInfo()
        info.id = id
        info.displayName = displayName
        return info
    }

    private func storageDevice(id: String,
                               name: String,
                               kind: Beebium_StorageDevice.Kind = .fixed,
                               mediaType: String = "hard-disc",
                               backingPath: String = "/tmp/test.dat",
                               activityIndicator: String = "")
                              -> Beebium_StorageDevice {
        var dev = Beebium_StorageDevice()
        dev.id = id
        dev.name = name
        dev.kind = kind
        dev.mediaType = mediaType
        dev.backingPath = backingPath
        dev.activityIndicatorName = activityIndicator
        return dev
    }

    // MARK: - shape

    func testEmptyInputProducesEmptyTree() {
        let tree = PeripheralTree.build(from: [])
        XCTAssertTrue(tree.groups.isEmpty)
        XCTAssertTrue(tree.orphans.isEmpty)
    }

    func testSingleExtensionAttachesToBuiltInExtensionPoint() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let tree = PeripheralTree.build(from: [scsi])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].extensionPoint, "1mhz-bus")
        XCTAssertEqual(tree.groups[0].nodes.count, 1)
        XCTAssertEqual(tree.groups[0].nodes[0].id, "acorn-scsi#1")
        XCTAssertTrue(tree.groups[0].nodes[0].children.isEmpty)
        XCTAssertEqual(tree.groups[0].nodes[0].primaryAttachment, "1mhz-bus")
        XCTAssertTrue(tree.groups[0].nodes[0].secondaryAttachments.isEmpty)
    }

    // MARK: - nesting via provides/attachesTo

    func testChildNestsUnderProvidingParent() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let hdd = info(name: "scsi-hard-disc",
                       id: "scsi-hdd#0",
                       attachesTo: ["scsi"],
                       config: ["scsi-id": "0", "image": "/foo.dat"],
                       attachments: [(point: "scsi", provider: "acorn-scsi#1")])

        let tree = PeripheralTree.build(from: [scsi, hdd])

        XCTAssertEqual(tree.groups.count, 1)
        let root = tree.groups[0].nodes[0]
        XCTAssertEqual(root.id, "acorn-scsi#1")
        XCTAssertEqual(root.children.count, 1)

        let child = root.children[0]
        XCTAssertEqual(child.id, "scsi-hdd#0")
        XCTAssertEqual(child.primaryAttachment, "scsi")
        XCTAssertEqual(child.config["scsi-id"], "0")
        XCTAssertEqual(child.config["image"], "/foo.dat")
    }

    func testMultipleChildrenPreserveInputOrder() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let hdd0 = info(name: "scsi-hard-disc",
                        id: "scsi-hdd#0",
                        attachesTo: ["scsi"],
                        config: ["scsi-id": "0"],
                        attachments: [(point: "scsi", provider: "acorn-scsi#1")])
        let hdd1 = info(name: "scsi-hard-disc",
                        id: "scsi-hdd#1",
                        attachesTo: ["scsi"],
                        config: ["scsi-id": "1"],
                        attachments: [(point: "scsi", provider: "acorn-scsi#1")])

        let tree = PeripheralTree.build(from: [scsi, hdd0, hdd1])
        let children = tree.groups[0].nodes[0].children
        XCTAssertEqual(children.map { $0.id }, ["scsi-hdd#0", "scsi-hdd#1"])
    }

    func testChildCanArriveBeforeParentInInput() {
        // The server returns extensions in topological order, but the
        // tree builder must not assume that -- a HDD listed before its
        // SCSI host adapter should still nest correctly.
        let hdd = info(name: "scsi-hard-disc",
                       id: "scsi-hdd#0",
                       attachesTo: ["scsi"],
                       attachments: [(point: "scsi", provider: "acorn-scsi#1")])
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])

        let tree = PeripheralTree.build(from: [hdd, scsi])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].nodes.count, 1)
        XCTAssertEqual(tree.groups[0].nodes[0].id, "acorn-scsi#1")
        XCTAssertEqual(tree.groups[0].nodes[0].children.map { $0.id }, ["scsi-hdd#0"])
    }

    // MARK: - multi-attach (Voltmace-style)

    func testMultiAttachExtensionAppearsOnceUnderPrimary() {
        // Voltmace Delta 14B/1: plugs into both user port and analogue
        // port. The first entry in attachesTo is treated as primary, so
        // the device shows up exactly once under that group with the
        // remaining attachment(s) listed as secondary (badge).
        let voltmace = info(name: "voltmace-delta-14b1",
                            id: "voltmace#1",
                            attachesTo: ["user-port", "analogue-port"])

        let tree = PeripheralTree.build(from: [voltmace])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].extensionPoint, "user-port")
        XCTAssertEqual(tree.groups[0].nodes[0].primaryAttachment, "user-port")
        XCTAssertEqual(tree.groups[0].nodes[0].secondaryAttachments, ["analogue-port"])

        // No phantom second appearance under analogue-port.
        let totalRoots = tree.groups.reduce(0) { $0 + $1.nodes.count }
        XCTAssertEqual(totalRoots, 1)
    }

    // MARK: - multiple roots

    func testMultipleRootsFallBackToAlphabeticalWhenNoPointOrderGiven() {
        // With no attachment-point list from the server (the defensive path),
        // groups sort by id so the order is at least deterministic.
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let rtc = info(name: "acorn-rtc",
                       id: "acorn-rtc#1",
                       attachesTo: ["user-port"])

        let tree = PeripheralTree.build(from: [scsi, rtc])

        XCTAssertEqual(tree.groups.map { $0.extensionPoint },
                       ["1mhz-bus", "user-port"])
    }

    func testGroupsFollowServerPresentationOrder() {
        // The server returns attachment points in catalogue order (not
        // alphabetical); the tree must preserve it. Here user-port precedes
        // 1mhz-bus, the reverse of alphabetical.
        let rtc = info(name: "acorn-rtc", id: "acorn-rtc#1", attachesTo: ["user-port"])
        let scsi = info(name: "acorn-scsi", id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"], provides: ["scsi"])

        let tree = PeripheralTree.build(from: [scsi, rtc], attachmentPoints: [
            point("user-port", "User Port"),
            point("1mhz-bus", "1 MHz Bus"),
        ])

        XCTAssertEqual(tree.groups.map { $0.extensionPoint },
                       ["user-port", "1mhz-bus"])
    }

    func testGroupTakesDisplayNameFromServer() {
        let tube = info(name: "acorn-65c102", id: "acorn-65c102#1", attachesTo: ["tube"])
        let tree = PeripheralTree.build(from: [tube], attachmentPoints: [
            point("tube", "Coprocessor"),
        ])
        XCTAssertEqual(tree.groups[0].extensionPoint, "tube")
        XCTAssertEqual(tree.groups[0].displayName, "Coprocessor")
    }

    func testGroupDisplayNameFallsBackToSlugWhenServerUnnamed() {
        // A point the server did not describe still renders, named by its slug.
        let gadget = info(name: "gadget", id: "gadget#1", attachesTo: ["mystery-bus"])
        let tree = PeripheralTree.build(from: [gadget])
        XCTAssertEqual(tree.groups[0].displayName, "mystery-bus")
    }

    func testTwoAdaptersNestEachDiscUnderItsOwnProvider() {
        // The motivating bug: two SCSI host adapters each provide a "scsi" point.
        // Nesting by "whoever last provides scsi" put both discs under the second
        // adapter; using the server's per-attachment provider_id nests each disc
        // under the adapter the server actually attached it to.
        let adapterA = info(name: "acorn-scsi", id: "scsi-a",
                            attachesTo: ["1mhz-bus"], provides: ["scsi"])
        let adapterB = info(name: "acorn-scsi", id: "scsi-b",
                            attachesTo: ["1mhz-bus"], provides: ["scsi"])
        let discA = info(name: "scsi-hard-disc", id: "hdd-a", attachesTo: ["scsi"],
                         attachments: [(point: "scsi", provider: "scsi-a")])
        let discB = info(name: "scsi-hard-disc", id: "hdd-b", attachesTo: ["scsi"],
                         attachments: [(point: "scsi", provider: "scsi-b")])

        let tree = PeripheralTree.build(from: [adapterA, adapterB, discA, discB],
                                        attachmentPoints: [point("1mhz-bus", "1 MHz Bus")])

        let roots = tree.groups[0].nodes
        XCTAssertEqual(roots.map { $0.id }, ["scsi-a", "scsi-b"])
        XCTAssertEqual(roots[0].children.map { $0.id }, ["hdd-a"])
        XCTAssertEqual(roots[1].children.map { $0.id }, ["hdd-b"])
    }

    func testSecondaryAttachmentLabelsComeFromServer() {
        let voltmace = info(name: "voltmace-delta-14b1", id: "voltmace#1",
                            attachesTo: ["user-port", "analogue-port"])
        let tree = PeripheralTree.build(from: [voltmace], attachmentPoints: [
            point("user-port", "User Port"),
            point("analogue-port", "Analogue Port"),
        ])
        let node = tree.groups[0].nodes[0]
        XCTAssertEqual(node.secondaryAttachments, ["analogue-port"])
        XCTAssertEqual(node.secondaryAttachmentLabels, ["Analogue Port"])
    }

    // MARK: - defensive

    func testExtensionAttachingToUnknownPointBecomesRootGroup() {
        // The server's registry refuses to init unsatisfied dependencies,
        // so this case is degenerate. But if it ever leaks through, the
        // extension must remain visible -- not silently dropped.
        let oddball = info(name: "weird",
                           id: "weird#1",
                           attachesTo: ["mystery-bus"])

        let tree = PeripheralTree.build(from: [oddball])

        XCTAssertEqual(tree.groups.count, 1)
        XCTAssertEqual(tree.groups[0].extensionPoint, "mystery-bus")
        XCTAssertEqual(tree.groups[0].nodes[0].id, "weird#1")
    }

    // MARK: - display name fallback

    func testDisplayNameUsesExplicitLabelWhenSet() {
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi#1",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"],
                        label: "Spare-Parts SCSI",
                        description: "Acorn SCSI host adapter")
        let tree = PeripheralTree.build(from: [scsi])
        XCTAssertEqual(tree.groups[0].nodes[0].displayName, "Spare-Parts SCSI")
    }

    func testDisplayNameTrustsServerLabel() {
        // The server's Extension::label() resolves the chain
        // (explicit-label > manifest.display_name > id) and sends the
        // result here. The client just uses what arrives.
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi-1234",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"],
                        label: "Acorn SCSI Host Adapter",
                        description: "Acorn SCSI Host Adapter for 1 MHz bus (0xFC40-0xFC43)")
        let tree = PeripheralTree.build(from: [scsi])
        XCTAssertEqual(tree.groups[0].nodes[0].displayName,
                       "Acorn SCSI Host Adapter")
    }

    func testDisplayNameIgnoresDescription() {
        // The description is catalogue prose, not a sidebar label.
        // When the server sends a label that equals the id (i.e.
        // neither an explicit label nor a manifest display_name was
        // available), the client falls through to humanise(name),
        // NOT to the description -- even when the description would
        // be more informative.
        let unknown = info(name: "wacky-widget",
                           id: "wacky-widget-1",
                           attachesTo: ["1mhz-bus"],
                           label: "wacky-widget-1",
                           description: "A Most Splendid Widget")
        let tree = PeripheralTree.build(from: [unknown])
        XCTAssertEqual(tree.groups[0].nodes[0].displayName, "Wacky Widget")
    }

    func testDisplayNameFallsBackToHumanisedNameForLabellessExtension() {
        // Defensive last-resort path: an extension whose server-sent
        // label is identical to its id (no manifest display_name, no
        // explicit override) still renders as something readable.
        let oddball = info(name: "frobnicator-mark-ii",
                           id: "frobnicator-mark-ii-1",
                           attachesTo: ["user-port"],
                           label: "frobnicator-mark-ii-1")
        let tree = PeripheralTree.build(from: [oddball])
        XCTAssertEqual(tree.groups[0].nodes[0].displayName,
                       "Frobnicator Mark Ii")
    }

    func testHumaniseUpperCasesKnownAcronyms() {
        XCTAssertEqual(PeripheralNameFormatter.humanise("acorn-scsi"),
                       "Acorn SCSI")
        XCTAssertEqual(PeripheralNameFormatter.humanise("scsi-hard-disc"),
                       "SCSI Hard Disc")
        XCTAssertEqual(PeripheralNameFormatter.humanise("aun-transport"),
                       "AUN Transport")
    }

    func testHumaniseTitleCasesUnknownTokens() {
        XCTAssertEqual(PeripheralNameFormatter.humanise("voltmace-delta-14b1"),
                       "Voltmace Delta 14b1")
    }

    // MARK: - has_ui propagation

    func testHasUIPropagatesFromExtensionInfo() {
        let withUI = info(name: "ui-bearing",
                          id: "ui-bearing",
                          attachesTo: ["1mhz-bus"],
                          hasUI: true)
        let withoutUI = info(name: "acorn-rtc",
                             id: "acorn-rtc",
                             attachesTo: ["user-port"],
                             hasUI: false)

        let tree = PeripheralTree.build(from: [withUI, withoutUI])

        let pointMap = Dictionary(uniqueKeysWithValues:
            tree.groups.map { ($0.extensionPoint, $0.nodes[0]) })

        XCTAssertEqual(pointMap["1mhz-bus"]?.hasUI, true)
        XCTAssertEqual(pointMap["user-port"]?.hasUI, false)
    }

    func testHasUIDefaultsToFalseWhenUnsetByServer() {
        // Defensive: an info that never sets has_ui (a synthetic test
        // input, or a server predating the field) must not silently
        // claim to offer a UI panel.
        var raw = Beebium_ExtensionInfo()
        raw.name = "legacy"
        raw.id = "legacy"
        raw.attachesTo = ["1mhz-bus"]
        // hasUi_p never assigned -- default is false.

        let tree = PeripheralTree.build(from: [raw])
        XCTAssertFalse(tree.groups[0].nodes[0].hasUI)
    }

    // MARK: - storage device propagation

    func testStorageDevicesDefaultToEmpty() {
        // An extension that doesn't publish storage devices yields a
        // node with an empty storageDevices array, not nil. The
        // Storage sidebar iterates these freely without nil-handling.
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])
        let tree = PeripheralTree.build(from: [scsi])
        XCTAssertTrue(tree.groups[0].nodes[0].storageDevices.isEmpty)
    }

    func testStorageDevicePropagatesAllFields() {
        let hdd = info(name: "scsi-hard-disc",
                       id: "scsi-hard-disc",
                       attachesTo: ["scsi"],
                       storageDevices: [
                           storageDevice(id: "scsi-hard-disc",
                                         name: "Hard Disc Drive 0",
                                         kind: .fixed,
                                         mediaType: "hard-disc",
                                         backingPath: "/discs/boot.dat",
                                         activityIndicator: "hdd-0-activity-led")
                       ],
                       attachments: [(point: "scsi", provider: "acorn-scsi")])
        let scsi = info(name: "acorn-scsi",
                        id: "acorn-scsi",
                        attachesTo: ["1mhz-bus"],
                        provides: ["scsi"])

        let tree = PeripheralTree.build(from: [scsi, hdd])
        let hddNode = tree.groups[0].nodes[0].children[0]

        XCTAssertEqual(hddNode.storageDevices.count, 1)
        let dev = hddNode.storageDevices[0]
        XCTAssertEqual(dev.id, "scsi-hard-disc")
        XCTAssertEqual(dev.name, "Hard Disc Drive 0")
        XCTAssertEqual(dev.kind, .fixed)
        XCTAssertEqual(dev.mediaType, "hard-disc")
        XCTAssertEqual(dev.backingPath, "/discs/boot.dat")
        XCTAssertEqual(dev.activityIndicatorName, "hdd-0-activity-led")
    }

    func testStorageDeviceRemovableKindMaps() {
        let oddball = info(name: "future-removable",
                           id: "future-removable",
                           attachesTo: ["1mhz-bus"],
                           storageDevices: [
                               storageDevice(id: "fr-0",
                                             name: "Cartridge slot",
                                             kind: .removable,
                                             mediaType: "rom-cartridge",
                                             backingPath: "")
                           ])
        let tree = PeripheralTree.build(from: [oddball])
        XCTAssertEqual(tree.groups[0].nodes[0].storageDevices[0].kind, .removable)
    }

    func testStorageDeviceMultipleEntriesPreserveOrder() {
        // The motivating case for "one extension, many devices":
        // a future Opus Challenger would publish its floppy drive
        // and RAM disc as two entries here, in declaration order.
        let combo = info(name: "future-combo",
                         id: "future-combo",
                         attachesTo: ["1mhz-bus"],
                         storageDevices: [
                             storageDevice(id: "combo/floppy",
                                           name: "Combo Floppy",
                                           kind: .removable,
                                           mediaType: "floppy"),
                             storageDevice(id: "combo/ramdisc",
                                           name: "Combo RAM Disc",
                                           kind: .fixed,
                                           mediaType: "ram-disc"),
                         ])
        let tree = PeripheralTree.build(from: [combo])
        let devs = tree.groups[0].nodes[0].storageDevices
        XCTAssertEqual(devs.count, 2)
        XCTAssertEqual(devs[0].id, "combo/floppy")
        XCTAssertEqual(devs[1].id, "combo/ramdisc")
    }

    func testExtensionWithEmptyAttachesToBecomesOrphan() {
        // PeripheralExtension's contract is non-empty attachesTo. If
        // that's ever violated, surface the extension under `orphans`
        // rather than indexing it under an empty group name.
        var oddball = Beebium_ExtensionInfo()
        oddball.name = "broken"
        oddball.id = "broken#1"
        oddball.label = "broken#1"
        oddball.attachesTo = []

        let tree = PeripheralTree.build(from: [oddball])

        XCTAssertTrue(tree.groups.isEmpty)
        XCTAssertEqual(tree.orphans.count, 1)
        XCTAssertEqual(tree.orphans[0].id, "broken#1")
    }
}
