import { describe, it, expect } from "vitest";
import {
    htotal,
    hdisplayed,
    hsyncPos,
    syncWidth,
    vtotal,
    vtotalAdj,
    vdisplayed,
    vsyncPos,
    interlaceMode,
    maxScanline,
    hsyncWidth,
    vsyncWidth,
    isInterlaced,
    type CrtcState,
} from "../src/crtc.js";

/** Helper to create a CrtcState with specified register values. */
function makeState(registers: number[]): CrtcState {
    // Pad to 18 registers
    const regs = [...registers];
    while (regs.length < 18) {
        regs.push(0);
    }
    return {
        registers: regs,
        addressRegister: 0,
        column: 0,
        row: 0,
        raster: 0,
        charAddr: 0,
        screenStart: 0,
        cursorPosition: 0,
        inHsync: false,
        inVsync: false,
        displayEnabled: true,
    };
}

describe("register accessors", () => {
    it("htotal reads R0", () => {
        const state = makeState([127]);
        expect(htotal(state)).toBe(127);
    });

    it("hdisplayed reads R1", () => {
        const state = makeState([0, 80]);
        expect(hdisplayed(state)).toBe(80);
    });

    it("hsyncPos reads R2", () => {
        const state = makeState([0, 0, 98]);
        expect(hsyncPos(state)).toBe(98);
    });

    it("syncWidth reads R3", () => {
        const state = makeState([0, 0, 0, 0x28]);
        expect(syncWidth(state)).toBe(0x28);
    });

    it("vtotal reads R4", () => {
        const state = makeState([0, 0, 0, 0, 38]);
        expect(vtotal(state)).toBe(38);
    });

    it("vtotalAdj reads R5", () => {
        const state = makeState([0, 0, 0, 0, 0, 0]);
        expect(vtotalAdj(state)).toBe(0);
    });

    it("vdisplayed reads R6", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 32]);
        expect(vdisplayed(state)).toBe(32);
    });

    it("vsyncPos reads R7", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 34]);
        expect(vsyncPos(state)).toBe(34);
    });

    it("interlaceMode reads R8", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 3]);
        expect(interlaceMode(state)).toBe(3);
    });

    it("maxScanline reads R9 masked to 5 bits", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0, 7]);
        expect(maxScanline(state)).toBe(7);

        // Bits above 4 are masked off
        const state2 = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF]);
        expect(maxScanline(state2)).toBe(0x1F);
    });
});

describe("hsyncWidth", () => {
    it("extracts low nibble of R3", () => {
        const state = makeState([0, 0, 0, 0x28]); // low nibble = 8
        expect(hsyncWidth(state)).toBe(8);
    });

    it("ignores high nibble", () => {
        const state = makeState([0, 0, 0, 0xF4]); // low nibble = 4
        expect(hsyncWidth(state)).toBe(4);
    });

    it("returns 0 when R3 low nibble is 0", () => {
        const state = makeState([0, 0, 0, 0x20]); // low nibble = 0
        expect(hsyncWidth(state)).toBe(0);
    });
});

describe("vsyncWidth", () => {
    it("extracts high nibble of R3", () => {
        const state = makeState([0, 0, 0, 0x28]); // high nibble = 2
        expect(vsyncWidth(state)).toBe(2);
    });

    it("returns 16 when high nibble is 0", () => {
        const state = makeState([0, 0, 0, 0x08]); // high nibble = 0
        expect(vsyncWidth(state)).toBe(16);
    });

    it("returns 15 for high nibble 0xF", () => {
        const state = makeState([0, 0, 0, 0xF8]); // high nibble = F
        expect(vsyncWidth(state)).toBe(15);
    });
});

describe("isInterlaced", () => {
    it("returns true when R8 bits 0-1 are both set (0x03)", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0x03]);
        expect(isInterlaced(state)).toBe(true);
    });

    it("returns false when R8 is 0", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0x00]);
        expect(isInterlaced(state)).toBe(false);
    });

    it("returns false when only bit 0 is set", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0x01]);
        expect(isInterlaced(state)).toBe(false);
    });

    it("returns false when only bit 1 is set", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0x02]);
        expect(isInterlaced(state)).toBe(false);
    });

    it("returns true when bits 0-1 set plus other bits", () => {
        const state = makeState([0, 0, 0, 0, 0, 0, 0, 0, 0xFF]);
        expect(isInterlaced(state)).toBe(true);
    });
});
