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

/// Tests that the live-input device list enumerates host devices only on an
/// explicit refresh (the Input Device submenu's appearance), never at
/// construction -- so building the deck's state touches no audio API and the
/// app never asks for the microphone at launch (defect: the prompt fired as
/// soon as the app opened, because the deck's render enumerated devices).
@MainActor
final class LiveInputDeviceListTests: XCTestCase {

    func testConstructionNeverEnumerates() {
        var calls = 0
        let list = LiveInputDeviceList(enumerate: { calls += 1; return [] })
        XCTAssertEqual(calls, 0, "no host enumeration at construction -- the deck's render must not "
                       + "touch an audio API")
        XCTAssertTrue(list.devices.isEmpty, "the list is empty until it is refreshed")
    }

    func testRefreshEnumeratesOnceAndPublishesTheDevices() {
        var calls = 0
        let devices = [LiveAudioDevice(id: 1, name: "Built-in Microphone"),
                       LiveAudioDevice(id: 2, name: "BlackHole 2ch")]
        let list = LiveInputDeviceList(enumerate: { calls += 1; return devices })

        list.refresh()

        XCTAssertEqual(calls, 1, "enumeration happens only on the submenu-open refresh")
        XCTAssertEqual(list.devices, devices)
    }

    func testRefreshReflectsTheDevicesAsOfWhenItOpens() {
        var current = [LiveAudioDevice(id: 1, name: "A")]
        let list = LiveInputDeviceList(enumerate: { current })

        list.refresh()
        XCTAssertEqual(list.devices.map(\.name), ["A"])

        // A device appears (or the user changes the setup) before the picker is
        // opened again; the next open shows the current list, not a stale one.
        current = [LiveAudioDevice(id: 1, name: "A"), LiveAudioDevice(id: 2, name: "B")]
        list.refresh()
        XCTAssertEqual(list.devices.map(\.name), ["A", "B"])
    }
}
