import Foundation
import GRPC
import NIO

/// Position in the BBC keyboard matrix
struct BBCKeyPosition: Hashable {
    let row: Int
    let column: Int
}

/// Client for sending keyboard input to beebium-server via gRPC.
///
/// This client handles the logical mapping from characters to BBC keyboard
/// matrix positions, including automatic Shift key handling for uppercase
/// letters and shifted symbols.
@MainActor
final class KeyboardClient: ObservableObject {

    private var channel: GRPCChannel?
    private var client: Beebium_KeyboardServiceClient?

    /// Track which keys are currently pressed (for proper release)
    private var pressedKeys: Set<BBCKeyPosition> = []

    /// Track characters that triggered a shift press (for proper shift release)
    private var shiftedCharacters: Set<Character> = []

    /// Debug logging
    private var keyEventCount: UInt64 = 0

    /// Connect to the keyboard service using an existing channel
    /// - Parameter channel: The gRPC channel (shared with VideoClient)
    func connect(channel: GRPCChannel) {
        self.channel = channel
        self.client = Beebium_KeyboardServiceClient(channel: channel)
        pressedKeys.removeAll()
        shiftedCharacters.removeAll()
        print("[KeyboardClient] Connected to keyboard service")
    }

    /// Disconnect from the server
    func disconnect() {
        // Release all pressed keys
        for position in pressedKeys {
            Task {
                await sendKeyUp(row: position.row, column: position.column)
            }
        }
        pressedKeys.removeAll()
        shiftedCharacters.removeAll()

        client = nil
        channel = nil
    }

    /// Handle a key down event for a character
    /// - Parameter character: The character that was typed
    func keyDown(character: Character) {
        guard let bbcKey = KeyboardMapper.characterToMatrix(character) else {
            // Character not mapped - ignore
            return
        }

        let position = BBCKeyPosition(row: bbcKey.row, column: bbcKey.column)

        // Don't send duplicate key down events
        if pressedKeys.contains(position) {
            return
        }

        keyEventCount += 1
        print("[KeyboardClient] keyDown '\(character)' → row=\(bbcKey.row) col=\(bbcKey.column) shift=\(bbcKey.needsShift)")

        // Track pressed state SYNCHRONOUSLY before async work
        // This prevents race condition where keyUp arrives before Task completes
        pressedKeys.insert(position)
        if bbcKey.needsShift {
            shiftedCharacters.insert(character)
        }

        Task {
            // If this character needs Shift, press Shift first
            if bbcKey.needsShift {
                await sendKeyDown(row: BBCKey.shift.row, column: BBCKey.shift.column)
            }

            // Press the actual key
            await sendKeyDown(row: bbcKey.row, column: bbcKey.column)
        }
    }

    /// Handle a key up event for a character
    /// - Parameter character: The character that was released
    func keyUp(character: Character) {
        guard let bbcKey = KeyboardMapper.characterToMatrix(character) else {
            // Character not mapped - ignore
            return
        }

        let position = BBCKeyPosition(row: bbcKey.row, column: bbcKey.column)

        // Only release if we think it's pressed
        guard pressedKeys.contains(position) else {
            print("[KeyboardClient] keyUp '\(character)' ignored - not in pressedKeys")
            return
        }

        print("[KeyboardClient] keyUp '\(character)' → row=\(bbcKey.row) col=\(bbcKey.column)")

        // Track released state SYNCHRONOUSLY before async work
        pressedKeys.remove(position)
        let needsShiftRelease = shiftedCharacters.contains(character)
        if needsShiftRelease {
            shiftedCharacters.remove(character)
        }

        Task {
            // Release the key
            await sendKeyUp(row: bbcKey.row, column: bbcKey.column)

            // If we pressed Shift for this character, release it
            if needsShiftRelease && shiftedCharacters.isEmpty {
                await sendKeyUp(row: BBCKey.shift.row, column: BBCKey.shift.column)
            }
        }
    }

    // MARK: - Private gRPC Methods

    private func sendKeyDown(row: Int, column: Int) async {
        guard let client = client else {
            print("[KeyboardClient] sendKeyDown: no client!")
            return
        }

        var request = Beebium_KeyRequest()
        request.row = UInt32(row)
        request.column = UInt32(column)

        do {
            _ = try await client.keyDown(request).response.get()
        } catch {
            print("[KeyboardClient] sendKeyDown gRPC error: \(error)")
        }
    }

    private func sendKeyUp(row: Int, column: Int) async {
        guard let client = client else {
            print("[KeyboardClient] sendKeyUp: no client!")
            return
        }

        var request = Beebium_KeyRequest()
        request.row = UInt32(row)
        request.column = UInt32(column)

        do {
            _ = try await client.keyUp(request).response.get()
        } catch {
            print("[KeyboardClient] sendKeyUp gRPC error: \(error)")
        }
    }
}
