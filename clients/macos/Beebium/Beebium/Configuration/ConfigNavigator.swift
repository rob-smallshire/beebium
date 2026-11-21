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

import SwiftUI

/// Drives which Configurator section is shown and carries a one-shot cue to the
/// section it switches to, so a control in one section can send the user to the
/// right place in another. A ROM cartridge spans two sections -- it is fitted
/// under Peripherals but its ROM images are loaded in Memory -- so each section
/// offers an affordance to the other, and this is how they are wired (spec 5.9).
///
/// Owned by ConfigurationEditor and shared through the environment. The cues are
/// read once on arrival and then cleared, so returning to a section later does
/// not re-trigger a scroll or a highlight.
@MainActor
final class ConfigNavigator: ObservableObject {
    /// The section currently shown. The sidebar binds to this, and a cross-section
    /// affordance sets it.
    @Published var section: ConfigurationEditor.ConfigSection

    /// A Peripherals attachment point to reveal on arrival (e.g. the cartridge
    /// slot a Memory row sends the user to). Cleared once consumed.
    @Published var revealPointId: String?

    /// A Memory sideways-socket label to reveal and highlight on arrival (e.g. a
    /// bank a cartridge's Peripherals view sends the user to). Cleared once
    /// consumed.
    @Published var revealSocketLabel: String?

    init(section: ConfigurationEditor.ConfigSection = .storage) {
        self.section = section
    }

    /// Switch to Peripherals and reveal `point` (the attachment point a cartridge
    /// is, or would be, fitted to).
    func goToPeripherals(point: String) {
        revealSocketLabel = nil
        revealPointId = point
        section = .peripherals
    }

    /// Switch to Memory and reveal the socket row labelled `label` (a bank a
    /// cartridge provides).
    func goToMemory(socketLabel label: String) {
        revealPointId = nil
        revealSocketLabel = label
        section = .memory
    }
}
