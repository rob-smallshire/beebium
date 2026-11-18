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

/// Tests that the cassette browse menu is built as a native menu with the right
/// items and enabled rules, and that the Input Device submenu enumerates the
/// host only when it opens -- never while the menu (or the deck) is built.
@MainActor
final class CassetteBrowseMenuTests: XCTestCase {

    private func noopActions(
        choose: @escaping (LiveAudioDevice) -> Void = { _ in }
    ) -> CassetteBrowseMenu.Actions {
        CassetteBrowseMenu.Actions(
            browse: {}, insertRecent: { _ in }, clearRecent: {},
            newBlank: { _ in }, chooseDevice: choose)
    }

    private func coordinator(
        canBrowse: Bool, liveInputAvailable: Bool,
        recentTapes: [RecentTape] = [], blankTapes: [Beebium_BlankTape] = [],
        enumerate: @escaping () -> [LiveAudioDevice] = { [] },
        choose: @escaping (LiveAudioDevice) -> Void = { _ in }
    ) -> CassetteBrowseCoordinator {
        let coordinator = CassetteBrowseCoordinator(
            inputDevices: LiveInputDeviceList(enumerate: enumerate),
            actions: noopActions(choose: choose))
        coordinator.canBrowse = canBrowse
        coordinator.liveInputAvailable = liveInputAvailable
        coordinator.recentTapes = recentTapes
        coordinator.blankTapes = blankTapes
        return coordinator
    }

    private func blank(_ label: String) -> Beebium_BlankTape {
        Beebium_BlankTape.with { $0.label = label }
    }

    // MARK: - Item order per local/remote

    func testLocalServerListsBrowseRecentNewBlankThenInputDevice() {
        let titles = coordinator(canBrowse: true, liveInputAvailable: true)
            .makeMenu().items.map(\.title)
        XCTAssertEqual(titles, ["Browse...", "Recent", "New Blank Tape", "Input Device"])
    }

    func testRemoteServerListsOnlyInputDevice() {
        let titles = coordinator(canBrowse: false, liveInputAvailable: true)
            .makeMenu().items.map(\.title)
        XCTAssertEqual(titles, ["Input Device"], "a remote server offers only the host-side input")
    }

    func testWithoutTheUsageStringThereIsNoInputDevice() {
        let titles = coordinator(canBrowse: true, liveInputAvailable: false)
            .makeMenu().items.map(\.title)
        XCTAssertEqual(titles, ["Browse...", "Recent", "New Blank Tape"])
    }

    // MARK: - Enabled rules

    func testRecentIsDisabledWhenEmptyAndEnabledWithTapes() {
        let empty = coordinator(canBrowse: true, liveInputAvailable: false).makeMenu()
        XCTAssertFalse(empty.items.first { $0.title == "Recent" }?.isEnabled ?? true)

        let withTapes = coordinator(
            canBrowse: true, liveInputAvailable: false,
            recentTapes: [RecentTape(filepath: "/tapes/a.uef", lastUsed: Date())]).makeMenu()
        XCTAssertTrue(withTapes.items.first { $0.title == "Recent" }?.isEnabled ?? false)
    }

    func testNewBlankTapeIsDisabledWithoutTemplatesAndEnabledWithThem() {
        let none = coordinator(canBrowse: true, liveInputAvailable: false).makeMenu()
        XCTAssertFalse(none.items.first { $0.title == "New Blank Tape" }?.isEnabled ?? true)

        let withTemplates = coordinator(
            canBrowse: true, liveInputAvailable: false,
            blankTapes: [blank("C15"), blank("C90")]).makeMenu()
        let item = withTemplates.items.first { $0.title == "New Blank Tape" }
        XCTAssertTrue(item?.isEnabled ?? false)
        XCTAssertEqual(item?.submenu?.items.map(\.title), ["C15", "C90"])
    }

    // MARK: - Input Device submenu enumerates only on open

    func testBuildingTheMenuEnumeratesNothing() {
        var enumerations = 0
        let coordinator = coordinator(canBrowse: true, liveInputAvailable: true,
                                      enumerate: { enumerations += 1; return [] })
        _ = coordinator.makeMenu()
        XCTAssertEqual(enumerations, 0, "building the menu -- and so rendering the deck -- must "
                       + "touch no audio API")
    }

    func testInputDeviceSubmenuEnumeratesOncePerOpen() {
        var enumerations = 0
        let devices = [LiveAudioDevice(id: 1, name: "Built-in Microphone"),
                       LiveAudioDevice(id: 2, name: "BlackHole 2ch")]
        let coordinator = coordinator(canBrowse: true, liveInputAvailable: true,
                                      enumerate: { enumerations += 1; return devices })
        let menu = coordinator.makeMenu()
        guard let submenu = menu.items.first(where: { $0.title == "Input Device" })?.submenu else {
            return XCTFail("the Input Device item must carry a submenu")
        }
        XCTAssertEqual(enumerations, 0, "the submenu is empty until it opens")
        XCTAssertTrue(coordinator === (submenu.delegate as? CassetteBrowseCoordinator),
                      "the submenu is populated by the coordinator on open")

        coordinator.menuNeedsUpdate(submenu)   // the submenu is about to open
        XCTAssertEqual(enumerations, 1)
        XCTAssertEqual(submenu.items.map(\.title), ["Built-in Microphone", "BlackHole 2ch"])

        coordinator.menuNeedsUpdate(submenu)   // opened again
        XCTAssertEqual(enumerations, 2, "each open enumerates exactly once")
    }

    func testInputDeviceSubmenuShowsADisabledItemWhenNoDevices() {
        let coordinator = coordinator(canBrowse: false, liveInputAvailable: true, enumerate: { [] })
        let submenu = coordinator.makeMenu().items.first { $0.title == "Input Device" }?.submenu
        coordinator.menuNeedsUpdate(submenu!)
        XCTAssertEqual(submenu?.items.map(\.title), ["No audio input devices"])
        XCTAssertFalse(submenu?.items.first?.isEnabled ?? true)
    }

    func testChoosingADeviceInvokesTheChooseAction() {
        var chosen: LiveAudioDevice?
        let devices = [LiveAudioDevice(id: 7, name: "USB Audio")]
        let coordinator = coordinator(canBrowse: false, liveInputAvailable: true,
                                      enumerate: { devices }, choose: { chosen = $0 })
        let submenu = coordinator.makeMenu().items.first { $0.title == "Input Device" }?.submenu
        coordinator.menuNeedsUpdate(submenu!)
        (submenu?.items.first as? BlockMenuItem)?.invoke()
        XCTAssertEqual(chosen, devices.first)
    }
}
