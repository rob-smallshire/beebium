// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

/// KeyboardMTKView.modifierKeyIsDown decides, from a flagsChanged event's raw
/// modifier bits, whether a specific physical modifier key is down -- using the
/// device-specific left/right bits so one side releasing while the other is held
/// is seen as an up for that side.
final class ModifierKeyStateTests: XCTestCase {
    // Device-dependent (left/right) raw bits.
    private let lShift: UInt = 0x0002, rShift: UInt = 0x0004
    private let lCtrl: UInt = 0x0001, rCtrl: UInt = 0x2000
    // Coalesced (device-independent) raw bits.
    private let shiftFlag: UInt = 0x20000, ctrlFlag: UInt = 0x40000
    private let functionFlag: UInt = 0x800000

    private func down(_ keyCode: UInt16, _ raw: UInt) -> Bool {
        KeyboardMTKView.modifierKeyIsDown(keyCode: keyCode, rawFlags: raw)
    }

    func testLeftShiftAloneDown() {
        let raw = lShift | shiftFlag
        XCTAssertTrue(down(MacKeyCode.shift, raw))
        XCTAssertFalse(down(MacKeyCode.rightShift, raw))
    }

    func testBothShiftsDown() {
        let raw = lShift | rShift | shiftFlag
        XCTAssertTrue(down(MacKeyCode.shift, raw))
        XCTAssertTrue(down(MacKeyCode.rightShift, raw))
    }

    func testReleasingLeftShiftWhileRightHeld() {
        // The regression case: left up, right still down. .shift stays set, but
        // the left device bit is gone -> left must read as up, right as down.
        let raw = rShift | shiftFlag
        XCTAssertFalse(down(MacKeyCode.shift, raw), "left Shift should be up")
        XCTAssertTrue(down(MacKeyCode.rightShift, raw), "right Shift still down")
    }

    func testReleasingRightControlWhileLeftHeld() {
        let raw = lCtrl | ctrlFlag  // right up, left still down
        XCTAssertTrue(down(MacKeyCode.control, raw))
        XCTAssertFalse(down(MacKeyCode.rightControl, raw))
    }

    func testFallbackWhenNoDeviceBits() {
        // Synthetic events may carry only the coalesced flag; a single modifier
        // must still register.
        XCTAssertTrue(down(MacKeyCode.shift, shiftFlag))
        XCTAssertTrue(down(MacKeyCode.control, ctrlFlag))
    }

    func testAllReleased() {
        XCTAssertFalse(down(MacKeyCode.shift, 0))
        XCTAssertFalse(down(MacKeyCode.control, 0))
    }

    func testFunction() {
        XCTAssertTrue(down(MacKeyCode.function, functionFlag))
        XCTAssertFalse(down(MacKeyCode.function, 0))
    }

    func testNonModifierKeyIsNeverDown() {
        XCTAssertFalse(down(0x00 /* 'a' */, lShift | shiftFlag))
    }
}
