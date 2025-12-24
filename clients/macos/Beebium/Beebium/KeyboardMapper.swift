import Foundation

/// BBC Micro keyboard matrix position with shift state
struct BBCKey: Hashable {
    let row: Int
    let column: Int
    let needsShift: Bool

    /// Shift key position (row 0, column 0)
    static let shift = BBCKey(row: 0, column: 0, needsShift: false)
}

/// Maps Unicode characters to BBC Micro keyboard matrix positions.
///
/// This is a logical mapping based on the character produced, not the physical
/// key position. The mapping is derived from B2's keys.inl and BeebEm's charToBeeb().
///
/// BBC Keyboard Matrix Layout (10 columns x 8 rows):
/// ```
/// Row 0: SHIFT(0), CTRL(1), [links 2-9]
/// Row 1: Q(0), 3(1), 4(2), 5(3), f4(4), 8(5), f7(6), -(7), ^/~(8), LEFT(9)
/// Row 2: f0(0), W(1), E(2), T(3), 7/'(4), I(5), 9(6), 0(7), £/_(8), DOWN(9)
/// Row 3: 1/!(0), 2/"(1), D(2), R(3), 6/&(4), U(5), O(6), P(7), [(8), UP(9)
/// Row 4: CAPS(0), A(1), X(2), F(3), Y(4), J(5), K(6), @(7), :/* (8), RETURN(9)
/// Row 5: SHIFTLOCK(0), S(1), C(2), G(3), H(4), N(5), L(6), ;/+(7), ](8), DELETE(9)
/// Row 6: TAB(0), Z(1), SPACE(2), V(3), B(4), M(5), ,/<(6), ./>(/7), //?(/8), COPY(9)
/// Row 7: ESC(0), f1(1), f2(2), f3(3), f5(4), f6(5), f8(6), f9(7), \(8), RIGHT(9)
/// ```
struct KeyboardMapper {

    /// Character to BBC key mapping table
    private static let characterMap: [Character: BBCKey] = [
        // Letters (lowercase - no shift needed)
        "a": BBCKey(row: 4, column: 1, needsShift: false),
        "b": BBCKey(row: 6, column: 4, needsShift: false),
        "c": BBCKey(row: 5, column: 2, needsShift: false),
        "d": BBCKey(row: 3, column: 2, needsShift: false),
        "e": BBCKey(row: 2, column: 2, needsShift: false),
        "f": BBCKey(row: 4, column: 3, needsShift: false),
        "g": BBCKey(row: 5, column: 3, needsShift: false),
        "h": BBCKey(row: 5, column: 4, needsShift: false),
        "i": BBCKey(row: 2, column: 5, needsShift: false),
        "j": BBCKey(row: 4, column: 5, needsShift: false),
        "k": BBCKey(row: 4, column: 6, needsShift: false),
        "l": BBCKey(row: 5, column: 6, needsShift: false),
        "m": BBCKey(row: 6, column: 5, needsShift: false),
        "n": BBCKey(row: 5, column: 5, needsShift: false),
        "o": BBCKey(row: 3, column: 6, needsShift: false),
        "p": BBCKey(row: 3, column: 7, needsShift: false),
        "q": BBCKey(row: 1, column: 0, needsShift: false),
        "r": BBCKey(row: 3, column: 3, needsShift: false),
        "s": BBCKey(row: 5, column: 1, needsShift: false),
        "t": BBCKey(row: 2, column: 3, needsShift: false),
        "u": BBCKey(row: 3, column: 5, needsShift: false),
        "v": BBCKey(row: 6, column: 3, needsShift: false),
        "w": BBCKey(row: 2, column: 1, needsShift: false),
        "x": BBCKey(row: 4, column: 2, needsShift: false),
        "y": BBCKey(row: 4, column: 4, needsShift: false),
        "z": BBCKey(row: 6, column: 1, needsShift: false),

        // Letters (uppercase - need shift)
        "A": BBCKey(row: 4, column: 1, needsShift: true),
        "B": BBCKey(row: 6, column: 4, needsShift: true),
        "C": BBCKey(row: 5, column: 2, needsShift: true),
        "D": BBCKey(row: 3, column: 2, needsShift: true),
        "E": BBCKey(row: 2, column: 2, needsShift: true),
        "F": BBCKey(row: 4, column: 3, needsShift: true),
        "G": BBCKey(row: 5, column: 3, needsShift: true),
        "H": BBCKey(row: 5, column: 4, needsShift: true),
        "I": BBCKey(row: 2, column: 5, needsShift: true),
        "J": BBCKey(row: 4, column: 5, needsShift: true),
        "K": BBCKey(row: 4, column: 6, needsShift: true),
        "L": BBCKey(row: 5, column: 6, needsShift: true),
        "M": BBCKey(row: 6, column: 5, needsShift: true),
        "N": BBCKey(row: 5, column: 5, needsShift: true),
        "O": BBCKey(row: 3, column: 6, needsShift: true),
        "P": BBCKey(row: 3, column: 7, needsShift: true),
        "Q": BBCKey(row: 1, column: 0, needsShift: true),
        "R": BBCKey(row: 3, column: 3, needsShift: true),
        "S": BBCKey(row: 5, column: 1, needsShift: true),
        "T": BBCKey(row: 2, column: 3, needsShift: true),
        "U": BBCKey(row: 3, column: 5, needsShift: true),
        "V": BBCKey(row: 6, column: 3, needsShift: true),
        "W": BBCKey(row: 2, column: 1, needsShift: true),
        "X": BBCKey(row: 4, column: 2, needsShift: true),
        "Y": BBCKey(row: 4, column: 4, needsShift: true),
        "Z": BBCKey(row: 6, column: 1, needsShift: true),

        // Digits (unshifted)
        "0": BBCKey(row: 2, column: 7, needsShift: false),
        "1": BBCKey(row: 3, column: 0, needsShift: false),
        "2": BBCKey(row: 3, column: 1, needsShift: false),
        "3": BBCKey(row: 1, column: 1, needsShift: false),
        "4": BBCKey(row: 1, column: 2, needsShift: false),
        "5": BBCKey(row: 1, column: 3, needsShift: false),
        "6": BBCKey(row: 3, column: 4, needsShift: false),
        "7": BBCKey(row: 2, column: 4, needsShift: false),
        "8": BBCKey(row: 1, column: 5, needsShift: false),
        "9": BBCKey(row: 2, column: 6, needsShift: false),

        // Shifted digits produce symbols (BBC keyboard layout)
        "!": BBCKey(row: 3, column: 0, needsShift: true),  // Shift+1
        "\"": BBCKey(row: 3, column: 1, needsShift: true), // Shift+2
        "#": BBCKey(row: 1, column: 1, needsShift: true),  // Shift+3
        "$": BBCKey(row: 1, column: 2, needsShift: true),  // Shift+4
        "%": BBCKey(row: 1, column: 3, needsShift: true),  // Shift+5
        "&": BBCKey(row: 3, column: 4, needsShift: true),  // Shift+6
        "'": BBCKey(row: 2, column: 4, needsShift: true),  // Shift+7
        "(": BBCKey(row: 1, column: 5, needsShift: true),  // Shift+8
        ")": BBCKey(row: 2, column: 6, needsShift: true),  // Shift+9

        // Punctuation (unshifted)
        " ": BBCKey(row: 6, column: 2, needsShift: false),  // Space
        ",": BBCKey(row: 6, column: 6, needsShift: false),  // Comma
        ".": BBCKey(row: 6, column: 7, needsShift: false),  // Period
        "/": BBCKey(row: 6, column: 8, needsShift: false),  // Slash
        ";": BBCKey(row: 5, column: 7, needsShift: false),  // Semicolon
        ":": BBCKey(row: 4, column: 8, needsShift: false),  // Colon
        "[": BBCKey(row: 3, column: 8, needsShift: false),  // Left bracket
        "]": BBCKey(row: 5, column: 8, needsShift: false),  // Right bracket
        "-": BBCKey(row: 1, column: 7, needsShift: false),  // Minus/hyphen
        "^": BBCKey(row: 1, column: 8, needsShift: false),  // Caret
        "@": BBCKey(row: 4, column: 7, needsShift: false),  // At sign
        "\\": BBCKey(row: 7, column: 8, needsShift: false), // Backslash

        // Shifted punctuation
        "<": BBCKey(row: 6, column: 6, needsShift: true),   // Shift+,
        ">": BBCKey(row: 6, column: 7, needsShift: true),   // Shift+.
        "?": BBCKey(row: 6, column: 8, needsShift: true),   // Shift+/
        "+": BBCKey(row: 5, column: 7, needsShift: true),   // Shift+;
        "*": BBCKey(row: 4, column: 8, needsShift: true),   // Shift+:
        "{": BBCKey(row: 3, column: 8, needsShift: true),   // Shift+[
        "}": BBCKey(row: 5, column: 8, needsShift: true),   // Shift+]
        "=": BBCKey(row: 1, column: 7, needsShift: true),   // Shift+-
        "~": BBCKey(row: 1, column: 8, needsShift: true),   // Shift+^
        "_": BBCKey(row: 2, column: 8, needsShift: false),  // Underscore (unshifted on BBC)
        "£": BBCKey(row: 2, column: 8, needsShift: true),   // Shift+_ (pound sign)
        "|": BBCKey(row: 7, column: 8, needsShift: true),   // Shift+\

        // Control keys
        "\r": BBCKey(row: 4, column: 9, needsShift: false), // Return/Enter
        "\n": BBCKey(row: 4, column: 9, needsShift: false), // Newline (treat as Return)
    ]

    /// Map a character to its BBC keyboard matrix position.
    /// - Parameter char: The Unicode character to map
    /// - Returns: The BBC key position, or nil if the character is not mapped
    static func characterToMatrix(_ char: Character) -> BBCKey? {
        return characterMap[char]
    }

    /// Check if a character requires the BBC Shift key to be pressed
    /// - Parameter char: The character to check
    /// - Returns: true if the character requires Shift on the BBC keyboard
    static func requiresShift(_ char: Character) -> Bool {
        return characterMap[char]?.needsShift ?? false
    }
}
