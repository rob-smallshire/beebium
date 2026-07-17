/**
 * BBC Micro keyboard matrix mapping.
 *
 * Maps Unicode characters to BBC keyboard matrix positions (row, column).
 * Based on the BBC Micro keyboard layout.
 *
 * BBC Keyboard Matrix Layout (10 columns x 8 rows):
 *     Row 0: SHIFT(0), CTRL(1), [links 2-9]
 *     Row 1: Q(0), 3(1), 4(2), 5(3), f4(4), 8(5), f7(6), -(7), ^/~(8), LEFT(9)
 *     Row 2: f0(0), W(1), E(2), T(3), 7/'(4), I(5), 9(6), 0(7), _/pound(8), DOWN(9)
 *     Row 3: 1/!(0), 2/"(1), D(2), R(3), 6/&(4), U(5), O(6), P(7), [(8), UP(9)
 *     Row 4: CAPS(0), A(1), X(2), F(3), Y(4), J(5), K(6), @(7), :/* (8), RETURN(9)
 *     Row 5: SHIFTLOCK(0), S(1), C(2), G(3), H(4), N(5), L(6), ;/+(7), ](8), DELETE(9)
 *     Row 6: TAB(0), Z(1), SPACE(2), V(3), B(4), M(5), ,/<(6), ./>(/7), //?(/8), COPY(9)
 *     Row 7: ESC(0), f1(1), f2(2), f3(3), f5(4), f6(5), f8(6), f9(7), \\(8), RIGHT(9)
 */

// Special key positions
export const SHIFT_KEY: [number, number] = [0, 0];
export const CTRL_KEY: [number, number] = [0, 1];
export const RETURN_KEY: [number, number] = [4, 9];
export const DELETE_KEY: [number, number] = [5, 9];
export const ESCAPE_KEY: [number, number] = [7, 0];
export const SPACE_KEY: [number, number] = [6, 2];

// Character to [row, column, needsShift] mapping
const CHARACTER_MAP = new Map<string, [row: number, column: number, needsShift: boolean]>([
    // Letters (lowercase - no shift)
    ["a", [4, 1, false]],
    ["b", [6, 4, false]],
    ["c", [5, 2, false]],
    ["d", [3, 2, false]],
    ["e", [2, 2, false]],
    ["f", [4, 3, false]],
    ["g", [5, 3, false]],
    ["h", [5, 4, false]],
    ["i", [2, 5, false]],
    ["j", [4, 5, false]],
    ["k", [4, 6, false]],
    ["l", [5, 6, false]],
    ["m", [6, 5, false]],
    ["n", [5, 5, false]],
    ["o", [3, 6, false]],
    ["p", [3, 7, false]],
    ["q", [1, 0, false]],
    ["r", [3, 3, false]],
    ["s", [5, 1, false]],
    ["t", [2, 3, false]],
    ["u", [3, 5, false]],
    ["v", [6, 3, false]],
    ["w", [2, 1, false]],
    ["x", [4, 2, false]],
    ["y", [4, 4, false]],
    ["z", [6, 1, false]],
    // Letters (uppercase - need shift)
    ["A", [4, 1, true]],
    ["B", [6, 4, true]],
    ["C", [5, 2, true]],
    ["D", [3, 2, true]],
    ["E", [2, 2, true]],
    ["F", [4, 3, true]],
    ["G", [5, 3, true]],
    ["H", [5, 4, true]],
    ["I", [2, 5, true]],
    ["J", [4, 5, true]],
    ["K", [4, 6, true]],
    ["L", [5, 6, true]],
    ["M", [6, 5, true]],
    ["N", [5, 5, true]],
    ["O", [3, 6, true]],
    ["P", [3, 7, true]],
    ["Q", [1, 0, true]],
    ["R", [3, 3, true]],
    ["S", [5, 1, true]],
    ["T", [2, 3, true]],
    ["U", [3, 5, true]],
    ["V", [6, 3, true]],
    ["W", [2, 1, true]],
    ["X", [4, 2, true]],
    ["Y", [4, 4, true]],
    ["Z", [6, 1, true]],
    // Digits (unshifted)
    ["0", [2, 7, false]],
    ["1", [3, 0, false]],
    ["2", [3, 1, false]],
    ["3", [1, 1, false]],
    ["4", [1, 2, false]],
    ["5", [1, 3, false]],
    ["6", [3, 4, false]],
    ["7", [2, 4, false]],
    ["8", [1, 5, false]],
    ["9", [2, 6, false]],
    // Shifted digits produce symbols
    ["!", [3, 0, true]],   // Shift+1
    ['"', [3, 1, true]],   // Shift+2
    ["#", [1, 1, true]],   // Shift+3
    ["$", [1, 2, true]],   // Shift+4
    ["%", [1, 3, true]],   // Shift+5
    ["&", [3, 4, true]],   // Shift+6
    ["'", [2, 4, true]],   // Shift+7
    ["(", [1, 5, true]],   // Shift+8
    [")", [2, 6, true]],   // Shift+9
    // Punctuation (unshifted)
    [" ", [6, 2, false]],   // Space
    [",", [6, 6, false]],   // Comma
    [".", [6, 7, false]],   // Period
    ["/", [6, 8, false]],   // Slash
    [";", [5, 7, false]],   // Semicolon
    [":", [4, 8, false]],   // Colon
    ["[", [3, 8, false]],   // Left bracket
    ["]", [5, 8, false]],   // Right bracket
    ["-", [1, 7, false]],   // Minus/hyphen
    ["^", [1, 8, false]],   // Caret
    ["@", [4, 7, false]],   // At sign
    ["\\", [7, 8, false]],  // Backslash
    ["_", [2, 8, false]],   // Underscore (unshifted on BBC)
    // Shifted punctuation
    ["<", [6, 6, true]],   // Shift+,
    [">", [6, 7, true]],   // Shift+.
    ["?", [6, 8, true]],   // Shift+/
    ["+", [5, 7, true]],   // Shift+;
    ["*", [4, 8, true]],   // Shift+:
    ["{", [3, 8, true]],   // Shift+[
    ["}", [5, 8, true]],   // Shift+]
    ["=", [1, 7, true]],   // Shift+-
    ["~", [1, 8, true]],   // Shift+^
    ["|", [7, 8, true]],   // Shift+\
    // Control characters
    ["\r", [4, 9, false]],  // Return/Enter
    ["\n", [4, 9, false]],  // Newline (treat as Return)
]);

/**
 * Map a character to BBC keyboard matrix position.
 *
 * @param char - A single character to map.
 * @returns A tuple of [row, column, needsShift], or undefined if unmapped.
 */
export function charToMatrix(char: string): [number, number, boolean] | undefined {
    return CHARACTER_MAP.get(char);
}

/**
 * Map a matrix position back to a character.
 *
 * @param row - The keyboard matrix row (0-7).
 * @param column - The keyboard matrix column (0-9).
 * @param shifted - Whether shift is pressed.
 * @returns The character, or undefined if not a character key.
 */
export function matrixToChar(row: number, column: number, shifted: boolean): string | undefined {
    for (const [char, [r, c, needsShift]] of CHARACTER_MAP) {
        if (r === row && c === column && needsShift === shifted) {
            return char;
        }
    }
    return undefined;
}
