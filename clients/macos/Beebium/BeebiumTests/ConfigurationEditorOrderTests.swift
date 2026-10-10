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

/// The Configurator shows its sections in the same order the emulator window's
/// sidebar does, so a user who learns one layout reads the other the same way.
/// These pin that correspondence so the two orderings cannot silently drift.
final class ConfigurationEditorOrderTests: XCTestCase {

    /// Every Configurator section maps to a distinct sidebar mode.
    func testEachSectionMapsToADistinctSidebarMode() {
        let modes = ConfigurationEditor.ConfigSection.allCases.map { $0.sidebarMode }
        XCTAssertEqual(Set(modes).count, modes.count, "two sections share a sidebar mode")
    }

    /// Sorting the sections the way ConfigurationEditor does yields exactly the
    /// live sidebar's order with the modes the Configurator omits removed.
    func testSectionOrderFollowsSidebarMode() {
        let sections = ConfigurationEditor.ConfigSection.allCases
            .sorted { $0.sidebarMode.rawValue < $1.sidebarMode.rawValue }
        let sectionModes = sections.map { $0.sidebarMode }

        let covered = Set(sectionModes)
        let expected = SidebarMode.allCases.filter { covered.contains($0) }

        XCTAssertEqual(sectionModes, expected)
        // The intended sequence, spelled out so a reordering is caught here too.
        XCTAssertEqual(sections.map { $0.rawValue },
                       ["Storage", "Memory", "Peripherals", "Keyboard", "Processor", "Network"])
    }
}
