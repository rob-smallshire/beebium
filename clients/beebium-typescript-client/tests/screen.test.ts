import { describe, it, expect } from "vitest";
import {
    SCREEN_MODES,
    MODE7_BASE,
    MODE7_BYTES_PER_LINE,
    MODE7_LINES,
    readMode7Screen,
    screenContains,
    type ReadFn,
} from "../src/screen.js";

describe("screen constants", () => {
    it("SCREEN_MODES has entries for modes 0-7", () => {
        for (let mode = 0; mode <= 7; mode++) {
            expect(SCREEN_MODES[mode], `mode ${mode} should exist`).toBeDefined();
        }
    });

    it("MODE7_BASE is 0x7C00", () => {
        expect(MODE7_BASE).toBe(0x7C00);
    });

    it("MODE7_BYTES_PER_LINE is 40", () => {
        expect(MODE7_BYTES_PER_LINE).toBe(40);
    });

    it("MODE7_LINES is 25", () => {
        expect(MODE7_LINES).toBe(25);
    });

    it("Mode 7 in SCREEN_MODES matches constants", () => {
        const m7 = SCREEN_MODES[7]!;
        expect(m7.base).toBe(MODE7_BASE);
        expect(m7.bytesPerLine).toBe(MODE7_BYTES_PER_LINE);
        expect(m7.lines).toBe(MODE7_LINES);
        expect(m7.charsPerLine).toBe(40);
    });
});

describe("SCREEN_MODES details", () => {
    it("Mode 0 is 80 chars wide", () => {
        expect(SCREEN_MODES[0]!.charsPerLine).toBe(80);
    });

    it("Mode 3 is text mode with 25 lines", () => {
        expect(SCREEN_MODES[3]!.lines).toBe(25);
    });

    it("Mode 4 base is 0x5800", () => {
        expect(SCREEN_MODES[4]!.base).toBe(0x5800);
    });
});

describe("readMode7Screen", () => {
    it("reads screen memory and converts printable ASCII", async () => {
        // Mock readFn that returns "HELLO" on the first row, spaces elsewhere
        const mockReadFn: ReadFn = async (address: number, length: number): Promise<Buffer> => {
            const buf = Buffer.alloc(length, 0x20); // fill with spaces
            // Write "HELLO" at offset 0 (first row)
            if (address === MODE7_BASE) {
                buf[0] = 0x48; // H
                buf[1] = 0x45; // E
                buf[2] = 0x4C; // L
                buf[3] = 0x4C; // L
                buf[4] = 0x4F; // O
            }
            return buf;
        };

        const screen = await readMode7Screen(mockReadFn);
        const lines = screen.split("\n");
        expect(lines.length).toBe(25);
        expect(lines[0]).toBe("HELLO");
        // Remaining lines should be empty (spaces stripped)
        expect(lines[1]).toBe("");
    });

    it("replaces control codes with spaces", async () => {
        const mockReadFn: ReadFn = async (_address: number, length: number): Promise<Buffer> => {
            const buf = Buffer.alloc(length, 0x20);
            // Place control codes and printable chars
            buf[0] = 0x01; // control code
            buf[1] = 0x41; // 'A'
            buf[2] = 0x7F; // DEL (non-printable)
            buf[3] = 0x42; // 'B'
            return buf;
        };

        const screen = await readMode7Screen(mockReadFn);
        const firstLine = screen.split("\n")[0]!;
        // 0x01 -> space, 'A', 0x7F -> space, 'B'
        expect(firstLine).toBe(" A B");
    });

    it("supports startRow and endRow parameters", async () => {
        const mockReadFn: ReadFn = async (address: number, length: number): Promise<Buffer> => {
            const buf = Buffer.alloc(length, 0x20);
            // Write "ROW5" at the start of the returned data
            buf[0] = 0x52; // R
            buf[1] = 0x4F; // O
            buf[2] = 0x57; // W
            buf[3] = 0x35; // 5
            return buf;
        };

        const screen = await readMode7Screen(mockReadFn, 5, 6);
        const lines = screen.split("\n");
        expect(lines.length).toBe(1);
        expect(lines[0]).toBe("ROW5");
    });

    it("clamps startRow and endRow to valid range", async () => {
        const mockReadFn: ReadFn = async (_address: number, length: number): Promise<Buffer> => {
            return Buffer.alloc(length, 0x20);
        };

        // Negative startRow should be clamped to 0, endRow > 25 clamped to 25
        const screen = await readMode7Screen(mockReadFn, -5, 100);
        const lines = screen.split("\n");
        expect(lines.length).toBe(25);
    });
});

describe("screenContains", () => {
    it("finds text in screen memory (case sensitive)", async () => {
        const mockReadFn: ReadFn = async (_address: number, length: number): Promise<Buffer> => {
            const buf = Buffer.alloc(length, 0x20);
            // Write "BBC MICRO" on first row
            const text = Buffer.from("BBC MICRO", "ascii");
            text.copy(buf);
            return buf;
        };

        expect(await screenContains(mockReadFn, "BBC")).toBe(true);
        expect(await screenContains(mockReadFn, "bbc")).toBe(false);
    });

    it("finds text case-insensitively", async () => {
        const mockReadFn: ReadFn = async (_address: number, length: number): Promise<Buffer> => {
            const buf = Buffer.alloc(length, 0x20);
            const text = Buffer.from("BBC MICRO", "ascii");
            text.copy(buf);
            return buf;
        };

        expect(await screenContains(mockReadFn, "bbc micro", false)).toBe(true);
    });

    it("returns false when text is not present", async () => {
        const mockReadFn: ReadFn = async (_address: number, length: number): Promise<Buffer> => {
            return Buffer.alloc(length, 0x20);
        };

        expect(await screenContains(mockReadFn, "HELLO")).toBe(false);
    });
});
