import { describe, it, expect } from "vitest";
import {
    charToMatrix,
    matrixToChar,
    SHIFT_KEY,
    CTRL_KEY,
    RETURN_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    SPACE_KEY,
} from "../src/keyboard-map.js";

describe("special key constants", () => {
    it("SHIFT_KEY is [0, 0]", () => {
        expect(SHIFT_KEY).toEqual([0, 0]);
    });

    it("CTRL_KEY is [0, 1]", () => {
        expect(CTRL_KEY).toEqual([0, 1]);
    });

    it("RETURN_KEY is [4, 9]", () => {
        expect(RETURN_KEY).toEqual([4, 9]);
    });

    it("DELETE_KEY is [5, 9]", () => {
        expect(DELETE_KEY).toEqual([5, 9]);
    });

    it("ESCAPE_KEY is [7, 0]", () => {
        expect(ESCAPE_KEY).toEqual([7, 0]);
    });

    it("SPACE_KEY is [6, 2]", () => {
        expect(SPACE_KEY).toEqual([6, 2]);
    });
});

describe("charToMatrix - exact key positions (matching Python client)", () => {
    it('"a" -> [4, 1, false]', () => {
        expect(charToMatrix("a")).toEqual([4, 1, false]);
    });

    it('"A" -> [4, 1, true]', () => {
        expect(charToMatrix("A")).toEqual([4, 1, true]);
    });

    it('"0" -> [2, 7, false]', () => {
        expect(charToMatrix("0")).toEqual([2, 7, false]);
    });

    it('"!" -> [3, 0, true] (shift+1)', () => {
        expect(charToMatrix("!")).toEqual([3, 0, true]);
    });

    it('" " -> [6, 2, false]', () => {
        expect(charToMatrix(" ")).toEqual([6, 2, false]);
    });

    it('"*" -> [4, 8, true]', () => {
        expect(charToMatrix("*")).toEqual([4, 8, true]);
    });

    it('"\\r" -> [4, 9, false]', () => {
        expect(charToMatrix("\r")).toEqual([4, 9, false]);
    });

    it('"z" -> [6, 1, false]', () => {
        expect(charToMatrix("z")).toEqual([6, 1, false]);
    });

    it('"m" -> [6, 5, false]', () => {
        expect(charToMatrix("m")).toEqual([6, 5, false]);
    });

    it('"Z" -> [6, 1, true]', () => {
        expect(charToMatrix("Z")).toEqual([6, 1, true]);
    });
});

describe("charToMatrix - lowercase letters", () => {
    const letters = "abcdefghijklmnopqrstuvwxyz";

    it("all 26 lowercase letters are mapped", () => {
        for (const ch of letters) {
            const result = charToMatrix(ch);
            expect(result, `letter '${ch}' should be mapped`).toBeDefined();
        }
    });

    it("no lowercase letter needs shift", () => {
        for (const ch of letters) {
            const [, , needsShift] = charToMatrix(ch)!;
            expect(needsShift, `'${ch}' should not need shift`).toBe(false);
        }
    });
});

describe("charToMatrix - uppercase letters", () => {
    const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    it("all 26 uppercase letters are mapped", () => {
        for (const ch of letters) {
            const result = charToMatrix(ch);
            expect(result, `letter '${ch}' should be mapped`).toBeDefined();
        }
    });

    it("all uppercase letters need shift", () => {
        for (const ch of letters) {
            const [, , needsShift] = charToMatrix(ch)!;
            expect(needsShift, `'${ch}' should need shift`).toBe(true);
        }
    });
});

describe("charToMatrix - uppercase and lowercase share row/column", () => {
    it("every lowercase letter has matching uppercase with same row/col but needsShift=true", () => {
        for (let i = 0; i < 26; i++) {
            const lower = String.fromCharCode(97 + i);
            const upper = String.fromCharCode(65 + i);
            const [lRow, lCol, lShift] = charToMatrix(lower)!;
            const [uRow, uCol, uShift] = charToMatrix(upper)!;
            expect(lRow, `row for '${lower}' and '${upper}'`).toBe(uRow);
            expect(lCol, `col for '${lower}' and '${upper}'`).toBe(uCol);
            expect(lShift).toBe(false);
            expect(uShift).toBe(true);
        }
    });
});

describe("charToMatrix - digits", () => {
    it("all 10 digits are mapped without shift", () => {
        for (let d = 0; d <= 9; d++) {
            const ch = String(d);
            const result = charToMatrix(ch);
            expect(result, `digit '${ch}' should be mapped`).toBeDefined();
            expect(result![2], `digit '${ch}' should not need shift`).toBe(false);
        }
    });
});

describe("charToMatrix - shifted symbols", () => {
    const shiftedSymbols: [string, number, number][] = [
        ["!", 3, 0],
        ['"', 3, 1],
        ["#", 1, 1],
        ["$", 1, 2],
        ["%", 1, 3],
        ["&", 3, 4],
        ["'", 2, 4],
        ["(", 1, 5],
        [")", 2, 6],
    ];

    for (const [ch, row, col] of shiftedSymbols) {
        it(`'${ch}' -> [${row}, ${col}, true]`, () => {
            expect(charToMatrix(ch)).toEqual([row, col, true]);
        });
    }
});

describe("charToMatrix - punctuation", () => {
    it("comma -> [6, 6, false]", () => {
        expect(charToMatrix(",")).toEqual([6, 6, false]);
    });

    it("period -> [6, 7, false]", () => {
        expect(charToMatrix(".")).toEqual([6, 7, false]);
    });

    it("semicolon -> [5, 7, false]", () => {
        expect(charToMatrix(";")).toEqual([5, 7, false]);
    });

    it("colon -> [4, 8, false]", () => {
        expect(charToMatrix(":")).toEqual([4, 8, false]);
    });
});

describe("charToMatrix - control characters", () => {
    it("\\r maps to RETURN_KEY position", () => {
        expect(charToMatrix("\r")).toEqual([4, 9, false]);
    });

    it("\\n maps to RETURN_KEY position", () => {
        expect(charToMatrix("\n")).toEqual([4, 9, false]);
    });
});

describe("charToMatrix - unmapped characters", () => {
    it("returns undefined for unmapped character", () => {
        expect(charToMatrix("\x01")).toBeUndefined();
    });

    it("returns undefined for non-ASCII character", () => {
        expect(charToMatrix("\u00E9")).toBeUndefined();
    });
});

describe("matrixToChar", () => {
    it("reverse lookup works for 'a'", () => {
        expect(matrixToChar(4, 1, false)).toBe("a");
    });

    it("reverse lookup works for 'A' (shifted)", () => {
        expect(matrixToChar(4, 1, true)).toBe("A");
    });

    it("reverse lookup for space", () => {
        expect(matrixToChar(6, 2, false)).toBe(" ");
    });

    it("returns undefined for unmapped position", () => {
        // Row 0, Column 0 is SHIFT_KEY, not a character
        expect(matrixToChar(0, 0, false)).toBeUndefined();
    });

    it("returns undefined for out-of-range position", () => {
        expect(matrixToChar(9, 9, false)).toBeUndefined();
    });
});

describe("completeness", () => {
    it("all 26 letters and 10 digits are mapped", () => {
        let mappedLetters = 0;
        let mappedDigits = 0;
        for (let i = 0; i < 26; i++) {
            if (charToMatrix(String.fromCharCode(97 + i)) !== undefined) mappedLetters++;
        }
        for (let d = 0; d <= 9; d++) {
            if (charToMatrix(String(d)) !== undefined) mappedDigits++;
        }
        expect(mappedLetters).toBe(26);
        expect(mappedDigits).toBe(10);
    });
});
