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

/// The seam both the SetMachineName response and the IDENTITY_CHANGED status
/// event use to update the published identity (#153): a rename's new rendered
/// name must reach the window title through `updateIdentity`.
@MainActor
final class SystemClientIdentityTests: XCTestCase {

    func testUpdateIdentityPublishesRenderedNameAndTemplate() {
        let client = SystemClient()

        var identity = Beebium_MachineIdentity()
        identity.uuid = "abc-123"
        identity.name = "Station 83 (AUN, Model B) #1"
        identity.nameTemplate = "Station {econet-station} (AUN, Model B) {machine-ordinal}"
        identity.modelType = "ModelB"
        identity.modelName = "BBC Model B"

        client.updateIdentity(identity)

        // machineName drives the window title; machineNameTemplate seeds the
        // rename popover's field.
        XCTAssertEqual(client.machineName, "Station 83 (AUN, Model B) #1")
        XCTAssertEqual(client.machineNameTemplate,
                       "Station {econet-station} (AUN, Model B) {machine-ordinal}")
        XCTAssertEqual(client.machineUUID, "abc-123")
        XCTAssertEqual(client.machineDisplayName, "BBC Model B")
    }

    func testUpdateIdentityReplacesTheRenderedNameOnRename() {
        // A rename sends a new rendered name on the same machine; the published
        // name must follow rather than stay on the first value.
        let client = SystemClient()

        var first = Beebium_MachineIdentity()
        first.uuid = "u"
        first.name = "Station 80 (AUN, Model B) #1"
        client.updateIdentity(first)
        XCTAssertEqual(client.machineName, "Station 80 (AUN, Model B) #1")

        var renamed = first
        renamed.name = "Girton"
        renamed.nameTemplate = "Girton"
        client.updateIdentity(renamed)

        XCTAssertEqual(client.machineName, "Girton")
        XCTAssertEqual(client.machineNameTemplate, "Girton")
    }
}
