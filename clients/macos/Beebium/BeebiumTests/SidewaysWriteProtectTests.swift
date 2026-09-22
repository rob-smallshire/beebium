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

/// The unified sideways-protection model: the pure SetSlotProtection outcome
/// mapping, and the rule for which protection group covers a socket (so the
/// padlock / Hide controls are built from GetSlotStatus.protection_groups, not
/// hardcoded board knowledge).
@MainActor
final class SidewaysProtectionTests: XCTestCase {

    private typealias Group = SidewaysClient.ProtectionGroup

    private func socket(slots: [Int]) -> SidewaysClient.Socket {
        SidewaysClient.Socket(
            socketIndex: UInt32(slots.max() ?? 0), label: "IC", slots: slots,
            kind: .ram, populated: false, imageName: "", romHeader: nil)
    }

    // MARK: - SetSlotProtection outcome

    func testSuccessIsApplied() {
        XCTAssertEqual(
            SidewaysClient.protectionOutcome(success: true, error: "", groupID: "slot-15"),
            .applied)
    }

    func testFailureWithMessageIsRejectedVerbatim() {
        XCTAssertEqual(
            SidewaysClient.protectionOutcome(success: false, error: "No such group",
                                             groupID: "board"),
            .rejected("No such group"))
    }

    func testFailureWithoutMessageGetsAGroupSpecificFallback() {
        XCTAssertEqual(
            SidewaysClient.protectionOutcome(success: false, error: "", groupID: "socket-14"),
            .rejected("Could not change protection for socket-14."))
    }

    // MARK: - Which group covers a socket

    func testOneSlotWriteGroupCoversItsSlot() {
        // ATPL slot-15 / a ROM-RAM board slot: a one-slot write group.
        let groups = [Group(id: "slot-15", label: "Write-protect", slots: [15],
                            supportsWriteProtect: true, supportsHide: false,
                            writeProtected: true, hidden: false)]
        let found = SidewaysClient.group(in: groups, forSocket: socket(slots: [15]),
                                         kind: .writeProtect)
        XCTAssertEqual(found?.id, "slot-15")
        XCTAssertEqual(found?.writeProtected, true)
        // It is not a hide group, so a hide lookup finds nothing.
        XCTAssertNil(SidewaysClient.group(in: groups, forSocket: socket(slots: [15]), kind: .hide))
    }

    func testWholeBoardGroupCoversEveryMemberSocket() {
        // Watford S2: one write group over all 16 slots -- every socket's padlock
        // resolves to the same group and so moves together.
        let board = Group(id: "board", label: "Write-protect (S2)", slots: Array(0...15),
                          supportsWriteProtect: true, supportsHide: false,
                          writeProtected: false, hidden: false)
        XCTAssertEqual(SidewaysClient.group(in: [board], forSocket: socket(slots: [4]),
                                            kind: .writeProtect)?.id, "board")
        XCTAssertEqual(SidewaysClient.group(in: [board], forSocket: socket(slots: [12]),
                                            kind: .writeProtect)?.id, "board")
    }

    func testAliasedSocketMatchesGroupOnAnySharedSlot() {
        // A Model B aliased socket answers [0,4,8,12]; a group naming any of
        // those covers it.
        let group = Group(id: "g", label: "WP", slots: [8],
                          supportsWriteProtect: true, supportsHide: false,
                          writeProtected: false, hidden: false)
        XCTAssertNotNil(SidewaysClient.group(in: [group],
                                             forSocket: socket(slots: [0, 4, 8, 12]),
                                             kind: .writeProtect))
    }

    func testHideGroupIsFoundOnlyForHideKind() {
        // Watford S1: a hide (read-protect) group over socket 14 only.
        let groups = [Group(id: "socket-14", label: "Hide (S1)", slots: [14],
                            supportsWriteProtect: false, supportsHide: true,
                            writeProtected: false, hidden: true)]
        XCTAssertEqual(SidewaysClient.group(in: groups, forSocket: socket(slots: [14]),
                                            kind: .hide)?.hidden, true)
        XCTAssertNil(SidewaysClient.group(in: groups, forSocket: socket(slots: [14]),
                                          kind: .writeProtect))
    }

    func testNoGroupWhenSlotsAreDisjoint() {
        let group = Group(id: "g", label: "WP", slots: [15],
                          supportsWriteProtect: true, supportsHide: false,
                          writeProtected: false, hidden: false)
        XCTAssertNil(SidewaysClient.group(in: [group], forSocket: socket(slots: [0]),
                                          kind: .writeProtect))
    }
}
