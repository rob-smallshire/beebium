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

/// Unit tests for the BBC SHIFT / CTRL decision logic that backs
/// KeyboardClient. Covers regressions #10 (SHIFT sticks across character
/// release) and #18 (typing `:` produces `*`), plus the synthesis case where
/// the BBC needs SHIFT for a character that the host typed without SHIFT
/// (e.g. ' is BBC Shift+7 on a US keyboard).
final class BBCModifierStateTests: XCTestCase {

    // MARK: - Helpers

    private func directShift() -> PressedKeyFact {
        PressedKeyFact(bbcKeyName: "Shift", fromCharacterMapping: false,
                       needsSyntheticShift: false, forbidsShift: false,
                       needsSyntheticCtrl: false, forbidsCtrl: false)
    }

    private func directCtrl() -> PressedKeyFact {
        PressedKeyFact(bbcKeyName: "Ctrl", fromCharacterMapping: false,
                       needsSyntheticShift: false, forbidsShift: false,
                       needsSyntheticCtrl: false, forbidsCtrl: false)
    }

    /// Build the fact a character key would produce. Use the helpers from
    /// BBCModifierState to keep the at-press-time decision logic in one
    /// place.
    private func characterKey(
        _ name: String,
        bbcShiftRequired: Bool,
        physicalShiftHeld: Bool,
        bbcCtrlRequired: Bool = false,
        physicalCtrlHeld: Bool = false
    ) -> PressedKeyFact {
        let s = BBCModifierState.shiftFlagsForCharacter(
            bbcShiftRequired: bbcShiftRequired,
            physicalShiftHeld: physicalShiftHeld
        )
        let c = BBCModifierState.ctrlFlagsForCharacter(
            bbcCtrlRequired: bbcCtrlRequired,
            physicalCtrlHeld: physicalCtrlHeld
        )
        return PressedKeyFact(
            bbcKeyName: name,
            fromCharacterMapping: true,
            needsSyntheticShift: s.needsSynthetic,
            forbidsShift: s.forbids,
            needsSyntheticCtrl: c.needsSynthetic,
            forbidsCtrl: c.forbids
        )
    }

    /// Direct-mapped non-modifier key (e.g., F1 mapped with bbcWithShift=true).
    private func directKey(_ name: String, withShift: Bool = false, withCtrl: Bool = false) -> PressedKeyFact {
        PressedKeyFact(
            bbcKeyName: name,
            fromCharacterMapping: false,
            needsSyntheticShift: withShift,
            forbidsShift: false,
            needsSyntheticCtrl: withCtrl,
            forbidsCtrl: false
        )
    }

    // MARK: - Empty / baseline

    func testNothingHeld_ShiftOff() {
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: []))
        XCTAssertFalse(BBCModifierState.desiredCtrl(pressedKeys: []))
    }

    // MARK: - Direct SHIFT alone (Planetoid thrust)

    func testPhysicalShiftAlone_BBCShiftOn() {
        XCTAssertTrue(BBCModifierState.desiredShift(pressedKeys: [directShift()]))
    }

    func testPhysicalShiftReleased_BBCShiftOff() {
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: []))
    }

    // MARK: - Synthesis: BBC needs SHIFT for a host-unshifted character

    func testSyntheticShift_ApostropheAlone() {
        // `'` on a US keyboard is unshifted on the host but Shift+7 on the BBC.
        // No physical SHIFT held -> synthesise BBC SHIFT.
        let q = characterKey("7", bbcShiftRequired: true, physicalShiftHeld: false)
        XCTAssertTrue(q.needsSyntheticShift)
        XCTAssertFalse(q.forbidsShift)
        XCTAssertTrue(BBCModifierState.desiredShift(pressedKeys: [q]))
    }

    func testSyntheticShift_ReleasedRestoresOff() {
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: []))
    }

    // MARK: - #18: SHIFT + ; should produce ':'

    func testIssue18_ShiftHeldAndColonChar_BBCShiftReleased() {
        let colon = characterKey(":", bbcShiftRequired: false, physicalShiftHeld: true)
        XCTAssertTrue(colon.forbidsShift)
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: [directShift(), colon]))
    }

    func testIssue18_ColonReleased_BBCShiftRestored() {
        XCTAssertTrue(BBCModifierState.desiredShift(pressedKeys: [directShift()]))
    }

    func testIssue18_UnderscoreVariant() {
        let under = characterKey("_", bbcShiftRequired: false, physicalShiftHeld: true)
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: [directShift(), under]))
    }

    // MARK: - Typing shifted characters with physical SHIFT held

    func testTypingShiftedChar_RidesPhysical() {
        // Typing 'A' (uppercase) with physical SHIFT held. Cache resolves
        // 'A' -> BBC 'A' key with needsShift=true. Physical is providing
        // SHIFT, so no synthesis is needed and no forbid is set; BBC SHIFT
        // simply rides physical.
        let a = characterKey("A", bbcShiftRequired: true, physicalShiftHeld: true)
        XCTAssertFalse(a.needsSyntheticShift)
        XCTAssertFalse(a.forbidsShift)
        XCTAssertTrue(BBCModifierState.desiredShift(pressedKeys: [directShift(), a]))
    }

    // MARK: - #10: SHIFT + char held; release SHIFT first

    func testIssue10_ShiftReleasedWhileAStillHeld_BBCShiftReleases() {
        // The key was pressed while physical SHIFT was held, so its
        // synthesis flag is false. After physical SHIFT goes away, the held
        // key contributes neither synthesis nor forbid -> BBC SHIFT off.
        let a = characterKey("A", bbcShiftRequired: true, physicalShiftHeld: true)
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: [a]))
    }

    func testIssue10_BothReleased_BBCShiftOff() {
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: []))
    }

    // MARK: - Mixed combinations

    func testForbidWinsOverNeedsAndPhysical() {
        // Pathological: held forbid + held synthesis. Forbid wins so the
        // user gets at least one character right rather than two wrong ones.
        let colon = characterKey(":", bbcShiftRequired: false, physicalShiftHeld: true)
        let apos = characterKey("7", bbcShiftRequired: true, physicalShiftHeld: false)
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: [directShift(), colon, apos]))
    }

    func testColonAlone_NoShift() {
        let semi = characterKey(";", bbcShiftRequired: false, physicalShiftHeld: false)
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: [semi]))
    }

    // MARK: - Direct-mapped non-modifier with explicit shift

    func testDirectKeyWithExplicitShift_OverridesPhysical() {
        XCTAssertTrue(BBCModifierState.desiredShift(pressedKeys: [directKey("f1", withShift: true)]))
    }

    func testDirectKeyWithoutShift_NoSynthesis() {
        XCTAssertFalse(BBCModifierState.desiredShift(pressedKeys: [directKey("f1", withShift: false)]))
    }

    // MARK: - CTRL parity

    func testCtrlAlone_BBCCtrlOn() {
        XCTAssertTrue(BBCModifierState.desiredCtrl(pressedKeys: [directCtrl()]))
    }

    func testCtrlReleasedWhileCharHeld_BBCCtrlOff() {
        let a = characterKey("A", bbcShiftRequired: false, physicalShiftHeld: false,
                             bbcCtrlRequired: true, physicalCtrlHeld: true)
        XCTAssertFalse(BBCModifierState.desiredCtrl(pressedKeys: [a]))
    }

    func testCtrlSynthesisWhenHostHasNoModifier() {
        // Hypothetical: cache resolves a character to a BBC key needing CTRL
        // and the host had no CTRL down. Synthesise.
        let a = characterKey("A", bbcShiftRequired: false, physicalShiftHeld: false,
                             bbcCtrlRequired: true, physicalCtrlHeld: false)
        XCTAssertTrue(a.needsSyntheticCtrl)
        XCTAssertTrue(BBCModifierState.desiredCtrl(pressedKeys: [a]))
    }

    // MARK: - A character never suppresses CTRL

    func testCharacterKeyDoesNotForbidHeldCtrl() {
        // CTRL is not a character-generating modifier: the host has already
        // folded it into a control code and the BBC folds it again itself.
        // Suppressing it would hide the CTRL key from guest software that
        // scans the keyboard matrix.
        let flags = BBCModifierState.ctrlFlagsForCharacter(
            bbcCtrlRequired: false,
            physicalCtrlHeld: true
        )

        XCTAssertFalse(flags.forbids)
        XCTAssertFalse(flags.needsSynthetic)
    }

    func testHeldCtrlSurvivesACharacterKeyPress() {
        // The BBC must see CTRL down while a character key resolved through
        // the character map is held.
        let m = characterKey("m", bbcShiftRequired: false, physicalShiftHeld: false,
                             bbcCtrlRequired: false, physicalCtrlHeld: true)

        XCTAssertTrue(BBCModifierState.desiredCtrl(pressedKeys: [directCtrl(), m]))
    }
}
