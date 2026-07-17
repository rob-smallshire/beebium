import { describe, it, expect } from "vitest";
import {
    flashSelect,
    teletextMode,
    lineWidthMode,
    fastClock,
    cursorWidth,
    CTRL_FLASH,
    CTRL_TELETEXT,
    CTRL_LINE_WIDTH,
    CTRL_FAST_CLOCK,
    CTRL_CURSOR_WIDTH,
} from "../src/video-ula.js";

describe("control register constants", () => {
    it("CTRL_FLASH is 0x01", () => {
        expect(CTRL_FLASH).toBe(0x01);
    });

    it("CTRL_TELETEXT is 0x02", () => {
        expect(CTRL_TELETEXT).toBe(0x02);
    });

    it("CTRL_LINE_WIDTH is 0x0C", () => {
        expect(CTRL_LINE_WIDTH).toBe(0x0C);
    });

    it("CTRL_FAST_CLOCK is 0x10", () => {
        expect(CTRL_FAST_CLOCK).toBe(0x10);
    });

    it("CTRL_CURSOR_WIDTH is 0xE0", () => {
        expect(CTRL_CURSOR_WIDTH).toBe(0xE0);
    });
});

describe("flashSelect (bit 0)", () => {
    it("returns true when set", () => {
        expect(flashSelect(0x01)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(flashSelect(0x00)).toBe(false);
    });

    it("returns true with other bits set", () => {
        expect(flashSelect(0xFF)).toBe(true);
    });

    it("returns false when only other bits are set", () => {
        expect(flashSelect(0xFE)).toBe(false);
    });
});

describe("teletextMode (bit 1)", () => {
    it("returns true when set", () => {
        expect(teletextMode(0x02)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(teletextMode(0x00)).toBe(false);
    });

    it("returns true for typical Mode 7 control value", () => {
        // Mode 7: teletext=1, line width=2 (40 chars), slow clock
        expect(teletextMode(0x0A)).toBe(true);
    });
});

describe("lineWidthMode (bits 2-3)", () => {
    it("returns 0 when bits 2-3 are 00", () => {
        expect(lineWidthMode(0x00)).toBe(0);
    });

    it("returns 1 when bits 2-3 are 01", () => {
        expect(lineWidthMode(0x04)).toBe(1);
    });

    it("returns 2 when bits 2-3 are 10", () => {
        expect(lineWidthMode(0x08)).toBe(2);
    });

    it("returns 3 when bits 2-3 are 11", () => {
        expect(lineWidthMode(0x0C)).toBe(3);
    });

    it("ignores bits outside 2-3", () => {
        expect(lineWidthMode(0xF3)).toBe(0); // bits 2-3 = 00
        expect(lineWidthMode(0xF7)).toBe(1); // bits 2-3 = 01
    });
});

describe("fastClock (bit 4)", () => {
    it("returns true when set", () => {
        expect(fastClock(0x10)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(fastClock(0x00)).toBe(false);
    });

    it("returns true for Mode 0 control value", () => {
        // Mode 0: bitmap, 80 chars, fast clock = 0x1C
        expect(fastClock(0x1C)).toBe(true);
    });
});

describe("cursorWidth (bits 5-7)", () => {
    it("returns 0 when bits 5-7 are 000", () => {
        expect(cursorWidth(0x00)).toBe(0);
    });

    it("returns 1 when bits 5-7 are 001", () => {
        expect(cursorWidth(0x20)).toBe(1);
    });

    it("returns 7 when bits 5-7 are 111", () => {
        expect(cursorWidth(0xE0)).toBe(7);
    });

    it("ignores lower bits", () => {
        expect(cursorWidth(0x1F)).toBe(0); // bits 5-7 = 000
        expect(cursorWidth(0x3F)).toBe(1); // bits 5-7 = 001
    });

    it("returns correct value for 0xFF", () => {
        expect(cursorWidth(0xFF)).toBe(7);
    });
});
