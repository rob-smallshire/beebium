/**
 * Break, Ctrl-Break and Shift-Break hold in emulated time (#207).
 *
 * The MOS reads its reset options, and DFS the Shift key, in emulated time
 * after Break is released, so the holds are emulated time too: the same press
 * at any speed. A host-time hold is too short on a machine running slower
 * than real time -- at 0.002x a 10 ms settle is 40 cycles, and Ctrl-Break
 * degrades to a plain Break. These check what the guest saw: the MOS's
 * last-Break type at &028D (0 soft, 2 hard), and !BOOT having run.
 *
 * Requires BEEBIUM_SERVER and BEEBIUM_ROM_DIR, as the other integration tests.
 */

import { describe, it, expect, afterEach } from "vitest";
import { join } from "node:path";
import { Beebium } from "../src/client.js";
import { DebuggerError } from "../src/exceptions.js";

const LAST_BREAK_TYPE = 0x028d;
// Boot option *RUN: !BOOT is JMP &2000, so a booted machine sits at &2000.
const BOOT_DISC_FILEPATH = join(__dirname, "..", "..", "..", "tests", "assets", "discs", "never_reads_keyboard.ssd");
const BOOTED_LOOP = 0x2000;

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

async function launchAtSpeedZero(): Promise<Beebium> {
    const bbc = await Beebium.launch({ model: "B", args: ["--preset", "model-b-disc"], timeoutMs: 20000 });
    activeHosts.add(bbc);
    await bbc.system.setSpeedMultiplier(0.0);
    await bbc.waitUntilReady();
    return bbc;
}

async function lastBreakType(bbc: Beebium): Promise<number> {
    await bbc.waitUntilReady();
    return (await bbc.memory.address.peek.read(LAST_BREAK_TYPE, 1))[0]!;
}

describe("Break keys in emulated time", () => {
    it("ctrlBreak is seen as Ctrl-Break at speed 0", async () => {
        const bbc = await launchAtSpeedZero();
        await bbc.keyboard.ctrlBreak();
        expect(await lastBreakType(bbc)).toBe(2);
    }, 30000);

    it("ctrlBreak is seen as Ctrl-Break on a machine slower than real time", async () => {
        // At 0.002x a host-time settle of 10 ms is 40 emulated cycles: CTRL
        // would be gone before the MOS read its reset options. An
        // emulated-time hold is the same length at any speed.
        const bbc = await launchAtSpeedZero();
        await bbc.system.setSpeedMultiplier(0.002);
        await bbc.keyboard.ctrlBreak(1);
        await bbc.system.setSpeedMultiplier(0.0);
        expect(await lastBreakType(bbc)).toBe(2);
    }, 60000);

    it("pressBreak is a soft Break", async () => {
        const bbc = await launchAtSpeedZero();
        await bbc.keyboard.ctrlBreak();
        await bbc.keyboard.pressBreak();
        expect(await lastBreakType(bbc)).toBe(0);
    }, 30000);

    it("shiftBreak runs !BOOT at speed 0", async () => {
        const bbc = await launchAtSpeedZero();
        await bbc.disc.drive(0).insert(BOOT_DISC_FILEPATH);
        await bbc.keyboard.shiftBreak();
        await bbc.runForEmulatedSeconds(3);  // the drive spins up and !BOOT loads
        expect((await bbc.cpu.getRegisters()).pc).toBe(BOOTED_LOOP);
    }, 30000);

    it("pressBreak with the same disc does not boot it", async () => {
        const bbc = await launchAtSpeedZero();
        await bbc.disc.drive(0).insert(BOOT_DISC_FILEPATH);
        await bbc.keyboard.pressBreak();
        expect(await lastBreakType(bbc)).toBe(0);  // the guest became ready: BASIC, not !BOOT
    }, 30000);

    it("refuses on a stopped machine before pressing anything", async () => {
        const bbc = await launchAtSpeedZero();
        await bbc.debugger.stop();
        await expect(bbc.keyboard.ctrlBreak()).rejects.toThrow(DebuggerError);
        await expect(bbc.keyboard.pressBreak()).rejects.toThrow(/machine is stopped/);
        await expect(bbc.keyboard.shiftBreak()).rejects.toThrow(DebuggerError);
        expect(await bbc.keyboard.isBreakHeld()).toBe(false);
        expect((await bbc.keyboard.getState()).pressedRows.every((row) => row === 0)).toBe(true);
    }, 30000);
});
