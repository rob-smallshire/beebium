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

/// Tests the ExtensionInstanceModel projection over the draft's extension
/// instances and attachment points.
final class ExtensionInstanceModelTests: XCTestCase {

    private typealias Ext = MachineDraft.Extension

    private func model() -> ExtensionInstanceModel {
        ExtensionInstanceModel(schema: ExtensionsSchemaSection(type: "extensions", attachmentPoints: [
            AttachmentPoint(id: "1mhz-bus", providedBy: nil, extensions: ["acorn-scsi"],
                            displayName: "1 MHz Bus", minOccupancy: 0, maxOccupancy: nil),
            AttachmentPoint(id: "user-port", providedBy: nil, extensions: ["acorn-rtc"],
                            displayName: "User Port", minOccupancy: 0, maxOccupancy: 1),
            AttachmentPoint(id: "scsi", providedBy: ["acorn-scsi"], extensions: ["scsi-hard-disc"],
                            displayName: "SCSI Bus", minOccupancy: 0, maxOccupancy: 7),
        ]))
    }

    func testModelPointsExcludeProvidedPoints() {
        XCTAssertEqual(model().modelPoints.map(\.id), ["1mhz-bus", "user-port"])
    }

    func testPointForExtension() {
        XCTAssertEqual(model().point(forExtension: "acorn-rtc")?.id, "user-port")
        XCTAssertEqual(model().point(forExtension: "scsi-hard-disc")?.id, "scsi")
        XCTAssertNil(model().point(forExtension: "nope"))
    }

    func testInstancesOnPoint() {
        let draft = MachineDraft(model: "model-b", extensions: [
            Ext(name: "acorn-rtc"),
            Ext(name: "acorn-scsi"),
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(0)]),
        ])
        let m = model()
        XCTAssertEqual(m.instances(onPoint: "user-port", in: draft).map(\.index), [0])
        XCTAssertEqual(m.instances(onPoint: "1mhz-bus", in: draft).map(\.index), [1])
        XCTAssertEqual(m.instances(onPoint: "scsi", in: draft).map(\.index), [2])
    }

    func testOccupancyAndAddable() {
        var draft = MachineDraft(model: "model-b")
        let m = model()
        // Empty user-port: 0 of 1, RTC addable.
        XCTAssertEqual(m.occupancy(onPoint: "user-port", in: draft).max, 1)
        XCTAssertEqual(m.addableExtensions(onPoint: "user-port", in: draft), ["acorn-rtc"])
        // Once occupied to its max, nothing more is addable.
        draft.extensions = [Ext(name: "acorn-rtc")]
        XCTAssertEqual(m.occupancy(onPoint: "user-port", in: draft).count, 1)
        XCTAssertTrue(m.addableExtensions(onPoint: "user-port", in: draft).isEmpty)
        // An unbounded point always has room.
        XCTAssertEqual(m.addableExtensions(onPoint: "1mhz-bus", in: draft), ["acorn-scsi"])
    }

    func testAddAppendsInstance() {
        let m = model()
        let draft = m.draft(adding: "acorn-rtc", to: MachineDraft(model: "model-b"))
        XCTAssertEqual((draft.extensions ?? []).map(\.name), ["acorn-rtc"])
        // A sole instance keeps the implicit id (its name).
        XCTAssertEqual(m.effectiveId(ofInstanceAt: 0, in: draft), "acorn-rtc")
    }

    func testAddingDuplicateAssignsDistinctId() {
        let m = model()
        var draft = m.draft(adding: "acorn-scsi", to: MachineDraft(model: "model-b"))
        draft = m.draft(adding: "acorn-scsi", to: draft)
        // First keeps the implicit id; the second gets a distinct one.
        XCTAssertEqual(m.effectiveId(ofInstanceAt: 0, in: draft), "acorn-scsi")
        XCTAssertEqual(m.effectiveId(ofInstanceAt: 1, in: draft), "acorn-scsi-1")
    }

    func testRemoveDoesNotCascade() {
        // Removing an adapter leaves its disc orphaned (resolve-preset reports
        // it); the client must not silently delete the disc too.
        let m = model()
        var draft = MachineDraft(model: "model-b", extensions: [
            Ext(name: "acorn-scsi"),
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(0)]),
        ])
        draft = m.draft(removingInstanceAt: 0, from: draft)
        XCTAssertEqual((draft.extensions ?? []).map(\.name), ["scsi-hard-disc"])
    }

    func testPointsProvidedByInstance() {
        let m = model()
        let adapter = ExtensionInstanceModel.Instance(index: 0, name: "acorn-scsi")
        XCTAssertEqual(m.pointsProvided(by: adapter).map(\.id), ["scsi"])
        let rtc = ExtensionInstanceModel.Instance(index: 1, name: "acorn-rtc")
        XCTAssertTrue(m.pointsProvided(by: rtc).isEmpty)
    }
}
