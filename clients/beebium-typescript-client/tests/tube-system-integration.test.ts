/**
 * Integration tests for TubeSystem.
 *
 * Launches a Tube-enabled server (host + coprocessor) and tests
 * coupled execution, predicate-based stopping, and timeout behaviour.
 *
 * Each test gets a fresh server instance.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";
import { TubeSystem } from "../src/tube-system.js";
import { screenContains, type ReadFn } from "../src/screen.js";
import { ServerProcess } from "../src/server-process.js";

const ROM_DIRPATH = process.env.BEEBIUM_ROM_DIR || "/Users/rjs/Code/beebium/roms";
const DFS_ROM_FILEPATH = `${ROM_DIRPATH}/acorn-dfs_2_26.rom`;

/** Track launched hosts for cleanup on timeout. */
const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

/** Launch a Tube-enabled Beebium and return the host client. Tracked for cleanup. */
async function launchTubeServer(): Promise<Beebium> {
    const host = await Beebium.launch({
        args: [
            "--tube-65c02",
            "--fdc", "acorn-1770",
            "--sideways", `slot=14:type=rom:image=${DFS_ROM_FILEPATH}`,
        ],
        timeoutMs: 20000,
    });
    activeHosts.add(host);
    return host;
}

/** ReadFn adapter for screenContains. */
function readFn(bbc: Beebium): ReadFn {
    return async (addr: number, len: number) =>
        bbc.memory.address.peek.read(addr, len);
}

// =========================================================================
// TubeSystem
// =========================================================================

describe("TubeSystem", () => {
    it("should create from host and connect to coprocessor", async () => {
        const host = await launchTubeServer();
        try {
            // Boot to Tube banner so coprocessor is connected
            const found = await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );
            expect(found).toBe(true);

            const coupled = await TubeSystem.fromHost(host);
            const coprocessor = coupled.getCoprocessor();
            expect(coprocessor).toBeDefined();

            const pState = await coprocessor.debugger.getState();
            expect(pState.cycleCount).toBeGreaterThan(0);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runUntil should resolve when predicate returns true", async () => {
        const host = await launchTubeServer();
        try {
            // Boot to Tube banner
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await TubeSystem.fromHost(host);

            // Run coupled until we see "BASIC" on screen (it should
            // already be there from the Tube banner screen).
            const result = await coupled.runUntil(
                () => screenContains(readFn(host), "BASIC"),
                5,
            );
            expect(result).toBe(true);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runUntil should return false on timeout", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await TubeSystem.fromHost(host);

            // Predicate that never matches
            const result = await coupled.runUntil(
                async () => false,
                0.5,
            );
            expect(result).toBe(false);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runUntil should leave both processors stopped", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await TubeSystem.fromHost(host);
            const coprocessor = coupled.getCoprocessor();

            await coupled.runUntil(async () => false, 0.5);

            expect(await host.debugger.isRunning()).toBe(false);
            expect(await coprocessor.debugger.isRunning()).toBe(false);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("runFor should advance cycles on both processors", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await TubeSystem.fromHost(host);
            const coprocessor = coupled.getCoprocessor();

            const hostCyclesBefore = (await host.debugger.getState()).cycleCount;
            const coprocessorCyclesBefore = (await coprocessor.debugger.getState()).cycleCount;

            await coupled.runFor(0.5);

            const hostCyclesAfter = (await host.debugger.getState()).cycleCount;
            const coprocessorCyclesAfter = (await coprocessor.debugger.getState()).cycleCount;

            // 0.5s at 2MHz host = ~1M cycles
            expect(hostCyclesAfter - hostCyclesBefore).toBeGreaterThan(500_000);
            // Coprocessor at 3MHz should advance even more
            expect(coprocessorCyclesAfter - coprocessorCyclesBefore).toBeGreaterThan(500_000);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    it("stop should halt both processors", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await TubeSystem.fromHost(host);
            const coprocessor = coupled.getCoprocessor();

            await coupled.run();

            // Both should be running
            expect(await host.debugger.isRunning()).toBe(true);
            expect(await coprocessor.debugger.isRunning()).toBe(true);

            await coupled.stop();

            // Both should be stopped
            expect(await host.debugger.isRunning()).toBe(false);
            expect(await coprocessor.debugger.isRunning()).toBe(false);

            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);

    // NOTE: there used to be a "coprocessor runUntil should stop at an address" test
    // here. It captured the coprocessor's current PC and asserted execution would
    // return to it -- but that PC is typically a one-shot boot/handshake address
    // the coprocessor never revisits, so the assertion was unreliable (it only ever
    // "passed by timing coincidence"). The behaviour it meant to check -- that a
    // coprocessor breakpoint fires while the host drives execution -- is the same bug
    // this branch fixes, and is covered robustly by `coprocessor cycle-budget
    // breakpoint should fire` below and by the C++ test_tube_inprocess unit test.

    it("coprocessor debugger.stop() should work when coprocessor is running", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );
            const coprocessor = await host.connectCoprocessor();

            // Coprocessor should be running freely after boot
            expect(await coprocessor.debugger.isRunning()).toBe(true);

            // Stop should work
            await coprocessor.debugger.stop();
            expect(await coprocessor.debugger.isRunning()).toBe(false);

            // Run should work
            await coprocessor.debugger.run();
            expect(await coprocessor.debugger.isRunning()).toBe(true);

            // Stop again should work
            await coprocessor.debugger.stop();
            expect(await coprocessor.debugger.isRunning()).toBe(false);

            await coprocessor.close();
        } finally {
            await host.close();
        }
    }, 15000);

    it("runUntil should throw on timeout when breakpoint is never hit", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coprocessor = await host.connectCoprocessor();

            // Set a breakpoint at an address the coprocessor will never reach
            // (address $0001 is zero page, never executed in the idle loop).
            // runUntil should timeout and throw.
            await expect(
                coprocessor.debugger.runUntil(0x0001, 2000), // 2s timeout
            ).rejects.toThrow(/Timed out/);

            // Coprocessor should be stopped after timeout
            expect(await coprocessor.debugger.isRunning()).toBe(false);

            await coprocessor.close();
        } finally {
            await host.close();
        }
    }, 15000);

    // Regression test for coprocessor breakpoints firing while the host drives
    // execution. This used to be skipped on CI: the coprocessor's breakpoint check
    // lived only in CoprocessorRunner::run(), which is never called -- the coprocessor
    // is ticked single-threaded from Machine::step() via tick_coprocessor() ->
    // tick() -> step(). Fixed by also checking breakpoints/watchpoints in step().
    it("coprocessor cycle-budget breakpoint should fire", async () => {
        const host = await launchTubeServer();
        try {
            await host.runUntilOrTimeout(
                () => screenContains(readFn(host), "Acorn TUBE"),
                10,
            );

            const coupled = await TubeSystem.fromHost(host);
            const coprocessor = coupled.getCoprocessor();

            // Test that a cycle-budget breakpoint on the coprocessor fires.
            await coupled.stop();
            const coprocessorCycles = (await coprocessor.debugger.getState()).cycleCount;
            const targetCycles = coprocessorCycles + 200_000;

            const bpId = await coprocessor.debugger.addBreakpoint(0x0000, {
                endAddress: 0x10000,
                condition: `cycles >= ${targetCycles}`,
            });

            // Start host too (coprocessor may need it for Tube I/O)
            try { await host.debugger.run(); } catch { /* already running */ }

            // Use the stream pattern to wait for the breakpoint to fire, then
            // read the authoritative state via getState(). Filter on sequence so
            // we only break on a stop that happened AFTER we resumed -- the
            // coprocessor was stopped by coupled.stop() above, so a stale
            // isRunning:false event is in flight; breaking on it would catch the
            // coprocessor still running toward the breakpoint (a race that surfaced
            // only on the slower macOS x86_64 runner). Same guard runUntil uses.
            const iter = coprocessor.debugger.watchExecutionState();
            const initial = await iter.next(); // consume initial state
            const runSequence = initial.value!.state.sequence;
            await coprocessor.debugger.run();
            for await (const event of iter) {
                if (!event.state.isRunning && event.state.sequence > runSequence) break;
            }

            // Read authoritative state after the stream confirms the stop.
            const finalState = await coprocessor.debugger.getState();
            expect(finalState.isRunning).toBe(false);
            expect(finalState.cycleCount).toBeGreaterThanOrEqual(BigInt(targetCycles));

            // Stop host too
            if (await host.debugger.isRunning()) await host.debugger.stop();

            await coprocessor.debugger.removeBreakpoint(bpId);
            await coupled.close();
        } finally {
            await host.close();
        }
    }, 30000);
});
