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

import Foundation

/// The live-input device picker's list, enumerated lazily so the empty cassette
/// deck never touches an audio API at render.
///
/// Reading the host's input devices is deferred until the Input Device submenu
/// is actually opened (its `onAppear` calls `refresh`), not done while the deck
/// view is built. The deck is rendered as soon as a machine window opens, so
/// enumerating there asked the system for the microphone at launch -- before the
/// user chose any input, which the system presents as an alarming prompt. The
/// list starts empty and touches no audio API until `refresh`; populating on the
/// submenu's appearance also keeps it fresh (devices as of when the picker opens).
///
/// The enumerator is injected so a test can record whether, and when, the host
/// enumeration is reached -- the construction of this list, and so the deck's
/// render, must reach it never.
@MainActor
final class LiveInputDeviceList: ObservableObject {
    /// The host input devices as of the last `refresh`, empty until then.
    @Published private(set) var devices: [LiveAudioDevice] = []

    private let enumerate: () -> [LiveAudioDevice]

    init(enumerate: @escaping () -> [LiveAudioDevice] = { LiveAudioDevices.inputs() }) {
        self.enumerate = enumerate
    }

    /// Re-read the host's input devices. Called when the Input Device submenu is
    /// about to be shown, never during view construction, so no audio API is
    /// touched -- and the microphone never asked for -- until the user opens the
    /// picker.
    func refresh() {
        devices = enumerate()
    }
}
