/**
 * runFields() and the field a frame carries (#190).
 *
 * Requires BEEBIUM_SERVER and BEEBIUM_ROM_DIR, as the other integration tests.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

async function launch(): Promise<Beebium> {
    const bbc = await Beebium.launch({ model: "B", timeoutMs: 20000 });
    activeHosts.add(bbc);
    await bbc.system.setSpeedMultiplier(0.0);
    return bbc;
}

describe("runFields", () => {
    it("stops just after the nth field edge, leaving the machine stopped", async () => {
        const bbc = await launch();
        await bbc.debugger.stop();
        const start = (await bbc.crtc.getState()).field;
        const state = await bbc.runFields(3);
        const crtc = await bbc.crtc.getState();
        expect(crtc.field).toBe(start + 3);
        expect(crtc.cyclesSinceVsync).toBeLessThan(8);
        expect(state.isRunning).toBe(false);
        expect(await bbc.debugger.isRunning()).toBe(false);
    }, 30000);

    it("a stopped capture is the frame of the field just run, in a progressive mode", async () => {
        const bbc = await launch();
        await bbc.waitUntilReady();
        await bbc.keyboard.type("MODE 4\r");
        await bbc.keyboard.waitForTyping();
        await bbc.runFields(10);
        for (let i = 0; i < 3; ++i) {
            await bbc.runFields(1);
            const frame = await bbc.video.captureFrame(2000);
            expect(frame.field).toBe((await bbc.crtc.getState()).field);
        }
    }, 30000);

    it("refuses fewer than one field", async () => {
        const bbc = await launch();
        await expect(bbc.runFields(0)).rejects.toThrow(RangeError);
    }, 30000);
});
