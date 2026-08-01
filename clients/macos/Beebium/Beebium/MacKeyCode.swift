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

/// macOS virtual key codes (from Carbon/HIToolbox/Events.h).
///
/// These correspond to physical key positions on an ANSI keyboard layout.
/// The `kVK_ANSI_*` codes represent the physical position where that key
/// appears on a US ANSI keyboard, not the character produced.
enum MacKeyCode {

    // MARK: - Letters (ANSI physical positions)

    static let a: UInt16 = 0x00
    static let s: UInt16 = 0x01
    static let d: UInt16 = 0x02
    static let f: UInt16 = 0x03
    static let h: UInt16 = 0x04
    static let g: UInt16 = 0x05
    static let z: UInt16 = 0x06
    static let x: UInt16 = 0x07
    static let c: UInt16 = 0x08
    static let v: UInt16 = 0x09
    static let b: UInt16 = 0x0B
    static let q: UInt16 = 0x0C
    static let w: UInt16 = 0x0D
    static let e: UInt16 = 0x0E
    static let r: UInt16 = 0x0F
    static let y: UInt16 = 0x10
    static let t: UInt16 = 0x11
    static let o: UInt16 = 0x1F
    static let u: UInt16 = 0x20
    static let i: UInt16 = 0x22
    static let p: UInt16 = 0x23
    static let l: UInt16 = 0x25
    static let j: UInt16 = 0x26
    static let k: UInt16 = 0x28
    static let n: UInt16 = 0x2D
    static let m: UInt16 = 0x2E

    // MARK: - Digits

    static let digit1: UInt16 = 0x12
    static let digit2: UInt16 = 0x13
    static let digit3: UInt16 = 0x14
    static let digit4: UInt16 = 0x15
    static let digit5: UInt16 = 0x17
    static let digit6: UInt16 = 0x16
    static let digit7: UInt16 = 0x1A
    static let digit8: UInt16 = 0x1C
    static let digit9: UInt16 = 0x19
    static let digit0: UInt16 = 0x1D

    // MARK: - Punctuation

    static let equal: UInt16 = 0x18
    static let minus: UInt16 = 0x1B
    static let rightBracket: UInt16 = 0x1E
    static let leftBracket: UInt16 = 0x21
    static let quote: UInt16 = 0x27
    static let semicolon: UInt16 = 0x29
    static let backslash: UInt16 = 0x2A
    static let comma: UInt16 = 0x2B
    static let slash: UInt16 = 0x2C
    static let period: UInt16 = 0x2F
    static let grave: UInt16 = 0x32

    // MARK: - Special Keys

    static let returnKey: UInt16 = 0x24
    static let tab: UInt16 = 0x30
    static let space: UInt16 = 0x31
    static let delete: UInt16 = 0x33
    static let escape: UInt16 = 0x35
    static let command: UInt16 = 0x37
    static let shift: UInt16 = 0x38
    static let capsLock: UInt16 = 0x39
    static let option: UInt16 = 0x3A
    static let control: UInt16 = 0x3B
    static let rightCommand: UInt16 = 0x36
    static let rightShift: UInt16 = 0x3C
    static let rightOption: UInt16 = 0x3D
    static let rightControl: UInt16 = 0x3E
    static let function: UInt16 = 0x3F

    /// Host modifier keys that should not trigger unmapped key sounds.
    /// These are macOS modifier keys used for host shortcuts (Cmd-K, etc.)
    /// and should be silently ignored when unmapped.
    static let hostModifierKeys: Set<UInt16> = [
        shift, rightShift,
        control, rightControl,
        option, rightOption,
        command, rightCommand,
        function
    ]

    /// Check if a key code is a host modifier key
    static func isHostModifierKey(_ keyCode: UInt16) -> Bool {
        hostModifierKeys.contains(keyCode)
    }

    // MARK: - Function Keys

    static let f1: UInt16 = 0x7A
    static let f2: UInt16 = 0x78
    static let f3: UInt16 = 0x63
    static let f4: UInt16 = 0x76
    static let f5: UInt16 = 0x60
    static let f6: UInt16 = 0x61
    static let f7: UInt16 = 0x62
    static let f8: UInt16 = 0x64
    static let f9: UInt16 = 0x65
    static let f10: UInt16 = 0x6D
    static let f11: UInt16 = 0x67
    static let f12: UInt16 = 0x6F
    static let f13: UInt16 = 0x69
    static let f14: UInt16 = 0x6B
    static let f15: UInt16 = 0x71
    static let f16: UInt16 = 0x6A
    static let f17: UInt16 = 0x40
    static let f18: UInt16 = 0x4F
    static let f19: UInt16 = 0x50
    static let f20: UInt16 = 0x5A

    // MARK: - Arrow Keys

    static let upArrow: UInt16 = 0x7E
    static let downArrow: UInt16 = 0x7D
    static let leftArrow: UInt16 = 0x7B
    static let rightArrow: UInt16 = 0x7C

    // MARK: - Navigation Keys

    static let home: UInt16 = 0x73
    static let end: UInt16 = 0x77
    static let pageUp: UInt16 = 0x74
    static let pageDown: UInt16 = 0x79
    static let forwardDelete: UInt16 = 0x75
    static let help: UInt16 = 0x72

    // MARK: - Keypad

    static let keypad0: UInt16 = 0x52
    static let keypad1: UInt16 = 0x53
    static let keypad2: UInt16 = 0x54
    static let keypad3: UInt16 = 0x55
    static let keypad4: UInt16 = 0x56
    static let keypad5: UInt16 = 0x57
    static let keypad6: UInt16 = 0x58
    static let keypad7: UInt16 = 0x59
    static let keypad8: UInt16 = 0x5B
    static let keypad9: UInt16 = 0x5C
    static let keypadDecimal: UInt16 = 0x41
    static let keypadMultiply: UInt16 = 0x43
    static let keypadPlus: UInt16 = 0x45
    static let keypadClear: UInt16 = 0x47
    static let keypadDivide: UInt16 = 0x4B
    static let keypadEnter: UInt16 = 0x4C
    static let keypadMinus: UInt16 = 0x4E
    static let keypadEquals: UInt16 = 0x51

    // MARK: - ISO Keyboard

    static let isoSection: UInt16 = 0x0A

    // MARK: - Display Names

    /// Short name for a key code (for compact display in reference tables)
    /// Uses symbols for special keys where appropriate. An unrecognised code
    /// yields "?", so callers never need a fallback of their own.
    static func name(for keyCode: UInt16) -> String {
        switch keyCode {
        // Letters
        case a: return "A"
        case b: return "B"
        case c: return "C"
        case d: return "D"
        case e: return "E"
        case f: return "F"
        case g: return "G"
        case h: return "H"
        case i: return "I"
        case j: return "J"
        case k: return "K"
        case l: return "L"
        case m: return "M"
        case n: return "N"
        case o: return "O"
        case p: return "P"
        case q: return "Q"
        case r: return "R"
        case s: return "S"
        case t: return "T"
        case u: return "U"
        case v: return "V"
        case w: return "W"
        case x: return "X"
        case y: return "Y"
        case z: return "Z"

        // Digits
        case digit0: return "0"
        case digit1: return "1"
        case digit2: return "2"
        case digit3: return "3"
        case digit4: return "4"
        case digit5: return "5"
        case digit6: return "6"
        case digit7: return "7"
        case digit8: return "8"
        case digit9: return "9"

        // Function keys
        case f1: return "F1"
        case f2: return "F2"
        case f3: return "F3"
        case f4: return "F4"
        case f5: return "F5"
        case f6: return "F6"
        case f7: return "F7"
        case f8: return "F8"
        case f9: return "F9"
        case f10: return "F10"
        case f11: return "F11"
        case f12: return "F12"
        case f13: return "F13"
        case f14: return "F14"
        case f15: return "F15"
        case f16: return "F16"
        case f17: return "F17"
        case f18: return "F18"
        case f19: return "F19"
        case f20: return "F20"

        // Special keys (with symbols)
        case returnKey: return "↩"
        case tab: return "⇥"
        case space: return "Space"
        case delete: return "⌫"
        case escape: return "⎋"
        case capsLock: return "⇪"

        // Modifier keys (shouldn't appear as base keys, but handle gracefully)
        case shift, rightShift: return "⇧"
        case control, rightControl: return "⌃"
        case option, rightOption: return "⌥"
        case command, rightCommand: return "⌘"
        case function: return "Fn"

        // Arrow keys
        case upArrow: return "↑"
        case downArrow: return "↓"
        case leftArrow: return "←"
        case rightArrow: return "→"

        // Navigation
        case home: return "Home"
        case end: return "End"
        case pageUp: return "Page Up"
        case pageDown: return "Page Down"
        case forwardDelete: return "⌦"
        case help: return "Help"

        // Punctuation
        case equal: return "="
        case minus: return "-"
        case leftBracket: return "["
        case rightBracket: return "]"
        case quote: return "'"
        case semicolon: return ";"
        case backslash: return "\\"
        case comma: return ","
        case period: return "."
        case slash: return "/"
        case grave: return "`"

        default:
            return "?"
        }
    }

    /// Human-readable name for a key code (for UI display)
    static func displayName(for keyCode: UInt16) -> String {
        switch keyCode {
        // Letters
        case a: return "A"
        case b: return "B"
        case c: return "C"
        case d: return "D"
        case e: return "E"
        case f: return "F"
        case g: return "G"
        case h: return "H"
        case i: return "I"
        case j: return "J"
        case k: return "K"
        case l: return "L"
        case m: return "M"
        case n: return "N"
        case o: return "O"
        case p: return "P"
        case q: return "Q"
        case r: return "R"
        case s: return "S"
        case t: return "T"
        case u: return "U"
        case v: return "V"
        case w: return "W"
        case x: return "X"
        case y: return "Y"
        case z: return "Z"

        // Digits
        case digit0: return "0"
        case digit1: return "1"
        case digit2: return "2"
        case digit3: return "3"
        case digit4: return "4"
        case digit5: return "5"
        case digit6: return "6"
        case digit7: return "7"
        case digit8: return "8"
        case digit9: return "9"

        // Function keys
        case f1: return "F1"
        case f2: return "F2"
        case f3: return "F3"
        case f4: return "F4"
        case f5: return "F5"
        case f6: return "F6"
        case f7: return "F7"
        case f8: return "F8"
        case f9: return "F9"
        case f10: return "F10"
        case f11: return "F11"
        case f12: return "F12"
        case f13: return "F13"
        case f14: return "F14"
        case f15: return "F15"
        case f16: return "F16"
        case f17: return "F17"
        case f18: return "F18"
        case f19: return "F19"
        case f20: return "F20"

        // Special keys
        case returnKey: return "Return"
        case tab: return "Tab"
        case space: return "Space"
        case delete: return "Delete"
        case escape: return "Escape"
        case capsLock: return "Caps Lock"
        case shift, rightShift: return "Shift"
        case control, rightControl: return "Control"
        case option, rightOption: return "Option"
        case command, rightCommand: return "Command"
        case function: return "Fn"

        // Arrow keys
        case upArrow: return "Up"
        case downArrow: return "Down"
        case leftArrow: return "Left"
        case rightArrow: return "Right"

        // Navigation
        case home: return "Home"
        case end: return "End"
        case pageUp: return "Page Up"
        case pageDown: return "Page Down"
        case forwardDelete: return "Forward Delete"
        case help: return "Help"

        // Punctuation
        case equal: return "="
        case minus: return "-"
        case leftBracket: return "["
        case rightBracket: return "]"
        case quote: return "'"
        case semicolon: return ";"
        case backslash: return "\\"
        case comma: return ","
        case period: return "."
        case slash: return "/"
        case grave: return "`"

        default:
            return String(format: "0x%02X", keyCode)
        }
    }
}
