/**
 * What launch() leaves the machine doing (#198).
 *
 * launch() leaves the machine running, started by the client once it has
 * connected, so a launch means the same thing in emulated time on every host.
 * With startStopped the machine has executed no instruction: the reset sequence
 * has run and it waits at the reset vector, so breakpoints set before run() see
 * everything the guest does.
 *
 * Requires BEEBIUM_SERVER and BEEBIUM_ROM_DIR, as the other integration tests.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";

const RESET_SEQUENCE_CYCLES = 7;

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

async function launch(startStopped?: boolean): Promise<Beebium> {
    const bbc = await Beebium.launch({ model: "B", timeoutMs: 20000, startStopped });
    activeHosts.add(bbc);
    return bbc;
}

async function resetVector(bbc: Beebium): Promise<number> {
    const bytes = await bbc.memory.address.peek.read(0xfffc, 2);
    return bytes[0]! | (bytes[1]! << 8);
}

describe("launch run state", () => {
    it("leaves the machine running", async () => {
        const bbc = await launch();
        const start = await bbc.debugger.getState();
        expect(start.isRunning).toBe(true);
        // isRunning is the server's word; the cycle count shows the machine
        // really moves. Wait for it to move, since a loaded host may not
        // schedule the emulation thread within any fixed interval.
        const deadline = Date.now() + 10000;
        let cycleCount = start.cycleCount;
        while (cycleCount <= start.cycleCount && Date.now() < deadline) {
            await new Promise((r) => setTimeout(r, 10));
            cycleCount = (await bbc.debugger.getState()).cycleCount;
        }
        expect(cycleCount).toBeGreaterThan(start.cycleCount);
    }, 30000);

    it("with startStopped leaves it at the reset vector, no instruction executed", async () => {
        const bbc = await launch(true);
        const state = await bbc.debugger.getState();
        expect(state.isRunning).toBe(false);
        expect(state.cycleCount).toBe(RESET_SEQUENCE_CYCLES);
        expect((await bbc.cpu.getRegisters()).pc).toBe(await resetVector(bbc));
        await new Promise((r) => setTimeout(r, 200));
        expect((await bbc.debugger.getState()).cycleCount).toBe(RESET_SEQUENCE_CYCLES);
    }, 30000);

    it("with startStopped lets a breakpoint set before run() see the first instructions", async () => {
        const bbc = await launch(true);
        const secondInstruction = (await resetVector(bbc)) + 2;  // MOS 1.20 opens with LDA #&40
        await bbc.debugger.addBreakpoint(secondInstruction);
        await bbc.debugger.run();
        const deadline = Date.now() + 5000;
        while ((await bbc.debugger.getState()).isRunning && Date.now() < deadline) {
            await new Promise((r) => setTimeout(r, 10));
        }
        expect((await bbc.cpu.getRegisters()).pc).toBe(secondInstruction);
        expect((await bbc.debugger.getState()).cycleCount).toBe(RESET_SEQUENCE_CYCLES + 2);
    }, 30000);
});
