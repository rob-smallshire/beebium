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

import XCTest
@testable import Beebium

/// Tests hard-disc capability detection from the extensions schema and the
/// ScsiDiscEditor projection/write-through over a draft's extension instances.
final class ScsiDiscEditorTests: XCTestCase {

    private typealias Ext = MachineDraft.Extension

    /// The stock SCSI chain: scsi-hard-disc on "scsi" provided by "acorn-scsi"
    /// attached to "1mhz-bus".
    private func scsiSchema() -> ExtensionsSchemaSection {
        ExtensionsSchemaSection(type: "extensions", attachmentPoints: [
            AttachmentPoint(id: "1mhz-bus", providedBy: nil, extensions: ["test-scratch-ram", "acorn-scsi"]),
            AttachmentPoint(id: "tube", providedBy: nil, extensions: ["acorn-65c02-coprocessor"]),
            AttachmentPoint(id: "scsi", providedBy: ["acorn-scsi"], extensions: ["scsi-hard-disc"]),
        ])
    }

    private func editor() -> ScsiDiscEditor {
        guard let capability = HardDiscCapability(schema: scsiSchema()) else {
            fatalError("expected a hard-disc capability from the SCSI schema")
        }
        return ScsiDiscEditor(capability: capability)
    }

    // MARK: - Capability detection

    func testDetectsScsiChainGenerically() throws {
        let capability = try XCTUnwrap(HardDiscCapability(schema: scsiSchema()))
        XCTAssertEqual(capability.discExtension, "scsi-hard-disc")
        XCTAssertEqual(capability.discPoint, "scsi")
        XCTAssertEqual(capability.adapterExtension, "acorn-scsi")
        XCTAssertEqual(capability.adapterPoint, "1mhz-bus")
    }

    func testNoCapabilityWhenNoProvidedDiscPoint() {
        let schema = ExtensionsSchemaSection(type: "extensions", attachmentPoints: [
            AttachmentPoint(id: "tube", providedBy: nil, extensions: ["acorn-65c02-coprocessor"]),
            AttachmentPoint(id: "user-port", providedBy: nil, extensions: ["acorn-rtc"]),
        ])
        XCTAssertNil(HardDiscCapability(schema: schema))
    }

    // MARK: - Add / remove, adapter management

    func testAddingFirstDiscAddsAdapter() {
        let e = editor()
        let draft = e.draft(addingDiscTo: MachineDraft(model: "model-b"))
        let names = (draft.extensions ?? []).map(\.name)
        XCTAssertEqual(names.sorted(), ["acorn-scsi", "scsi-hard-disc"])
        XCTAssertEqual(e.discs(in: draft).map(\.scsiId), [0])
    }

    func testAddingSecondDiscKeepsOneAdapterAndPicksNextId() {
        let e = editor()
        var draft = e.draft(addingDiscTo: MachineDraft(model: "model-b"))
        draft = e.draft(addingDiscTo: draft)
        XCTAssertEqual(e.adapterIndices(in: draft).count, 1)
        XCTAssertEqual(e.discs(in: draft).map(\.scsiId), [0, 1])
    }

    func testRemovingLastDiscRemovesAdapter() {
        let e = editor()
        var draft = e.draft(addingDiscTo: MachineDraft(model: "model-b"))
        let disc = e.discs(in: draft)[0]
        draft = e.draft(removingDiscAt: disc.instanceIndex, from: draft)
        XCTAssertTrue((draft.extensions ?? []).isEmpty, "the orphaned adapter is removed with the last disc")
    }

    func testRemovingOneOfTwoDiscsKeepsAdapter() {
        let e = editor()
        var draft = e.draft(addingDiscTo: MachineDraft(model: "model-b"))
        draft = e.draft(addingDiscTo: draft)
        let first = e.discs(in: draft)[0]
        draft = e.draft(removingDiscAt: first.instanceIndex, from: draft)
        XCTAssertEqual(e.discs(in: draft).count, 1)
        XCTAssertEqual(e.adapterIndices(in: draft).count, 1)
    }

    func testNextFreeScsiIdSkipsUsed() {
        let e = editor()
        let draft = MachineDraft(model: "model-b", extensions: [
            Ext(name: "acorn-scsi"),
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(0)]),
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(2)]),
        ])
        XCTAssertEqual(e.nextFreeScsiId(in: draft), 1)
    }

    // MARK: - Image edits

    func testSettingImageWritesConfig() {
        let e = editor()
        var draft = e.draft(addingDiscTo: MachineDraft(model: "model-b"))
        let disc = e.discs(in: draft)[0]
        draft = e.draft(settingImage: "/discs/hd.dat", forDiscAt: disc.instanceIndex, in: draft)
        XCTAssertEqual(e.discs(in: draft)[0].image, "/discs/hd.dat")
        XCTAssertFalse(e.discs(in: draft)[0].isBundledMaster)
    }

    func testBundledMasterDetectedFromBareName() {
        let e = editor()
        let draft = MachineDraft(model: "model-b", extensions: [
            Ext(name: "acorn-scsi"),
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(0), "image": .string("l3fs-v1_26b.dat")]),
        ])
        let disc = e.discs(in: draft)[0]
        XCTAssertTrue(disc.isBundledMaster)
        XCTAssertEqual(disc.displayName, "l3fs-v1_26b.dat")
    }

    func testClearingImageRemovesConfigKey() {
        let e = editor()
        var draft = MachineDraft(model: "model-b", extensions: [
            Ext(name: "acorn-scsi"),
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(0), "image": .string("hd.dat")]),
        ])
        let disc = e.discs(in: draft)[0]
        draft = e.draft(settingImage: nil, forDiscAt: disc.instanceIndex, in: draft)
        XCTAssertNil(e.discs(in: draft)[0].image)
        // scsi-id survives; only the image key is removed.
        XCTAssertEqual(e.discs(in: draft)[0].scsiId, 0)
    }
}
