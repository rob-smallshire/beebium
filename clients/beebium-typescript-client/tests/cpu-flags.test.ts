import { describe, it, expect } from "vitest";
import {
    carry,
    zero,
    interruptDisable,
    decimal,
    breakFlag,
    overflow,
    negative,
    formatRegisters,
    type Registers,
} from "../src/cpu.js";

describe("carry (bit 0)", () => {
    it("returns true when carry bit is set", () => {
        expect(carry(0x01)).toBe(true);
    });

    it("returns false when carry bit is clear", () => {
        expect(carry(0x00)).toBe(false);
    });

    it("returns true when all bits set", () => {
        expect(carry(0xFF)).toBe(true);
    });

    it("returns false when all bits except carry are set", () => {
        expect(carry(0xFE)).toBe(false);
    });
});

describe("zero (bit 1)", () => {
    it("returns true when zero bit is set", () => {
        expect(zero(0x02)).toBe(true);
    });

    it("returns false when zero bit is clear", () => {
        expect(zero(0x00)).toBe(false);
    });

    it("returns true with combined flags", () => {
        expect(zero(0x03)).toBe(true); // carry + zero
    });
});

describe("interruptDisable (bit 2)", () => {
    it("returns true when set", () => {
        expect(interruptDisable(0x04)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(interruptDisable(0x00)).toBe(false);
    });
});

describe("decimal (bit 3)", () => {
    it("returns true when set", () => {
        expect(decimal(0x08)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(decimal(0x00)).toBe(false);
    });
});

describe("breakFlag (bit 4)", () => {
    it("returns true when set", () => {
        expect(breakFlag(0x10)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(breakFlag(0x00)).toBe(false);
    });
});

describe("overflow (bit 6)", () => {
    it("returns true when set", () => {
        expect(overflow(0x40)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(overflow(0x00)).toBe(false);
    });
});

describe("negative (bit 7)", () => {
    it("returns true when set", () => {
        expect(negative(0x80)).toBe(true);
    });

    it("returns false when clear", () => {
        expect(negative(0x00)).toBe(false);
    });
});

describe("combined flag checks", () => {
    it("carry is true and zero is false for 0x01", () => {
        expect(carry(0x01)).toBe(true);
        expect(zero(0x01)).toBe(false);
    });

    it("all flags true for 0xFF", () => {
        expect(carry(0xFF)).toBe(true);
        expect(zero(0xFF)).toBe(true);
        expect(interruptDisable(0xFF)).toBe(true);
        expect(decimal(0xFF)).toBe(true);
        expect(breakFlag(0xFF)).toBe(true);
        expect(overflow(0xFF)).toBe(true);
        expect(negative(0xFF)).toBe(true);
    });

    it("all flags false for 0x00", () => {
        expect(carry(0x00)).toBe(false);
        expect(zero(0x00)).toBe(false);
        expect(interruptDisable(0x00)).toBe(false);
        expect(decimal(0x00)).toBe(false);
        expect(breakFlag(0x00)).toBe(false);
        expect(overflow(0x00)).toBe(false);
        expect(negative(0x00)).toBe(false);
    });
});

describe("formatRegisters", () => {
    it("formats all-zero registers correctly", () => {
        const regs: Registers = {
            a: 0, x: 0, y: 0, sp: 0, pc: 0, p: 0,
            inNmiHandler: false, inIrqHandler: false,
            nmiPending: false, irqPending: false,
            deviceIrqFlags: 0, deviceNmiFlags: 0,
        };
        const result = formatRegisters(regs);
        expect(result).toBe("A=00 X=00 Y=00 SP=00 PC=0000 P=00 [nv-bdizc]");
    });

    it("formats non-zero registers with hex values", () => {
        const regs: Registers = {
            a: 0x42, x: 0xFF, y: 0x10, sp: 0xFD, pc: 0xC000, p: 0x30,
            inNmiHandler: false, inIrqHandler: false,
            nmiPending: false, irqPending: false,
            deviceIrqFlags: 0, deviceNmiFlags: 0,
        };
        const result = formatRegisters(regs);
        // p=0x30 = break + unused => Bb set, rest clear
        expect(result).toBe("A=42 X=FF Y=10 SP=FD PC=C000 P=30 [nv-Bdizc]");
    });

    it("shows all flags set correctly for 0xFF", () => {
        const regs: Registers = {
            a: 0, x: 0, y: 0, sp: 0, pc: 0, p: 0xFF,
            inNmiHandler: false, inIrqHandler: false,
            nmiPending: false, irqPending: false,
            deviceIrqFlags: 0, deviceNmiFlags: 0,
        };
        const result = formatRegisters(regs);
        // p=0xFF means ALL bits set, so all flags uppercase
        expect(result).toContain("[NV-BDIZC]");
    });

    it("shows NZC flags for p=0x83", () => {
        const regs: Registers = {
            a: 0, x: 0, y: 0, sp: 0, pc: 0, p: 0x83,
            inNmiHandler: false, inIrqHandler: false,
            nmiPending: false, irqPending: false,
            deviceIrqFlags: 0, deviceNmiFlags: 0,
        };
        const result = formatRegisters(regs);
        // 0x83 = 1000_0011 = N, Z, C
        expect(result).toContain("[Nv-bdiZC]");
    });
});
