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

import Foundation

/// One held key's contribution to BBC SHIFT/CTRL state, captured at press time.
///
/// `needsSyntheticShift` and `forbidsShift` are decided at the moment the key
/// went down. They do not change during the lifetime of the press, even if the
/// physical SHIFT state changes underneath. This is what makes the model
/// resilient to "release physical SHIFT while a character key is still held"
/// and to "type a BBC-shifted character (e.g. ':' or '\''): the BBC SHIFT
/// state has to be coherent with what the user typed at the moment of press".
struct PressedKeyFact {
    /// BBC key name this host key resolved to (used to detect direct-modifier
    /// mappings via "Shift" / "Ctrl").
    let bbcKeyName: String

    /// True iff resolved through the character-mapping path (cache lookup).
    let fromCharacterMapping: Bool

    /// While this key is held, BBC SHIFT must be down regardless of physical
    /// SHIFT state. Set when:
    ///   - resolved key needs SHIFT and physical SHIFT was NOT held at press
    ///     time -- we synthesise SHIFT so the BBC produces the character
    ///     (e.g. typing ' on a US keyboard maps to BBC '/'7' key which is the
    ///     shifted face);
    ///   - direct keyCode->BBC mapping with explicit bbcWithShift=true.
    let needsSyntheticShift: Bool

    /// While this key is held, BBC SHIFT must be released regardless of
    /// physical SHIFT state. Set only when physical SHIFT *was* held at press
    /// time and the resolved character on the BBC is the unshifted face
    /// (e.g. typing ':' via Shift+';' on a US keyboard).
    let forbidsShift: Bool

    /// Symmetric to needsSyntheticShift / forbidsShift for BBC CTRL.
    let needsSyntheticCtrl: Bool
    let forbidsCtrl: Bool
}

/// Pure decision functions for what BBC SHIFT / CTRL state should be, given
/// the set of currently-pressed host keys.
///
/// Decision order (per modifier):
///   1. If any held key has `forbids` set -> modifier off.
///   2. Else if any held key has `needsSynthetic` set -> modifier on.
///   3. Else: modifier follows physical state (the user holding the directly-
///      mapped SHIFT / CTRL key on the host).
enum BBCModifierState {

    static func desiredShift(pressedKeys: [PressedKeyFact]) -> Bool {
        if pressedKeys.contains(where: { $0.forbidsShift }) { return false }
        if pressedKeys.contains(where: { $0.needsSyntheticShift }) { return true }
        return pressedKeys.contains { isDirectShift($0) }
    }

    static func desiredCtrl(pressedKeys: [PressedKeyFact]) -> Bool {
        if pressedKeys.contains(where: { $0.forbidsCtrl }) { return false }
        if pressedKeys.contains(where: { $0.needsSyntheticCtrl }) { return true }
        return pressedKeys.contains { isDirectCtrl($0) }
    }

    /// Helper used at keyDown time to decide a character-mapped key's shift
    /// flags. Returns (needsSynthetic, forbids).
    static func shiftFlagsForCharacter(
        bbcShiftRequired: Bool,
        physicalShiftHeld: Bool
    ) -> (needsSynthetic: Bool, forbids: Bool) {
        if bbcShiftRequired {
            // Character requires BBC SHIFT. If physical is providing it,
            // ride physical (so releasing physical also releases BBC SHIFT,
            // matching the natural "BBC sees the new modifier state on
            // auto-repeat" behaviour). Otherwise synthesise.
            return (needsSynthetic: !physicalShiftHeld, forbids: false)
        } else {
            // Character forbids BBC SHIFT. Only matters if physical is
            // currently providing it -- otherwise nothing to suppress.
            return (needsSynthetic: false, forbids: physicalShiftHeld)
        }
    }

    /// Symmetric to shiftFlagsForCharacter for BBC CTRL.
    /// CTRL flags for a character-mapped key. Unlike SHIFT, a character never
    /// *forbids* BBC CTRL.
    ///
    /// SHIFT selects which character a key produces, so a character that
    /// resolves to an unshifted BBC face must suppress a physically-held
    /// SHIFT. CTRL does not select characters: the host has already folded it
    /// into a control code, and the BBC folds it again in hardware. Suppressing
    /// it would mean the guest never sees CTRL held -- indistinguishable, to
    /// software that scans the keyboard matrix, from the user not having
    /// pressed it. So CTRL is left to follow the physical state.
    static func ctrlFlagsForCharacter(
        bbcCtrlRequired: Bool,
        physicalCtrlHeld: Bool
    ) -> (needsSynthetic: Bool, forbids: Bool) {
        if bbcCtrlRequired {
            return (needsSynthetic: !physicalCtrlHeld, forbids: false)
        } else {
            return (needsSynthetic: false, forbids: false)
        }
    }

    private static func isDirectShift(_ k: PressedKeyFact) -> Bool {
        !k.fromCharacterMapping && k.bbcKeyName == "Shift"
    }

    private static func isDirectCtrl(_ k: PressedKeyFact) -> Bool {
        !k.fromCharacterMapping && k.bbcKeyName == "Ctrl"
    }
}
