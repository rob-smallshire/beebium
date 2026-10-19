import { describe, it, expect } from "vitest";
import {
    SCREEN_MODES,
    readMode7Screen,
    rowStartsWith,
    screenContains,
    type TeletextSource,
} from "../src/screen.js";
import type { TeletextScreen } from "../src/video.js";
import type { TeletextScreenCell } from "../src/generated/video.js";

const ROWS = 25;
const COLUMNS = 40;

/** A fake video service whose teletext grid shows `lines` at the top. */
function showing(lines: Array<string | number[]>, active = true): TeletextSource {
    const codes = new Array<number>(ROWS * COLUMNS).fill(0x20);
    lines.forEach((line, row) => {
        const rowCodes = typeof line === "string" ? [...line].map((c) => c.charCodeAt(0)) : line;
        rowCodes.forEach((code, column) => { codes[row * COLUMNS + column] = code; });
    });
    const cells = codes.map((character) => ({ character }) as TeletextScreenCell);
    const screen: TeletextScreen = {
        active,
        rows: ROWS,
        columns: COLUMNS,
        text: "",
        frameNumber: 1,
        cells,
        cell: (row, column) => cells[row * COLUMNS + column]!,
    };
    return { teletextScreen: async () => screen };
}

describe("SCREEN_MODES", () => {
    it("has entries for modes 0-7", () => {
        for (let mode = 0; mode <= 7; mode++) {
            expect(SCREEN_MODES[mode], `mode ${mode} should exist`).toBeDefined();
        }
    });
});

describe("readMode7Screen", () => {
    it("reads the displayed grid as text", async () => {
        const screen = await readMode7Screen(showing(["HELLO"]));
        const lines = screen.split("\n");
        expect(lines.length).toBe(25);
        expect(lines[0]).toBe("HELLO");
        expect(lines[1]).toBe("");
    });

    it("replaces control codes with spaces", async () => {
        const screen = await readMode7Screen(showing([[0x01, 0x41, 0x7f, 0x42]]));
        expect(screen.split("\n")[0]).toBe(" A B");
    });

    it("supports startRow and endRow", async () => {
        const rows = Array.from({ length: 6 }, (_, i) => `ROW${i}`);
        const screen = await readMode7Screen(showing(rows), 5, 6);
        expect(screen.split("\n")).toEqual(["ROW5"]);
    });

    it("clamps startRow and endRow to the screen", async () => {
        const screen = await readMode7Screen(showing([]), -5, 100);
        expect(screen.split("\n").length).toBe(25);
    });

    it("reads blank when the display is not MODE 7", async () => {
        // So a polling loop can read the screen from launch, before the first
        // MODE 7 frame has been displayed.
        expect(await readMode7Screen(showing(["STALE"], false))).toBe("\n".repeat(24));
    });
});

describe("screenContains", () => {
    it("finds text (case sensitive)", async () => {
        expect(await screenContains(showing(["BBC MICRO"]), "BBC")).toBe(true);
        expect(await screenContains(showing(["BBC MICRO"]), "bbc")).toBe(false);
    });

    it("finds text case-insensitively", async () => {
        expect(await screenContains(showing(["BBC MICRO"]), "bbc micro", false)).toBe(true);
    });

    it("returns false when the text is absent", async () => {
        expect(await screenContains(showing([]), "HELLO")).toBe(false);
    });

    it("finds nothing on a display that is not MODE 7", async () => {
        expect(await screenContains(showing(["STALE"], false), "STALE")).toBe(false);
        expect(await rowStartsWith(showing(["STALE"], false), "S")).toBe(false);
    });
});

describe("rowStartsWith", () => {
    it("finds a row starting with the prefix", async () => {
        expect(await rowStartsWith(showing(["BASIC", "", ">"]), ">")).toBe(true);
        expect(await rowStartsWith(showing(["A>B"]), ">")).toBe(false);
    });
});
