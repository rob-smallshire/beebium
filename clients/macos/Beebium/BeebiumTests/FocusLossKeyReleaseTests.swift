// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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
import AppKit
@testable import Beebium

/// Rule R4 of docs/frontend-modifier-keys.md: when the window stops being key,
/// everything held on the emulated keyboard is released.
///
/// macOS delivers the matching key-up / flagsChanged to whoever gains focus, so
/// anything still down at that moment stays down on the BBC -- the stuck SHIFT
/// that "corrupts everything they type next and looks like the emulator is
/// broken".
///
/// Two things are tested, and the second is the one that is easy to lose. The
/// release must happen, and it must happen *synchronously* with the focus-loss
/// notification. Deferring it -- the reflex `Task { @MainActor in ... }` the
/// rest of the client uses for genuinely asynchronous work -- puts it behind
/// the events that accompany focus loss, which is the race R4 exists to win.
///
/// The notification wiring itself needs a window server, so these drive the
/// pieces underneath it: `KeyboardMTKView.releaseHeldKeysOnFocusLoss()`, which
/// the observer calls, and `KeyboardClient`'s held-key bookkeeping.
@MainActor
final class FocusLossKeyReleaseTests: XCTestCase {

    // MARK: - Fixture

    /// A mapping with direct entries for the host keys the tests press: 'A'
    /// (keyCode 0), 'Z' (keyCode 6), and left Shift.
    private func makeMapping() -> KeyboardMapping {
        let entries: [[String: Any]] = [
            ["macOS": ["keyCode": 0], "bbc": ["keyName": "A"]],
            ["macOS": ["keyCode": 6], "bbc": ["keyName": "Z"]],
            ["macOS": ["keyCode": Int(MacKeyCode.shift)], "bbc": ["keyName": "Shift"]]
        ]
        return KeyboardMapping(json: [
            "version": 1,
            "id": UUID().uuidString,
            "name": "FocusLossTest",
            "characterMapping": false,
            "synchronizeCapsLock": false,
            "keyMappings": entries
        ])
    }

    /// A keyboard client wired to the fixture mapping but to no gRPC channel.
    /// The held-key bookkeeping is synchronous and local; only the wire sends
    /// need a channel, and those quietly do nothing without one.
    ///
    /// The manager is held by the test case, not returned: `KeyboardClient`
    /// keeps only a weak reference to it, and a manager that went out of scope
    /// would leave every keyDown reporting "mapping not ready".
    private var manager: KeyboardMappingManager!
    private var client: KeyboardClient!

    override func setUp() async throws {
        try await super.setUp()
        await MainActor.run {
            manager = KeyboardMappingManager()
            manager.bbcKeyCache.load(from: makeKeyCacheResponse())
            manager.activeMapping = makeMapping()
            client = KeyboardClient()
            client.mappingManager = manager
        }
    }

    override func tearDown() async throws {
        await MainActor.run {
            client = nil
            manager = nil
        }
        try await super.tearDown()
    }

    /// The letters the tests press, in both faces, plus the modifier names the
    /// resolver expects to find.
    private func makeKeyCacheResponse() -> Beebium_AllKeyMappingsResponse {
        var response = Beebium_AllKeyMappingsResponse()
        func add(character: String, name: String, ikNumber: UInt32, needsShift: Bool) {
            var entry = Beebium_KeyMappingEntry()
            entry.found = true
            entry.character = character
            entry.name = name
            entry.ikNumber = ikNumber
            entry.needsShift = needsShift
            response.mappings.append(entry)
        }
        add(character: "a", name: "a", ikNumber: 0x41, needsShift: false)
        add(character: "A", name: "A", ikNumber: 0x41, needsShift: true)
        add(character: "z", name: "z", ikNumber: 0x61, needsShift: false)
        add(character: "Z", name: "Z", ikNumber: 0x61, needsShift: true)
        add(character: "", name: "Shift", ikNumber: 0x00, needsShift: false)
        add(character: "", name: "Ctrl", ikNumber: 0x01, needsShift: false)
        return response
    }

    private func press(_ keyCode: UInt16) {
        client.keyDown(input: KeyInput(keyCode: keyCode, source: .physicalKeyboard))
    }

    private func release(_ keyCode: UInt16) {
        client.keyUp(input: KeyInput(keyCode: keyCode, source: .physicalKeyboard))
    }

    // MARK: - KeyboardClient.releaseAllKeys

    func testHeldKeysAreTrackedUntilReleased() {
        XCTAssertFalse(client.hasKeysHeld, "nothing pressed yet")

        press(0)
        XCTAssertTrue(client.hasKeysHeld)

        release(0)
        XCTAssertFalse(client.hasKeysHeld, "the key's own key-up clears it")
    }

    func testReleaseAllKeysClearsEverythingHeld() {
        press(MacKeyCode.shift)
        press(0)
        press(6)
        XCTAssertTrue(client.hasKeysHeld)

        client.releaseAllKeys()

        XCTAssertFalse(client.hasKeysHeld,
                       "R4: nothing may remain held once focus is lost")
    }

    func testReleaseAllKeysTakesEffectBeforeTheNextStatement() {
        // The timing half of R4, stated as bluntly as it can be: by the time
        // the call returns, the keyboard is clear. A deferred release -- the
        // `Task { @MainActor in ... }` shape used elsewhere in the client --
        // would still be sitting in the main queue at this point, and the
        // events that follow focus loss would be decided against stale state.
        press(0)

        client.releaseAllKeys()

        XCTAssertFalse(client.hasKeysHeld,
                       "the release must not be deferred to a later run-loop turn")
    }

    func testKeyUpArrivingAfterReleaseAllKeysIsIgnored() {
        // The key-up that macOS sends to whoever gains focus does sometimes
        // still reach us (Cmd-Tab back and forth quickly). Having already
        // released, we must not treat it as a second release and desync.
        press(MacKeyCode.shift)
        press(0)
        client.releaseAllKeys()

        release(0)
        release(MacKeyCode.shift)

        XCTAssertFalse(client.hasKeysHeld)
    }

    func testKeysPressedAfterFocusReturnsAreTrackedAgain() {
        press(0)
        client.releaseAllKeys()
        XCTAssertFalse(client.hasKeysHeld)

        // Focus comes back and the user presses afresh; the client is a clean
        // slate rather than a latched-empty one.
        press(6)
        XCTAssertTrue(client.hasKeysHeld)
        release(6)
        XCTAssertFalse(client.hasKeysHeld)
    }

    // MARK: - Touch Bar keys share the held-key set

    func testTouchBarKeyIsHeldUntilReleased() {
        client.touchBarKeyDown(bbcKeyName: "f0", ikNumber: 0x20)
        XCTAssertTrue(client.hasKeysHeld)

        client.touchBarKeyUp(bbcKeyName: "f0")
        XCTAssertFalse(client.hasKeysHeld)
    }

    func testReleaseAllKeysReleasesTouchBarKeys() {
        // The regression: Touch Bar keys used to go straight to the wire
        // without being tracked, so R4 could not see them and a key held when
        // focus moved away stayed down on the BBC.
        client.touchBarKeyDown(bbcKeyName: "f0", ikNumber: 0x20)
        XCTAssertTrue(client.hasKeysHeld)

        client.releaseAllKeys()

        XCTAssertFalse(client.hasKeysHeld,
                       "R4 must cover Touch Bar keys, not just the physical keyboard")
    }

    func testTouchBarKeyUpAfterFocusLossIsIgnored() {
        // The finger comes up after the window has lost focus. The release
        // already happened; this must not be forwarded as a second one.
        client.touchBarKeyDown(bbcKeyName: "f0", ikNumber: 0x20)
        client.releaseAllKeys()

        client.touchBarKeyUp(bbcKeyName: "f0")

        XCTAssertFalse(client.hasKeysHeld)
    }

    func testTouchBarAndHostKeysAreDistinctIdentities() {
        // A Touch Bar key and a host key that happen to reach the same BBC key
        // are separate presses, and releasing one leaves the other held.
        press(0)
        client.touchBarKeyDown(bbcKeyName: "A", ikNumber: 0x41)

        release(0)
        XCTAssertTrue(client.hasKeysHeld, "the Touch Bar key is still down")

        client.touchBarKeyUp(bbcKeyName: "A")
        XCTAssertFalse(client.hasKeysHeld)
    }

    func testTouchBarBreakIsHeldAndReleased() {
        client.touchBarKeyDown(bbcKeyName: "Break", ikNumber: 0)
        XCTAssertTrue(client.hasKeysHeld)

        client.touchBarKeyUp(bbcKeyName: "Break")
        XCTAssertFalse(client.hasKeysHeld)
    }

    // MARK: - What a bulk release puts on the wire

    private func releasePlan(_ pressed: [PressedKeyState],
                             shift: Bool = false,
                             ctrl: Bool = false) -> [KeyReleaseOperation] {
        KeyboardClient.releaseOperations(
            pressed: pressed, shiftWasDown: shift, ctrlWasDown: ctrl)
    }

    private func held(ik: UInt8, isBreak: Bool = false, name: String = "k") -> PressedKeyState {
        PressedKeyState(
            ikNumber: ik,
            isBreak: isBreak,
            fact: PressedKeyFact(
                bbcKeyName: name,
                fromCharacterMapping: false,
                needsSyntheticShift: false,
                forbidsShift: false,
                needsSyntheticCtrl: false,
                forbidsCtrl: false
            )
        )
    }

    func testBulkReleaseSendsAKeyUpPerHeldKey() {
        XCTAssertEqual(releasePlan([held(ik: 0x41)]), [.key(0x41)])
    }

    func testBulkReleaseSendsBreakUpForAHeldBreak() {
        // The regression: BREAK was released with a KeyUp for its (meaningless)
        // ikNumber, which leaves the reset line asserted. It has its own
        // message and must get it.
        XCTAssertEqual(releasePlan([held(ik: 0, isBreak: true, name: "Break")]), [.breakUp])
    }

    func testBulkReleaseLeavesModifierPositionsToTheReconciler() {
        // A held BBC SHIFT contributes no release of its own -- the trailing
        // modifier release below is the one that clears the matrix position.
        let operations = releasePlan([held(ik: 0x00, name: "Shift")], shift: true)
        XCTAssertEqual(operations, [.key(0x00)])
    }

    func testBulkReleaseClearsModifiersLastAndOnlyWhenDown() {
        let operations = releasePlan([held(ik: 0x41)], shift: true, ctrl: true)
        XCTAssertEqual(operations, [.key(0x41), .key(0x00), .key(0x01)])

        XCTAssertEqual(releasePlan([held(ik: 0x41)]), [.key(0x41)],
                       "no modifier release when neither was down")
    }

    func testBulkReleaseOfNothingIsNoOperations() {
        XCTAssertEqual(releasePlan([]), [])
    }

    // MARK: - KeyboardMTKView.releaseHeldKeysOnFocusLoss

    func testFocusLossHandlerReleasesTheKeyboard() {
        let view = KeyboardMTKView(frame: .zero, device: nil)
        view.keyboardClient = client

        press(MacKeyCode.shift)
        press(0)
        XCTAssertTrue(client.hasKeysHeld)

        view.releaseHeldKeysOnFocusLoss()

        XCTAssertFalse(client.hasKeysHeld,
                       "the focus-loss handler must clear the emulated keyboard")
    }

    func testFocusLossHandlerSurvivesNoKeyboardClient() {
        // The view outlives its client on teardown; losing focus then must not
        // be a crash, just a no-op.
        let view = KeyboardMTKView(frame: .zero, device: nil)
        view.releaseHeldKeysOnFocusLoss()
    }
}
