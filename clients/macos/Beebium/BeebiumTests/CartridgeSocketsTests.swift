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

/// The cartridge-socket projection (spec 5.9, #300/#301): a sideways socket the
/// server marks `provided_by_point` exists only when a cartridge is fitted at
/// that point, so the Memory section's row depends on the draft's extensions.
/// These pin the schema decoding of the new keys and the three socket states --
/// ordinary, cartridge absent, cartridge present -- the Memory view renders.
final class CartridgeSocketsTests: XCTestCase {

    private typealias Ext = MachineDraft.Extension

    // MARK: - Decoding the new schema keys

    /// `provided_by_point` decodes onto a cartridge socket; an ordinary socket
    /// leaves it nil.
    func testDecodesProvidedByPointOnSocket() throws {
        let json = #"""
        {
          "type": "sideways_bank",
          "has_aliasing": false,
          "sockets": [
            {"label": "ROM 0", "slots": [0], "capabilities": ["empty"],
             "runtime_configurable": true, "provided_by_point": "cartridge-rear"},
            {"label": "IC101", "slots": [3], "capabilities": ["rom", "ram", "empty"],
             "runtime_configurable": false}
          ],
          "default_roms": []
        }
        """#
        let section = try JSONDecoder().decode(SidewaysSchemaSection.self, from: Data(json.utf8))
        let cartridge = section.sockets[0]
        XCTAssertEqual(cartridge.providedByPoint, "cartridge-rear")
        XCTAssertTrue(cartridge.isCartridgeProvided)
        let motherboard = section.sockets[1]
        XCTAssertNil(motherboard.providedByPoint)
        XCTAssertFalse(motherboard.isCartridgeProvided)
    }

    /// `provides_sideways` decodes onto a cartridge's catalogue entry; an
    /// extension that provides no sideways sockets leaves it nil.
    func testDecodesProvidesSidewaysOnCatalogueEntry() throws {
        let schema = try Self.masterCartridgeSchema()
        let provides = try XCTUnwrap(schema.providesSideways(forExtension: "acorn-rom-cartridge"))
        XCTAssertEqual(provides, ProvidesSideways(banks: 2, bankCapabilities: ["rom", "empty"]))
        XCTAssertNil(schema.providesSideways(forExtension: "acorn-rtc"))
    }

    // MARK: - The three states

    /// A motherboard socket (no `provided_by_point`) is always ordinary, whatever
    /// the draft fits.
    func testOrdinaryForMotherboardSocket() throws {
        let sockets = CartridgeSockets(extensionsSchema: try Self.masterCartridgeSchema())
        let motherboard = SidewaysSocketSchema(
            label: "IC101", slots: [3], capabilities: ["rom", "ram", "empty"],
            runtimeConfigurable: false)
        XCTAssertEqual(sockets.state(of: motherboard, in: MachineDraft(model: "master-128")),
                       .ordinary)
    }

    /// A cartridge socket with no cartridge fitted at its point reads "cartridge
    /// absent", naming the point and its display name.
    func testCartridgeAbsentWhenNoCartridgeFitted() throws {
        let sockets = CartridgeSockets(extensionsSchema: try Self.masterCartridgeSchema())
        let draft = MachineDraft(model: "master-128")  // nothing in the slots
        XCTAssertEqual(sockets.state(of: Self.rearSocket, in: draft),
                       .cartridgeAbsent(point: "cartridge-rear", pointName: "Rear Cartridge Slot"))
    }

    /// Fitting the cartridge at the rear slot makes its banks present, with the
    /// manifest's bank capabilities and the cartridge named.
    func testCartridgePresentWhenFitted() throws {
        let sockets = CartridgeSockets(extensionsSchema: try Self.masterCartridgeSchema())
        // No `at`: the first alternative (cartridge-rear) is where it attaches.
        let draft = MachineDraft(model: "master-128", extensions: [Ext(name: "acorn-rom-cartridge")])
        XCTAssertEqual(
            sockets.state(of: Self.rearSocket, in: draft),
            .cartridgePresent(point: "cartridge-rear", pointName: "Rear Cartridge Slot",
                              cartridgeName: "acorn-rom-cartridge",
                              cartridgeDisplayName: "Acorn ROM Cartridge",
                              bankCapabilities: ["rom", "empty"]))
    }

    /// A cartridge in the FRONT slot does not provide the REAR socket: fitting one
    /// at `cartridge-front` leaves the rear banks absent and the front banks
    /// present. This exercises the extension's `at` alternative.
    func testCartridgeAtFrontDoesNotProvideRearSocket() throws {
        let sockets = CartridgeSockets(extensionsSchema: try Self.masterCartridgeSchema())
        let draft = MachineDraft(model: "master-128", extensions: [
            Ext(name: "acorn-rom-cartridge", config: ["at": .string("cartridge-front")]),
        ])
        XCTAssertEqual(sockets.state(of: Self.rearSocket, in: draft),
                       .cartridgeAbsent(point: "cartridge-rear", pointName: "Rear Cartridge Slot"))
        if case .cartridgePresent(let point, _, _, _, let caps) =
            sockets.state(of: Self.frontSocket, in: draft) {
            XCTAssertEqual(point, "cartridge-front")
            XCTAssertEqual(caps, ["rom", "empty"])
        } else {
            XCTFail("the front socket should be present with a cartridge at cartridge-front")
        }
    }

    /// An extension at a cartridge point that provides no sideways sockets does
    /// not make the socket present -- only a cartridge (one whose manifest
    /// provides sideways) does.
    func testNonProvidingExtensionLeavesSocketAbsent() throws {
        let sockets = CartridgeSockets(extensionsSchema: try Self.masterCartridgeSchema())
        // A hypothetical non-cartridge extension sitting on the rear point.
        let draft = MachineDraft(model: "master-128", extensions: [Ext(name: "not-a-cartridge")])
        XCTAssertEqual(sockets.state(of: Self.rearSocket, in: draft),
                       .cartridgeAbsent(point: "cartridge-rear", pointName: "Rear Cartridge Slot"))
    }

    /// The banks a cartridge provides at a point are the sockets whose
    /// `provided_by_point` names it, lowest bank first -- what the cartridge's
    /// Peripherals view links to in Memory.
    func testProvidedSocketsAtPoint() throws {
        let sockets = CartridgeSockets(extensionsSchema: try Self.masterCartridgeSchema())
        let schema = SidewaysSchemaSection(
            type: "sideways_bank", hasAliasing: false,
            sockets: [
                SidewaysSocketSchema(label: "IC101", slots: [3], capabilities: ["rom"],
                                     runtimeConfigurable: false),
                SidewaysSocketSchema(label: "ROM 1", slots: [1], capabilities: ["empty"],
                                     runtimeConfigurable: true, providedByPoint: "cartridge-rear"),
                SidewaysSocketSchema(label: "ROM 0", slots: [0], capabilities: ["empty"],
                                     runtimeConfigurable: true, providedByPoint: "cartridge-rear"),
                SidewaysSocketSchema(label: "ROM 2", slots: [2], capabilities: ["empty"],
                                     runtimeConfigurable: true, providedByPoint: "cartridge-front"),
            ],
            defaultRoms: [])
        XCTAssertEqual(sockets.providedSockets(atPoint: "cartridge-rear", in: schema).map(\.label),
                       ["ROM 0", "ROM 1"])  // lowest bank first
        XCTAssertEqual(sockets.providedSockets(atPoint: "cartridge-front", in: schema).map(\.label),
                       ["ROM 2"])
        XCTAssertTrue(sockets.providedSockets(atPoint: "modem-connector", in: schema).isEmpty)
    }

    // MARK: - Reorder anchoring (an unfitted cartridge bank takes no drop)

    /// With no unfitted cartridge banks, every reorder is allowed.
    func testReorderAllowedWithNoAnchors() {
        XCTAssertTrue(MemorySectionView.reorderAllowed(
            source: IndexSet(integer: 0), destination: 3, anchorRowIndices: []))
    }

    /// A reorder entirely above the anchor block is allowed.
    func testReorderAboveAnchorsAllowed() {
        // Anchors at the bottom (rows 4, 5); reorder among 0...3.
        XCTAssertTrue(MemorySectionView.reorderAllowed(
            source: IndexSet(integer: 1), destination: 3, anchorRowIndices: [4, 5]))
    }

    /// A reorder that crosses an anchor is rejected (it would discard the ROM).
    func testReorderCrossingAnchorRejected() {
        XCTAssertFalse(MemorySectionView.reorderAllowed(
            source: IndexSet(integer: 0), destination: 5, anchorRowIndices: [4]))
    }

    /// An anchor row cannot be the drag source.
    func testAnchorCannotBeDragged() {
        XCTAssertFalse(MemorySectionView.reorderAllowed(
            source: IndexSet(integer: 4), destination: 1, anchorRowIndices: [4]))
    }

    /// A reorder between two fitted cartridge rows below the anchors is allowed.
    func testReorderBelowAnchorsAllowed() {
        // Anchors 2, 3 (front, unfitted); rows 4, 5 (rear, fitted) reorderable.
        XCTAssertTrue(MemorySectionView.reorderAllowed(
            source: IndexSet(integer: 5), destination: 4, anchorRowIndices: [2, 3]))
    }

    // MARK: - Fixtures

    private static let rearSocket = SidewaysSocketSchema(
        label: "ROM 0", slots: [0], capabilities: ["empty"],
        runtimeConfigurable: true, providedByPoint: "cartridge-rear")

    private static let frontSocket = SidewaysSocketSchema(
        label: "ROM 2", slots: [2], capabilities: ["empty"],
        runtimeConfigurable: true, providedByPoint: "cartridge-front")

    /// A Master-128-like extensions schema: the two cartridge slots and the Acorn
    /// ROM cartridge that attaches to one of them and provides two ROM banks. A
    /// `not-a-cartridge` entry (hosted on the rear point, providing no sideways
    /// sockets) exercises the "only a cartridge makes the socket present" rule.
    private static func masterCartridgeSchema() throws -> ExtensionsSchemaSection {
        let json = #"""
        {
          "type": "extensions",
          "attachment_points": [
            {"id": "cartridge-rear", "display_name": "Rear Cartridge Slot",
             "extensions": ["acorn-rom-cartridge", "not-a-cartridge"], "max_occupancy": 1},
            {"id": "cartridge-front", "display_name": "Front Cartridge Slot",
             "extensions": ["acorn-rom-cartridge"], "max_occupancy": 1},
            {"id": "user-port", "display_name": "User Port",
             "extensions": ["acorn-rtc"], "max_occupancy": 1}
          ],
          "catalogue": [
            {"name": "acorn-rom-cartridge", "display_name": "Acorn ROM Cartridge",
             "kind": "peripheral", "attaches_to_one_of": ["cartridge-rear", "cartridge-front"],
             "provides_sideways": {"banks": 2, "bank_capabilities": ["rom", "empty"]}},
            {"name": "not-a-cartridge", "display_name": "Not A Cartridge", "kind": "peripheral"},
            {"name": "acorn-rtc", "display_name": "Real-Time Clock", "kind": "peripheral"}
          ]
        }
        """#
        return try JSONDecoder().decode(ExtensionsSchemaSection.self, from: Data(json.utf8))
    }
}
