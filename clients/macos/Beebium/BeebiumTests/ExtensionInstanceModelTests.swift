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
        let draft = m.draft(adding: "acorn-rtc", onPoint: "user-port", to: MachineDraft(model: "model-b"))
        XCTAssertEqual((draft.extensions ?? []).map(\.name), ["acorn-rtc"])
        // A sole instance keeps the implicit id (its name).
        XCTAssertEqual(m.effectiveId(ofInstanceAt: 0, in: draft), "acorn-rtc")
    }

    func testAddingDuplicateAssignsDistinctId() {
        let m = model()
        var draft = m.draft(adding: "acorn-scsi", onPoint: "1mhz-bus", to: MachineDraft(model: "model-b"))
        draft = m.draft(adding: "acorn-scsi", onPoint: "1mhz-bus", to: draft)
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

    // MARK: - Provider-scoped nesting (#266)

    /// A schema whose scsi-hard-disc declares an `adapter-id` parameter that is
    /// `provider_of` "scsi", so a disc can be attributed to one adapter. Decoded
    /// from JSON because ExtensionParameter/ExtensionCatalogueEntry decode from
    /// the schema wire form rather than offering a memberwise initialiser.
    private func providerModel() -> ExtensionInstanceModel {
        let json = """
        {
          "type": "extensions",
          "attachment_points": [
            {"id": "1mhz-bus", "provided_by": null, "extensions": ["acorn-scsi"],
             "display_name": "1 MHz Bus", "max_occupancy": null},
            {"id": "scsi", "provided_by": ["acorn-scsi"], "extensions": ["scsi-hard-disc"],
             "display_name": "SCSI Bus", "max_occupancy": 7}
          ],
          "catalogue": [
            {"name": "acorn-scsi", "display_name": "Acorn SCSI", "provides": ["scsi"]},
            {"name": "scsi-hard-disc", "display_name": "SCSI Hard Disc",
             "parameters": [
               {"key": "scsi-id", "type": "integer"},
               {"key": "adapter-id", "type": "string", "provider_of": "scsi"}
             ]}
          ]
        }
        """
        let schema = try! JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
        return ExtensionInstanceModel(schema: schema)
    }

    /// Two SCSI adapters: disc 2 names the first by adapter-id, disc 3 names the
    /// second, disc 4 names neither (so it falls to the last adapter).
    private func twoAdapterDraft() -> MachineDraft {
        MachineDraft(model: "model-b", extensions: [
            Ext(name: "acorn-scsi"),                                            // 0: id "acorn-scsi"
            Ext(name: "acorn-scsi", config: ["id": .string("acorn-scsi-1")]),   // 1: id "acorn-scsi-1"
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(0), "adapter-id": .string("acorn-scsi")]),    // 2
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(1), "adapter-id": .string("acorn-scsi-1")]),  // 3
            Ext(name: "scsi-hard-disc", config: ["scsi-id": .int(2)]),          // 4: no adapter-id -> last
        ])
    }

    func testDiscsNestUnderTheAdapterTheirAdapterIdNames() {
        let m = providerModel()
        let draft = twoAdapterDraft()
        let adapters = m.instances(onPoint: "1mhz-bus", in: draft)
        XCTAssertEqual(adapters.map(\.index), [0, 1])
        // The first adapter owns only the disc that names it; the second owns the
        // disc that names it plus the disc that names no adapter (the last wins).
        XCTAssertEqual(m.instances(onPoint: "scsi", under: adapters[0], in: draft).map(\.index), [2])
        XCTAssertEqual(m.instances(onPoint: "scsi", under: adapters[1], in: draft).map(\.index), [3, 4])
    }

    func testOccupancyIsCountedPerAdapter() {
        let m = providerModel()
        let draft = twoAdapterDraft()
        let adapters = m.instances(onPoint: "1mhz-bus", in: draft)
        XCTAssertEqual(m.occupancy(onPoint: "scsi", under: adapters[0], in: draft).count, 1)
        let second = m.occupancy(onPoint: "scsi", under: adapters[1], in: draft)
        XCTAssertEqual(second.count, 2)
        XCTAssertEqual(second.max, 7)
    }

    func testAddBeneathOneOfSeveralAdaptersNamesIt() {
        let m = providerModel()
        var draft = twoAdapterDraft()
        let second = m.instances(onPoint: "1mhz-bus", in: draft)[1]
        draft = m.draft(adding: "scsi-hard-disc", onPoint: "scsi", under: second, to: draft)
        let added = draft.extensions!.last!
        XCTAssertEqual(added.name, "scsi-hard-disc")
        XCTAssertEqual(added.config?["adapter-id"], .string("acorn-scsi-1"))
        // And it then nests under the adapter it named.
        XCTAssertTrue(m.instances(onPoint: "scsi", under: second, in: draft).contains { $0.index == 5 })
    }

    func testAddBeneathASingleAdapterWritesNameAlone() {
        let m = providerModel()
        // One adapter, no discs yet.
        var draft = MachineDraft(model: "model-b", extensions: [Ext(name: "acorn-scsi")])
        let sole = m.instances(onPoint: "1mhz-bus", in: draft)[0]
        draft = m.draft(adding: "scsi-hard-disc", onPoint: "scsi", under: sole, to: draft)
        let added = draft.extensions!.last!
        XCTAssertEqual(added.name, "scsi-hard-disc")
        // Byte-for-byte as before: a lone adapter writes no adapter-id (and no id),
        // so there is no config at all; the "else the last provider" rule still
        // nests the disc beneath it.
        XCTAssertNil(added.config)
        XCTAssertEqual(m.instances(onPoint: "scsi", under: sole, in: draft).map(\.index), [1])
    }
}
