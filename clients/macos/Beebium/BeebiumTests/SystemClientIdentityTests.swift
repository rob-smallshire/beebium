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
/// event use to update the published identity: the plain name reaches the
/// window title, the rendered description reaches the subtitle, and the
/// description template seeds a description editor -- all through
/// `updateIdentity`.
@MainActor
final class SystemClientIdentityTests: XCTestCase {

    func testUpdateIdentityPublishesNameDescriptionAndTemplate() {
        let client = SystemClient()

        var identity = Beebium_MachineIdentity()
        identity.uuid = "abc-123"
        identity.name = "Model B (Disc) #1"
        identity.description_p = "Station 83 (AUN, Model B)"
        identity.descriptionTemplate = "Station {econet-station} (AUN, Model B)"
        identity.modelType = "ModelB"
        identity.modelName = "BBC Model B"

        client.updateIdentity(identity)

        // machineName drives the window title (plain); machineDescription drives
        // the subtitle (rendered); machineDescriptionTemplate seeds the editor.
        XCTAssertEqual(client.machineName, "Model B (Disc) #1")
        XCTAssertEqual(client.machineDescription, "Station 83 (AUN, Model B)")
        XCTAssertEqual(client.machineDescriptionTemplate,
                       "Station {econet-station} (AUN, Model B)")
        XCTAssertEqual(client.machineUUID, "abc-123")
        XCTAssertEqual(client.machineDisplayName, "BBC Model B")
    }

    func testUpdateIdentityReplacesNameAndDescriptionOnChange() {
        // A rename or a re-render sends new values on the same machine; the
        // published fields must follow rather than stay on the first value.
        let client = SystemClient()

        var first = Beebium_MachineIdentity()
        first.uuid = "u"
        first.name = "Model B (Disc) #1"
        first.description_p = "Station 80 (AUN, Model B)"
        client.updateIdentity(first)
        XCTAssertEqual(client.machineName, "Model B (Disc) #1")
        XCTAssertEqual(client.machineDescription, "Station 80 (AUN, Model B)")

        var renamed = first
        renamed.name = "Girton"
        renamed.description_p = ""
        renamed.descriptionTemplate = ""
        client.updateIdentity(renamed)

        XCTAssertEqual(client.machineName, "Girton")
        XCTAssertEqual(client.machineDescription, "")
        XCTAssertEqual(client.machineDescriptionTemplate, "")
    }
}
