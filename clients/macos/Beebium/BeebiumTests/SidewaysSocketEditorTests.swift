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

/// Tests for SidewaysSocketEditor: the projection of a sideways schema over a
/// draft's slots into socket rows, and the write-through of edits back into the
/// draft. The draft is the single source of truth; every edited socket is
/// written as an explicit slot.
final class SidewaysSocketEditorTests: XCTestCase {

    private typealias Content = SidewaysSocketEditor.Content
    private typealias Slot = MachineDraft.SidewaysSlot

    /// A stock Model B: four aliased sockets, BASIC default in slot 15.
    private func modelB() -> SidewaysSocketEditor {
        SidewaysSocketEditor(schema: SidewaysSchemaSection(
            type: "sideways_bank",
            hasAliasing: true,
            sockets: [
                SidewaysSocketSchema(label: "IC52",  slots: [0, 4, 8, 12],  capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
                SidewaysSocketSchema(label: "IC88",  slots: [1, 5, 9, 13],  capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
                SidewaysSocketSchema(label: "IC100", slots: [2, 6, 10, 14], capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
                SidewaysSocketSchema(label: "IC101", slots: [3, 7, 11, 15], capabilities: ["rom", "ram", "empty"], runtimeConfigurable: false),
            ],
            defaultRoms: [SidewaysDefaultRom(slot: 15, image: "bbc-basic_2.rom", role: "language")]))
    }

    /// A board whose sockets carry a RAM write-protect switch.
    private func writeProtectBoard() -> SidewaysSocketEditor {
        SidewaysSocketEditor(schema: SidewaysSchemaSection(
            type: "sideways_bank", hasAliasing: false,
            sockets: [
                SidewaysSocketSchema(label: "S15", slots: [15], capabilities: ["rom", "ram", "empty", "write_protect"], runtimeConfigurable: true),
                SidewaysSocketSchema(label: "S14", slots: [14], capabilities: ["rom", "ram", "empty", "write_protect"], runtimeConfigurable: true),
                SidewaysSocketSchema(label: "S12", slots: [12], capabilities: ["rom", "ram", "empty", "write_protect"], runtimeConfigurable: true),
            ],
            defaultRoms: []))
    }

    private func socket(_ editor: SidewaysSocketEditor, _ label: String) -> SidewaysSocketSchema {
        editor.schema.sockets.first { $0.label == label }!
    }

    // MARK: - Projection

    func testProjectsPresetDefaultAndEmptySockets() {
        let editor = modelB()
        let slots = [Slot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        let rows = editor.rows(currentSlots: slots, originalSlots: slots)

        XCTAssertEqual(rows.map(\.label), ["IC101", "IC100", "IC88", "IC52"])
        XCTAssertEqual(rows.map(\.priority), [15, 14, 13, 12])

        XCTAssertEqual(rows[0].content, Content(kind: .rom, image: "bbc-basic_2.rom"))   // default
        XCTAssertEqual(rows[1].content, Content(kind: .rom, image: "acorn-dfs_2_26.rom")) // preset
        XCTAssertEqual(rows[2].content, .empty)
        XCTAssertEqual(rows[3].content, .empty)
        XCTAssertFalse(rows.contains { $0.isChanged })
    }

    // MARK: - Write-through

    func testClearingDfsWritesExplicitEmptyAtPresetSlot() {
        let editor = modelB()
        let original = [Slot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        let new = editor.slots(settingSocket: socket(editor, "IC100"), to: .empty, in: original)
        XCTAssertEqual(new, [Slot(slot: 14, type: "empty")])

        let rows = editor.rows(currentSlots: new, originalSlots: original)
        XCTAssertTrue(rows.first { $0.label == "IC100" }!.isChanged)
    }

    func testLoadingRomIntoEmptySocketWritesAtPrioritySlot() {
        let editor = modelB()
        let new = editor.slots(settingSocket: socket(editor, "IC52"),
                               to: Content(kind: .rom, image: "/tmp/toolkit.rom"), in: [])
        XCTAssertEqual(new, [Slot(slot: 12, type: "rom", imageUri: "/tmp/toolkit.rom")])
    }

    func testSidewaysRamWithAndWithoutPreload() {
        let editor = modelB()
        let blank = editor.slots(settingSocket: socket(editor, "IC52"),
                                 to: Content(kind: .ram, image: nil), in: [])
        XCTAssertEqual(blank, [Slot(slot: 12, type: "ram")])

        let preloaded = editor.slots(settingSocket: socket(editor, "IC52"),
                                     to: Content(kind: .ram, image: "/tmp/under-test.rom"), in: [])
        XCTAssertEqual(preloaded, [Slot(slot: 12, type: "ram", imageUri: "/tmp/under-test.rom")])
    }

    func testRomWithoutImageRecordsNoOverride() {
        let editor = modelB()
        // A ROM with no image is incomplete; setting a socket that had an entry
        // to it removes the entry rather than writing an invalid slot.
        let original = [Slot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        let new = editor.slots(settingSocket: socket(editor, "IC100"),
                               to: Content(kind: .rom, image: nil), in: original)
        XCTAssertEqual(new, [])
    }

    func testRevertingRestoresOriginalSlot() {
        let editor = modelB()
        let original = [Slot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        // Clear IC100, then revert it to its original content.
        let cleared = editor.slots(settingSocket: socket(editor, "IC100"), to: .empty, in: original)
        let restored = editor.slots(settingSocket: socket(editor, "IC100"),
                                    to: editor.content(for: socket(editor, "IC100"), in: original),
                                    in: cleared)
        XCTAssertEqual(restored, original)
        XCTAssertFalse(editor.rows(currentSlots: restored, originalSlots: original).contains { $0.isChanged })
    }

    // MARK: - Write-protect

    func testWriteProtectedRamWritesFlag() {
        let editor = writeProtectBoard()
        let new = editor.slots(settingSocket: socket(editor, "S12"),
                               to: Content(kind: .ram, image: nil, writeProtected: true), in: [])
        XCTAssertEqual(new, [Slot(slot: 12, type: "ram", writeProtected: true)])
    }

    func testUnprotectedRamWritesNoFlag() {
        let editor = writeProtectBoard()
        let new = editor.slots(settingSocket: socket(editor, "S12"),
                               to: Content(kind: .ram, image: nil, writeProtected: false), in: [])
        XCTAssertEqual(new, [Slot(slot: 12, type: "ram", writeProtected: nil)])
    }

    func testWriteProtectNeverWrittenForRom() {
        let editor = writeProtectBoard()
        let new = editor.slots(settingSocket: socket(editor, "S12"),
                               to: Content(kind: .rom, image: "/tmp/toolkit.rom", writeProtected: true), in: [])
        XCTAssertEqual(new, [Slot(slot: 12, type: "rom", imageUri: "/tmp/toolkit.rom")])
    }

    func testWriteProtectNotWrittenWhenSocketLacksSwitch() {
        // Model B RAM has no write-protect switch; a stale flag must not persist.
        let editor = modelB()
        let new = editor.slots(settingSocket: socket(editor, "IC52"),
                               to: Content(kind: .ram, image: nil, writeProtected: true), in: [])
        XCTAssertEqual(new, [Slot(slot: 12, type: "ram", writeProtected: nil)])
    }

    // MARK: - Reorder

    func testMovingContentsMovesImagesBetweenSockets() {
        let editor = modelB()
        let original = [Slot(slot: 14, type: "rom", imageUri: "acorn-dfs_2_26.rom")]
        // Rows by priority: IC101(BASIC), IC100(DFS), IC88(empty), IC52(empty).
        // Drag DFS (index 1) to the bottom.
        let moved = editor.slots(movingContentsFromOffsets: IndexSet(integer: 1), toOffset: 4,
                                 currentSlots: original, originalSlots: original)
        let rows = editor.rows(currentSlots: moved, originalSlots: original)

        XCTAssertEqual(rows.map(\.label), ["IC101", "IC100", "IC88", "IC52"])
        XCTAssertEqual(rows.first { $0.label == "IC52" }!.content,
                       Content(kind: .rom, image: "acorn-dfs_2_26.rom"))  // gained DFS
        XCTAssertEqual(rows.first { $0.label == "IC100" }!.content, .empty) // lost DFS
        XCTAssertEqual(rows.first { $0.label == "IC101" }!.content,
                       Content(kind: .rom, image: "bbc-basic_2.rom"))       // BASIC stays
    }

    func testMovingKeepsWriteProtectWithTheSocket() {
        let editor = writeProtectBoard()
        // S15 protected a.rom, S14 unprotected b.rom (priority order S15, S14, S12).
        var slots = editor.slots(settingSocket: socket(editor, "S15"),
                                 to: Content(kind: .ram, image: "/tmp/a.rom", writeProtected: true), in: [])
        slots = editor.slots(settingSocket: socket(editor, "S14"),
                             to: Content(kind: .ram, image: "/tmp/b.rom", writeProtected: false), in: slots)

        // Swap the two images by moving the top past the second.
        let moved = editor.slots(movingContentsFromOffsets: IndexSet(integer: 0), toOffset: 2,
                                 currentSlots: slots, originalSlots: [])
        let rows = editor.rows(currentSlots: moved, originalSlots: [])

        let s15 = rows.first { $0.label == "S15" }!
        let s14 = rows.first { $0.label == "S14" }!
        XCTAssertEqual(s15.content.image, "/tmp/b.rom")
        XCTAssertTrue(s15.content.writeProtected, "write-protect stays with the socket, not the image")
        XCTAssertEqual(s14.content.image, "/tmp/a.rom")
        XCTAssertFalse(s14.content.writeProtected)
    }

    // MARK: - Display (title cache)

    @MainActor
    func testTitleCacheDisplayPrefersResolvedTitle() async {
        let cache = RomTitleCache()
        cache.resolver = { image in
            image == "acorn-dfs_2_26.rom"
                ? RomHeaderInfo(recognised: true, title: "DFS", version: "2.26",
                                copyright: "", isLanguage: false, isServiceOnly: true,
                                containsRomfs: false, kinds: ["service"])
                : nil
        }
        let content = Content(kind: .rom, image: "acorn-dfs_2_26.rom")
        XCTAssertEqual(cache.displayText(for: content), "acorn-dfs_2_26.rom")  // before resolution
        await cache.resolve("acorn-dfs_2_26.rom")
        XCTAssertEqual(cache.displayText(for: content), "DFS 2.26")
        XCTAssertEqual(cache.kinds(for: content), ["service"])
        // An empty socket always reads "Empty".
        XCTAssertEqual(cache.displayText(for: .empty), "Empty")
    }
}
