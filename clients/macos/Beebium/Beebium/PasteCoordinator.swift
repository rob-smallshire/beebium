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

import Foundation
import SwiftUI

// MARK: - Seams
//
// Narrow protocols over the three things a paste touches, so the sequencing can
// be tested without a server, an audio device, or a live Econet transport.

/// Types text on the emulated keyboard and reports when it has drained.
@MainActor
protocol PasteTypist: AnyObject {
    /// Returns nil on success, or a message describing the refusal.
    func typeQuickly(_ text: String) async -> String?
    /// Returns once the server says nothing is left to type.
    func waitUntilTypingComplete() async
    /// Discards anything still queued, returning the count dropped.
    @discardableResult func clearTyping() async -> Int
}

/// The emulation speed control, as the paste needs to see it.
@MainActor
protocol PasteSpeedControl: AnyObject {
    var isUnlimited: Bool { get }
    func setUnlimited(_ unlimited: Bool)
}

/// The audio mute control, as the paste needs to see it.
@MainActor
protocol PasteAudioMute: AnyObject {
    var isMuted: Bool { get set }
}

extension KeyboardClient: PasteTypist {}
extension SpeedControlModel: PasteSpeedControl {}
extension AudioMixerState: PasteAudioMute {}

// MARK: - Coordinator

/// The machine-side view of the pasteboard: types text on the emulated keyboard,
/// optionally running the machine at full speed until the text has all arrived.
///
/// This is its own object rather than methods on the Metal view because a fast
/// paste is not one call. It mutes audio, raises the emulation speed, types,
/// waits for the queue to drain, and then puts both back -- correctly on every
/// exit path, including failure. Leaving a machine silently stuck at unlimited
/// speed after a failed paste would be a far worse bug than a slow paste, so
/// that sequence is worth testing without a window or a server.
@MainActor
final class PasteCoordinator: ObservableObject {

    /// How the machine should be driven while the text is typed.
    enum Pace {
        /// Type at the machine's current speed. The machine accepts keys only
        /// as fast as the MOS scans them: roughly 80 ms per character at 1x.
        case machineSpeed
        /// Run the machine as fast as the host allows until the text has all
        /// been typed, then restore the previous speed.
        case fullSpeed
    }

    /// True while a paste is draining, so the UI can reflect it.
    @Published private(set) var isPasting = false

    weak var typist: PasteTypist?
    weak var speedControl: PasteSpeedControl?
    weak var audioMute: PasteAudioMute?

    /// Whether a transport that bridges to real time is currently active.
    ///
    /// A closure rather than a reference to EconetClient because the state is
    /// private(set) there, and because this is the only thing the paste needs
    /// to know about networking.
    var realTimeTransportActive: () -> Bool = { false }

    /// Why a full-speed paste is unavailable, or nil when it is available.
    ///
    /// Surfaced as the menu item's tooltip, so a disabled item explains itself
    /// on hover without interrupting anyone who did not ask.
    var fullSpeedUnavailableReason: String? {
        guard typist != nil else { return "Not connected to a machine." }
        guard speedControl != nil else {
            return "Emulation speed cannot be changed for this machine."
        }
        if realTimeTransportActive() {
            // Running faster than 1x severs a transport bridging to real
            // hardware or a real network, and an in-flight filing system
            // operation would fail. Withhold the command rather than break a
            // live connection behind the user's back.
            return "Unavailable while a real-time network transport is active, "
                 + "because running faster than normal speed would disconnect it."
        }
        return nil
    }

    var canPasteAtFullSpeed: Bool { fullSpeedUnavailableReason == nil }

    init() {}

    /// Type text on the emulated keyboard.
    ///
    /// Returns nil on success, or a message describing why it could not be
    /// done. Never leaves the machine's speed or audio altered, on any path.
    @discardableResult
    func paste(_ text: String, pace: Pace = .machineSpeed) async -> String? {
        guard !text.isEmpty else { return nil }
        guard let typist else { return "Not connected to a machine." }
        guard !isPasting else { return "Another paste is still being typed." }

        isPasting = true
        defer { isPasting = false }

        guard pace == .fullSpeed else {
            // Wait for the queue to drain even at machine speed. The RPC
            // returns as soon as the text is enqueued, but the machine types
            // on for as long as it takes -- so isPasting has to mean "text is
            // still arriving", not "the call is in flight", or Escape would
            // have nothing to cancel a second after the paste began.
            if let failure = await typist.typeQuickly(text) { return failure }
            await typist.waitUntilTypingComplete()
            return nil
        }
        if let reason = fullSpeedUnavailableReason { return reason }

        let boost = beginFullSpeed()
        defer { endFullSpeed(boost) }

        if let failure = await typist.typeQuickly(text) { return failure }
        await typist.waitUntilTypingComplete()
        return nil
    }

    /// Discard anything still queued to be typed.
    @discardableResult
    func cancel() async -> Int {
        guard let typist else { return 0 }
        return await typist.clearTyping()
    }

    // MARK: - Speed and audio, saved and restored

    /// What was changed to go full speed, so it can be put back.
    private struct Boost {
        let raisedSpeed: Bool
        let mutedAudio: Bool
    }

    private func beginFullSpeed() -> Boost {
        // Silence rather than fast-forward. Nothing in the audio path is
        // speed-aware: samples are produced far faster than they can be
        // played, and both the server buffer and the client ring drop on
        // overflow rather than block, so the result is loud chopped noise.
        // This is inaudible in the common case of pasting at a prompt, where
        // the machine is emitting silence anyway.
        var mutedAudio = false
        if let audioMute, !audioMute.isMuted {
            audioMute.isMuted = true
            mutedAudio = true
        }

        // Drive the speed model rather than the RPC, so the Unlimited toggle
        // reflects what the machine is actually doing. The model adopts the
        // server's speed only once, at startup, assuming the GUI is the only
        // thing that changes it; going behind its back would desync the
        // control for the rest of the session.
        var raisedSpeed = false
        if let speedControl, !speedControl.isUnlimited {
            speedControl.setUnlimited(true)
            raisedSpeed = true
        }

        return Boost(raisedSpeed: raisedSpeed, mutedAudio: mutedAudio)
    }

    private func endFullSpeed(_ boost: Boost) {
        // Undo only what this paste did, and only if it still holds. A user who
        // reaches for the speed control or the mute button mid-paste means it;
        // finishing must not stomp their choice.
        if boost.raisedSpeed, let speedControl, speedControl.isUnlimited {
            speedControl.setUnlimited(false)
        }
        if boost.mutedAudio, let audioMute, audioMute.isMuted {
            audioMute.isMuted = false
        }
    }

}

// MARK: - Menu plumbing

/// Makes the window's paste coordinator reachable from the Edit menu.
///
/// "Paste at Full Speed" is our own command rather than a standard responder
/// action, so it is not validated by AppKit's responder chain the way Paste is.
/// A focused value gives the menu the coordinator for whichever machine window
/// is frontmost, and nothing at all when a dialog is focused.
struct PasteCoordinatorFocusedValueKey: FocusedValueKey {
    typealias Value = PasteCoordinator
}

extension FocusedValues {
    var pasteCoordinator: PasteCoordinator? {
        get { self[PasteCoordinatorFocusedValueKey.self] }
        set { self[PasteCoordinatorFocusedValueKey.self] = newValue }
    }
}
