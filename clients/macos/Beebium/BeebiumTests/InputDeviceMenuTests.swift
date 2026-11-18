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

import AppKit
import XCTest
@testable import Beebium

/// Tests that the live-input device menu is built fresh on open: it enumerates
/// the host once per open and makes one item per device, choosing it picks that
/// device; an absent device gives a single disabled item; and construction
/// still enumerates nothing (the deck's render must touch no audio API).
@MainActor
final class InputDeviceMenuTests: XCTestCase {

    func testOneEnabledItemPerDeviceAndRefreshRunsOncePerOpen() {
        var enumerations = 0
        let devices = [LiveAudioDevice(id: 1, name: "Built-in Microphone"),
                       LiveAudioDevice(id: 2, name: "BlackHole 2ch")]
        let list = LiveInputDeviceList(enumerate: { enumerations += 1; return devices })

        var chosen: LiveAudioDevice?
        let items = InputDeviceMenu.items(from: list) { chosen = $0 }

        XCTAssertEqual(enumerations, 1, "the host is enumerated once, when the menu opens")
        XCTAssertEqual(items.map(\.title), ["Built-in Microphone", "BlackHole 2ch"])
        XCTAssertTrue(items.allSatisfy { $0.isEnabled }, "device items are selectable")

        (items[1] as? BlockMenuItem)?.invoke()
        XCTAssertEqual(chosen, devices[1], "choosing an item picks that device")
    }

    func testNoDevicesGivesOneDisabledItem() {
        let list = LiveInputDeviceList(enumerate: { [] })
        let items = InputDeviceMenu.items(from: list) { _ in
            XCTFail("the disabled placeholder must not choose a device")
        }
        XCTAssertEqual(items.count, 1)
        XCTAssertEqual(items.first?.title, "No audio input devices")
        XCTAssertFalse(items.first?.isEnabled ?? true, "the placeholder is disabled")
    }

    func testBuildingTheMenuEachOpenReflectsTheCurrentDevices() {
        var current = [LiveAudioDevice(id: 1, name: "A")]
        let list = LiveInputDeviceList(enumerate: { current })

        XCTAssertEqual(InputDeviceMenu.items(from: list) { _ in }.map(\.title), ["A"])
        current = [LiveAudioDevice(id: 1, name: "A"), LiveAudioDevice(id: 2, name: "B")]
        XCTAssertEqual(InputDeviceMenu.items(from: list) { _ in }.map(\.title), ["A", "B"],
                       "a second open shows the devices present then")
    }
}
