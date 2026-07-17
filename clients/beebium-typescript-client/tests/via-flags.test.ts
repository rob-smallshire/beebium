import { describe, it, expect } from "vitest";
import {
    paLatching,
    pbLatching,
    t2CountPb6,
    t1Continuous,
    t1OutputPb7,
    ifrCa2,
    ifrCa1,
    ifrSr,
    ifrCb2,
    ifrCb1,
    ifrT2,
    ifrT1,
    ifrIrq,
    formatViaState,
    type ViaState,
} from "../src/via.js";

// -- ACR flag helpers ---------------------------------------------------------

describe("paLatching (ACR bit 0)", () => {
    it("returns true when bit 0 is set", () => {
        expect(paLatching(0x01)).toBe(true);
    });

    it("returns false when bit 0 is clear", () => {
        expect(paLatching(0x00)).toBe(false);
    });

    it("returns true with other bits also set", () => {
        expect(paLatching(0xFF)).toBe(true);
    });

    it("returns false when only other bits are set", () => {
        expect(paLatching(0xFE)).toBe(false);
    });
});

describe("pbLatching (ACR bit 1)", () => {
    it("returns true when bit 1 is set", () => {
        expect(pbLatching(0x02)).toBe(true);
    });

    it("returns false when bit 1 is clear", () => {
        expect(pbLatching(0x00)).toBe(false);
    });
});

describe("t2CountPb6 (ACR bit 5)", () => {
    it("returns true when bit 5 is set", () => {
        expect(t2CountPb6(0x20)).toBe(true);
    });

    it("returns false when bit 5 is clear", () => {
        expect(t2CountPb6(0x00)).toBe(false);
    });
});

describe("t1Continuous (ACR bit 6)", () => {
    it("returns true when bit 6 is set", () => {
        expect(t1Continuous(0x40)).toBe(true);
    });

    it("returns false when bit 6 is clear", () => {
        expect(t1Continuous(0x00)).toBe(false);
    });
});

describe("t1OutputPb7 (ACR bit 7)", () => {
    it("returns true when bit 7 is set", () => {
        expect(t1OutputPb7(0x80)).toBe(true);
    });

    it("returns false when bit 7 is clear", () => {
        expect(t1OutputPb7(0x00)).toBe(false);
    });
});

// -- IFR flag helpers ---------------------------------------------------------

describe("ifrCa2 (IFR bit 0)", () => {
    it("returns true when set", () => {
        expect(ifrCa2(0x01)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrCa2(0x00)).toBe(false);
    });
});

describe("ifrCa1 (IFR bit 1)", () => {
    it("returns true when set", () => {
        expect(ifrCa1(0x02)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrCa1(0x00)).toBe(false);
    });
});

describe("ifrSr (IFR bit 2)", () => {
    it("returns true when set", () => {
        expect(ifrSr(0x04)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrSr(0x00)).toBe(false);
    });
});

describe("ifrCb2 (IFR bit 3)", () => {
    it("returns true when set", () => {
        expect(ifrCb2(0x08)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrCb2(0x00)).toBe(false);
    });
});

describe("ifrCb1 (IFR bit 4)", () => {
    it("returns true when set", () => {
        expect(ifrCb1(0x10)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrCb1(0x00)).toBe(false);
    });
});

describe("ifrT2 (IFR bit 5)", () => {
    it("returns true when set", () => {
        expect(ifrT2(0x20)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrT2(0x00)).toBe(false);
    });
});

describe("ifrT1 (IFR bit 6)", () => {
    it("returns true when set", () => {
        expect(ifrT1(0x40)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrT1(0x00)).toBe(false);
    });
});

describe("ifrIrq (IFR bit 7)", () => {
    it("returns true when set", () => {
        expect(ifrIrq(0x80)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(ifrIrq(0x00)).toBe(false);
    });
});

describe("combined IFR checks", () => {
    it("all flags true for 0xFF", () => {
        expect(ifrCa2(0xFF)).toBe(true);
        expect(ifrCa1(0xFF)).toBe(true);
        expect(ifrSr(0xFF)).toBe(true);
        expect(ifrCb2(0xFF)).toBe(true);
        expect(ifrCb1(0xFF)).toBe(true);
        expect(ifrT2(0xFF)).toBe(true);
        expect(ifrT1(0xFF)).toBe(true);
        expect(ifrIrq(0xFF)).toBe(true);
    });

    it("all flags false for 0x00", () => {
        expect(ifrCa2(0x00)).toBe(false);
        expect(ifrCa1(0x00)).toBe(false);
        expect(ifrSr(0x00)).toBe(false);
        expect(ifrCb2(0x00)).toBe(false);
        expect(ifrCb1(0x00)).toBe(false);
        expect(ifrT2(0x00)).toBe(false);
        expect(ifrT1(0x00)).toBe(false);
        expect(ifrIrq(0x00)).toBe(false);
    });
});

// -- formatViaState -----------------------------------------------------------

describe("formatViaState", () => {
    it("produces expected output", () => {
        const state: ViaState = {
            ora: 0x12, orb: 0x34,
            ddra: 0xFF, ddrb: 0x00,
            ira: 0x12, irb: 0x34,
            t1c: 0x1234, t1l: 0x5678,
            t2c: 0x9ABC, t2l: 0xDE,
            acr: 0x40, pcr: 0x00, sr: 0x00,
            ifr: 0x60, ier: 0x60,
            t1Pending: true, t2Pending: false, t1Pb7: 1,
            ca1: false, ca2: false, cb1: false, cb2: false,
        };
        const result = formatViaState(state);
        expect(result).toContain("ORA=12");
        expect(result).toContain("ORB=34");
        expect(result).toContain("DDRA=FF");
        expect(result).toContain("DDRB=00");
        expect(result).toContain("T1C=1234");
        expect(result).toContain("T1L=5678");
        expect(result).toContain("T2C=9ABC");
        expect(result).toContain("T2L=DE");
        expect(result).toContain("ACR=40");
        expect(result).toContain("PCR=00");
        expect(result).toContain("SR=00");
        expect(result).toContain("IFR=60");
        expect(result).toContain("IER=60");
    });
});
