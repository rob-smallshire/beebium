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

/// Translation between macOS virtual key codes (Carbon kVK_* values, the values
/// NSEvent.keyCode carries) and W3C UI Events `KeyboardEvent.code` values, in
/// both directions.
///
/// The cross-client keyboard mapping format (version 2, see
/// `docs/keyboard-mapping-files.md` sections 3.1 and 7, issue #231) keys entries
/// on `code` values so one file serves every client. This is the macOS client's
/// side of that translation: it names a key by position, independent of the
/// host layout. It is used when converting a version 1 file (macOS key code ->
/// `code`) and when reading a version 2 file (`code` -> macOS key code for
/// resolution).
///
/// The table mirrors the shared corpus at
/// `tests/assets/keyboard-mappings/macos-keycodes.json`; `W3CKeyCodeTests` pins
/// the two equal so they cannot drift. kVK_Function (63) and keys with no `code`
/// value are deliberately absent: they stay platform-specific entries (3.4).
enum W3CKeyCode {

    /// macOS virtual key code (kVK) -> W3C `code`. The authoritative direction.
    static let codeByMacKeyCode: [UInt16: String] = [
        0: "KeyA",
        1: "KeyS",
        2: "KeyD",
        3: "KeyF",
        4: "KeyH",
        5: "KeyG",
        6: "KeyZ",
        7: "KeyX",
        8: "KeyC",
        9: "KeyV",
        10: "IntlBackslash",
        11: "KeyB",
        12: "KeyQ",
        13: "KeyW",
        14: "KeyE",
        15: "KeyR",
        16: "KeyY",
        17: "KeyT",
        18: "Digit1",
        19: "Digit2",
        20: "Digit3",
        21: "Digit4",
        22: "Digit6",
        23: "Digit5",
        24: "Equal",
        25: "Digit9",
        26: "Digit7",
        27: "Minus",
        28: "Digit8",
        29: "Digit0",
        30: "BracketRight",
        31: "KeyO",
        32: "KeyU",
        33: "BracketLeft",
        34: "KeyI",
        35: "KeyP",
        36: "Enter",
        37: "KeyL",
        38: "KeyJ",
        39: "Quote",
        40: "KeyK",
        41: "Semicolon",
        42: "Backslash",
        43: "Comma",
        44: "Slash",
        45: "KeyN",
        46: "KeyM",
        47: "Period",
        48: "Tab",
        49: "Space",
        50: "Backquote",
        51: "Backspace",
        53: "Escape",
        54: "MetaRight",
        55: "MetaLeft",
        56: "ShiftLeft",
        57: "CapsLock",
        58: "AltLeft",
        59: "ControlLeft",
        60: "ShiftRight",
        61: "AltRight",
        62: "ControlRight",
        64: "F17",
        65: "NumpadDecimal",
        67: "NumpadMultiply",
        69: "NumpadAdd",
        71: "NumLock",
        72: "AudioVolumeUp",
        73: "AudioVolumeDown",
        74: "AudioVolumeMute",
        75: "NumpadDivide",
        76: "NumpadEnter",
        78: "NumpadSubtract",
        79: "F18",
        80: "F19",
        81: "NumpadEqual",
        82: "Numpad0",
        83: "Numpad1",
        84: "Numpad2",
        85: "Numpad3",
        86: "Numpad4",
        87: "Numpad5",
        88: "Numpad6",
        89: "Numpad7",
        90: "F20",
        91: "Numpad8",
        92: "Numpad9",
        93: "IntlYen",
        94: "IntlRo",
        95: "NumpadComma",
        96: "F5",
        97: "F6",
        98: "F7",
        99: "F3",
        100: "F8",
        101: "F9",
        102: "Lang2",
        103: "F11",
        104: "Lang1",
        105: "F13",
        106: "F16",
        107: "F14",
        109: "F10",
        110: "ContextMenu",
        111: "F12",
        113: "F15",
        114: "Insert",
        115: "Home",
        116: "PageUp",
        117: "Delete",
        118: "F4",
        119: "End",
        120: "F2",
        121: "PageDown",
        122: "F1",
        123: "ArrowLeft",
        124: "ArrowRight",
        125: "ArrowDown",
        126: "ArrowUp",
    ]

    /// W3C `code` -> macOS virtual key code, derived by inverting the table
    /// above. Each `code` maps to exactly one key code (the corpus has no
    /// duplicate codes), so the inverse is well defined.
    static let macKeyCodeByCode: [String: UInt16] = {
        var inverse: [String: UInt16] = [:]
        inverse.reserveCapacity(codeByMacKeyCode.count)
        for (keyCode, code) in codeByMacKeyCode {
            inverse[code] = keyCode
        }
        return inverse
    }()

    /// The W3C `code` for a macOS virtual key code, or nil for a key that has no
    /// neutral code (it stays a platform-specific entry).
    static func code(forMacKeyCode keyCode: UInt16) -> String? {
        codeByMacKeyCode[keyCode]
    }

    /// The macOS virtual key code for a W3C `code`, or nil if this host has no
    /// such key.
    static func macKeyCode(forCode code: String) -> UInt16? {
        macKeyCodeByCode[code]
    }
}
