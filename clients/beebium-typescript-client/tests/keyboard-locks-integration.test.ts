/**
 * Integration tests for typing under CAPS LOCK and SHIFT LOCK.
 *
 * Reproduces and exercises the fix for GitHub issue #5: text typed via
 * the TypeScript API must take account of CAPS LOCK and SHIFT LOCK,
 * otherwise `keyboard.type("hello")` produces "HELLO" on screen because
 * the BBC Micro defaults to CAPS LOCK on after boot.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";
import { screenContains, type ReadFn } from "../src/screen.js";

const HOST_CLOCK_HZ = 2_000_000;

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

async function launchHost(): Promise<Beebium> {
    const host = await Beebium.launch({ model: "B", timeoutMs: 20000 });
    activeHosts.add(host);
    return host;
}

function readFn(bbc: Beebium): ReadFn {
    return async (addr: number, len: number) =>
        bbc.memory.address.peek.read(addr, len);
}

async function runForEmulatedSeconds(bbc: Beebium, seconds: number): Promise<void> {
    const targetCycles = Math.floor(seconds * HOST_CLOCK_HZ);
    const startCycles = (await bbc.debugger.getState()).cycleCount;
    const wasRunning = await bbc.debugger.isRunning();
    if (!wasRunning) {
        await bbc.debugger.run();
    }
    while ((await bbc.debugger.getState()).cycleCount - startCycles < targetCycles) {
        await new Promise(r => setTimeout(r, 50));
    }
    if (!wasRunning) {
        await bbc.debugger.stop();
    }
}

async function bootToBasicStopped(bbc: Beebium): Promise<void> {
    await runForEmulatedSeconds(bbc, 2.0);
    if (await bbc.debugger.isRunning()) {
        await bbc.debugger.stop();
    }
}

async function bootAndRun(bbc: Beebium): Promise<void> {
    await bootToBasicStopped(bbc);
    await bbc.debugger.run();
}

describe("Integration: Keyboard lock keys (issue #5)", () => {
    it("BBC Micro boots with CAPS LOCK on, SHIFT LOCK off", async () => {
        const bbc = await launchHost();
        await bootToBasicStopped(bbc);
        const state = await bbc.keyboard.getLockState();
        expect(state.capsLock).toBe(true);
        expect(state.shiftLock).toBe(false);
    });

    it("type('abc') with CAPS LOCK on echoes as 'ABC' (current behaviour)", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        expect((await bbc.keyboard.getLockState()).capsLock).toBe(true);

        await bbc.keyboard.type("abc");
        await runForEmulatedSeconds(bbc, 1.0);

        const found = await screenContains(readFn(bbc), "ABC");
        const lower = await screenContains(readFn(bbc), "abc");
        expect(found).toBe(true);
        expect(lower).toBe(false);
    });

    it("withTextInput disables CAPS LOCK so type('abc') echoes as 'abc'", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);

        await bbc.keyboard.withTextInput(async () => {
            expect((await bbc.keyboard.getLockState()).capsLock).toBe(false);

            await bbc.keyboard.type("abc");
            await runForEmulatedSeconds(bbc, 1.0);

            const lower = await screenContains(readFn(bbc), "abc");
            const upper = await screenContains(readFn(bbc), "ABC");
            expect(lower).toBe(true);
            expect(upper).toBe(false);
        });
    });

    it("withTextInput restores CAPS LOCK to its prior state on exit", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        const before = (await bbc.keyboard.getLockState()).capsLock;
        expect(before).toBe(true);

        await bbc.keyboard.withTextInput(async () => {
            // No-op body
        });

        expect((await bbc.keyboard.getLockState()).capsLock).toBe(before);
    });

    it("tapCapsLock flips the logical caps-lock latch", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        const before = (await bbc.keyboard.getLockState()).capsLock;

        await bbc.keyboard.tapCapsLock();

        expect((await bbc.keyboard.getLockState()).capsLock).not.toBe(before);
    });

    it("withTextInput throws if the emulator is not running", async () => {
        const bbc = await launchHost();
        await bootToBasicStopped(bbc);

        await expect(
            bbc.keyboard.withTextInput(async () => { /* no-op */ }),
        ).rejects.toThrow(/running emulator/);
    });

    it("tapCapsLock throws if the emulator is not running", async () => {
        const bbc = await launchHost();
        await bootToBasicStopped(bbc);

        await expect(bbc.keyboard.tapCapsLock()).rejects.toThrow(/running emulator/);
    });
});

describe("Integration: Keyboard lock keys at unlimited speed", () => {
    // At unlimited speed the lock-LED brightness no longer settles to 0/255
    // within a cycle-budgeted poll; these exercise the logical-latch path that
    // replaced the brightness inference, with the emulator running flat out.

    it("reads the logical lock state exactly at unlimited speed", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        // Pacing clock is wired once the emulator is running; switch to
        // unlimited speed for the lock operations under test.
        await bbc.system.setSpeedMultiplier(0.0);
        const state = await bbc.keyboard.getLockState();
        expect(state.capsLock).toBe(true);
        expect(state.shiftLock).toBe(false);
    });

    it("lock-key case control works at unlimited speed", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        // Pacing clock is wired once the emulator is running; switch to
        // unlimited speed for the lock operations under test.
        await bbc.system.setSpeedMultiplier(0.0);
        expect((await bbc.keyboard.getLockState()).capsLock).toBe(true);

        await bbc.keyboard.withTextInput(async () => {
            expect((await bbc.keyboard.getLockState()).capsLock).toBe(false);
            await bbc.keyboard.type("hello");
            await runForEmulatedSeconds(bbc, 1.0);
            expect(await screenContains(readFn(bbc), "hello")).toBe(true);
            expect(await screenContains(readFn(bbc), "HELLO")).toBe(false);
        });

        expect((await bbc.keyboard.getLockState()).capsLock).toBe(true);
    });

    it("SHIFT-modifier case control works at unlimited speed", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        // Pacing clock is wired once the emulator is running; switch to
        // unlimited speed for the lock operations under test.
        await bbc.system.setSpeedMultiplier(0.0);

        await bbc.keyboard.withTextInput(async () => {
            await bbc.keyboard.type("MixedCase");
            await runForEmulatedSeconds(bbc, 1.0);
            expect(await screenContains(readFn(bbc), "MixedCase")).toBe(true);
        });
    });

    it("tapCapsLock flips the latch at unlimited speed", async () => {
        const bbc = await launchHost();
        await bootAndRun(bbc);
        // Pacing clock is wired once the emulator is running; switch to
        // unlimited speed for the lock operations under test.
        await bbc.system.setSpeedMultiplier(0.0);
        const before = (await bbc.keyboard.getLockState()).capsLock;

        await bbc.keyboard.tapCapsLock();

        expect((await bbc.keyboard.getLockState()).capsLock).not.toBe(before);
    });
});
