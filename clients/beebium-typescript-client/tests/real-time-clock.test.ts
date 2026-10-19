import { describe, it, expect, vi } from "vitest";
import { RealTimeClock } from "../src/real-time-clock.js";

function stubReturning(response: object) {
    return {
        getRealTimeClockState: vi.fn((_request: unknown, callback: Function) => callback(null, response)),
    };
}

describe("RealTimeClock", () => {
    it("maps the registers and exposes the CMOS RAM from register 14", async () => {
        const registers = Buffer.alloc(64);
        registers[0x0e + 0x0f] = 0x2c;
        const clock = new RealTimeClock(stubReturning({
            present: true, chip: "CDP6818", registers, addressRegister: 0x1d,
        }) as any);
        const state = await clock.getState();
        expect(state.present).toBe(true);
        expect(state.chip).toBe("CDP6818");
        expect(state.registers.length).toBe(64);
        expect(state.cmosRam.length).toBe(50);
        expect(state.cmosRam[0x0f]).toBe(0x2c);
        expect(state.addressRegister).toBe(0x1d);
    });

    it("reports a machine with no clock of its own", async () => {
        const clock = new RealTimeClock(stubReturning({
            present: false, chip: "", registers: Buffer.alloc(0), addressRegister: 0,
        }) as any);
        const state = await clock.getState();
        expect(state.present).toBe(false);
        expect(state.registers.length).toBe(0);
    });
});
