// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

@MainActor
final class PasteCoordinatorTests: XCTestCase {

    // MARK: - Doubles

    final class FakeTypist: PasteTypist {
        var typed: [String] = []
        var refusal: String?
        var didWait = false
        var cleared = 0
        /// Called while the paste is draining, so a test can act mid-paste.
        var onWait: (() -> Void)?

        func typeQuickly(_ text: String) async -> String? {
            if let refusal { return refusal }
            typed.append(text)
            return nil
        }

        func waitUntilTypingComplete() async {
            didWait = true
            onWait?()
        }

        @discardableResult
        func clearTyping() async -> Int {
            cleared += 1
            return 7
        }
    }

    final class FakeSpeedControl: PasteSpeedControl {
        private(set) var isUnlimited: Bool
        var changes: [Bool] = []

        init(isUnlimited: Bool = false) { self.isUnlimited = isUnlimited }

        func setUnlimited(_ unlimited: Bool) {
            isUnlimited = unlimited
            changes.append(unlimited)
        }
    }

    final class FakeAudioMute: PasteAudioMute {
        var isMuted: Bool
        init(isMuted: Bool = false) { self.isMuted = isMuted }
    }

    /// The fakes are held for the test's lifetime; the coordinator's references
    /// to them are weak.
    private var retained: [AnyObject] = []

    private func makeCoordinator(
        hasTypist: Bool = true,
        speedUnlimited: Bool = false,
        audioMuted: Bool = false,
        realTimeTransportActive: Bool = false
    ) -> (PasteCoordinator, FakeTypist?, FakeSpeedControl?, FakeAudioMute?) {
        let typist = hasTypist ? FakeTypist() : nil
        let speed = FakeSpeedControl(isUnlimited: speedUnlimited)
        let audio = FakeAudioMute(isMuted: audioMuted)
        if let typist { retained.append(typist) }
        retained.append(speed)
        retained.append(audio)

        let coordinator = PasteCoordinator()
        coordinator.typist = typist
        coordinator.speedControl = speed
        coordinator.audioMute = audio
        coordinator.realTimeTransportActive = { realTimeTransportActive }
        return (coordinator, typist, speed, audio)
    }

    // MARK: - Ordinary paste

    func testPasteTypesTheText() async {
        let (coordinator, typist, speed, audio) = makeCoordinator()
        let failure = await coordinator.paste("HELLO")
        XCTAssertNil(failure)
        XCTAssertEqual(typist?.typed, ["HELLO"])
        // A machine-speed paste must not touch speed or audio at all.
        XCTAssertEqual(speed?.changes, [])
        XCTAssertEqual(audio?.isMuted, false)
    }

    func testEmptyPasteDoesNothing() async {
        let (coordinator, typist, _, _) = makeCoordinator()
        let failure = await coordinator.paste("")
        XCTAssertNil(failure)
        XCTAssertEqual(typist?.typed, [])
    }

    func testPasteWithoutATypistReportsIt() async {
        let (coordinator, _, _, _) = makeCoordinator(hasTypist: false)
        let failure = await coordinator.paste("HELLO")
        XCTAssertNotNil(failure)
    }

    func testRefusalIsPassedThrough() async {
        let (coordinator, typist, _, _) = makeCoordinator()
        typist?.refusal = "cannot type U+2603"
        let failure = await coordinator.paste("A\u{2603}B")
        XCTAssertEqual(failure, "cannot type U+2603")
    }

    // MARK: - Waiting for the server rather than guessing

    func testMachineSpeedPasteWaitsForTheServerToSayItFinished() async {
        // The RPC returns as soon as the text is enqueued, but the machine
        // types on. Without waiting, isPasting would be false a moment later
        // and Escape would have nothing to cancel.
        let (coordinator, typist, _, _) = makeCoordinator()
        await coordinator.paste("LONG TEXT")
        XCTAssertEqual(typist?.didWait, true)
    }

    func testIsPastingIsTrueWhileWaitingAndFalseAfter() async {
        let (coordinator, typist, _, _) = makeCoordinator()
        var pastingWhileDraining: Bool?
        typist?.onWait = { [weak coordinator] in
            pastingWhileDraining = coordinator?.isPasting
        }

        XCTAssertFalse(coordinator.isPasting)
        await coordinator.paste("TEXT")

        XCTAssertEqual(pastingWhileDraining, true)
        XCTAssertFalse(coordinator.isPasting, "cleared afterwards")
    }

    // MARK: - Full-speed paste

    func testFullSpeedRaisesSpeedMutesAudioAndRestoresBoth() async {
        let (coordinator, typist, speed, audio) = makeCoordinator()

        let failure = await coordinator.paste("LONG TEXT", pace: .fullSpeed)

        XCTAssertNil(failure)
        XCTAssertEqual(speed?.changes, [true, false], "speed raised then restored")
        XCTAssertEqual(speed?.isUnlimited, false)
        XCTAssertEqual(audio?.isMuted, false, "audio unmuted again")
        XCTAssertEqual(typist?.didWait, true, "waited for the server to say it had finished")
    }

    func testFullSpeedRestoresAfterARefusal() async {
        let (coordinator, typist, speed, audio) = makeCoordinator()
        typist?.refusal = "nope"

        let failure = await coordinator.paste("TEXT", pace: .fullSpeed)

        XCTAssertEqual(failure, "nope")
        XCTAssertEqual(speed?.changes, [true, false], "restored despite the failure")
        XCTAssertEqual(audio?.isMuted, false)
    }

    func testSpeedIsRestoredHoweverTheWaitEnds() async {
        // The wait ends when the server says the queue is empty, but also when
        // the stream breaks -- a cancelled paste, a lost connection, a stopped
        // server. The machine must never be left at unlimited speed and muted.
        let (coordinator, _, speed, audio) = makeCoordinator()

        await coordinator.paste("TEXT", pace: .fullSpeed)

        XCTAssertEqual(speed?.changes, [true, false])
        XCTAssertEqual(audio?.isMuted, false)
    }

    func testAlreadyUnlimitedSpeedIsLeftAlone() async {
        // Nothing was changed, so nothing should be restored -- otherwise the
        // paste would drop the machine out of a speed the user chose.
        let (coordinator, _, speed, _) = makeCoordinator(speedUnlimited: true)

        await coordinator.paste("TEXT", pace: .fullSpeed)

        XCTAssertEqual(speed?.changes, [], "did not touch a speed it did not set")
        XCTAssertEqual(speed?.isUnlimited, true)
    }

    func testAlreadyMutedAudioStaysMuted() async {
        let (coordinator, _, _, audio) = makeCoordinator(audioMuted: true)

        await coordinator.paste("TEXT", pace: .fullSpeed)

        XCTAssertEqual(audio?.isMuted, true, "did not unmute what the user muted")
    }

    func testSpeedChangedByTheUserMidPasteIsNotStomped() async {
        // The user reaching for the speed control during a paste means it.
        let (coordinator, typist, speed, _) = makeCoordinator()
        typist?.onWait = { [weak speed] in speed?.setUnlimited(false) }

        await coordinator.paste("TEXT", pace: .fullSpeed)

        // true (ours), false (the user's). The restore must not add another.
        XCTAssertEqual(speed?.changes, [true, false])
        XCTAssertEqual(speed?.isUnlimited, false)
    }

    // MARK: - Availability of the full-speed command

    func testFullSpeedUnavailableWithoutAConnection() async {
        let (coordinator, _, _, _) = makeCoordinator(hasTypist: false)
        XCTAssertFalse(coordinator.canPasteAtFullSpeed)
        XCTAssertNotNil(coordinator.fullSpeedUnavailableReason)
    }

    func testFullSpeedUnavailableWhileARealTimeTransportIsActive() async {
        let (coordinator, _, _, _) = makeCoordinator(realTimeTransportActive: true)
        XCTAssertFalse(coordinator.canPasteAtFullSpeed)
        XCTAssertTrue(
            coordinator.fullSpeedUnavailableReason?.contains("real-time") ?? false,
            "the reason should say what is blocking it, for the tooltip")
    }

    func testFullSpeedPasteIsRefusedWhileGatedRatherThanSeveringTheTransport() async {
        let (coordinator, _, speed, _) = makeCoordinator(realTimeTransportActive: true)

        let failure = await coordinator.paste("TEXT", pace: .fullSpeed)

        XCTAssertNotNil(failure)
        XCTAssertEqual(speed?.changes, [], "never changed speed, so never severed the link")
    }

    func testFullSpeedAvailableWhenNothingBlocksIt() async {
        let (coordinator, _, _, _) = makeCoordinator()
        XCTAssertTrue(coordinator.canPasteAtFullSpeed)
        XCTAssertNil(coordinator.fullSpeedUnavailableReason)
    }

    // MARK: - Cancellation

    func testCancelClearsTheQueue() async {
        let (coordinator, typist, _, _) = makeCoordinator()
        let cleared = await coordinator.cancel()
        XCTAssertEqual(cleared, 7)
        XCTAssertEqual(typist?.cleared, 1)
    }
}
