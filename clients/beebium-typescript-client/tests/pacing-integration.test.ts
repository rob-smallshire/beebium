/**
 * Integration tests for the pacing / speed-headroom stats.
 *
 * Exercises the throughput sampling a UI speed slider polls via getPacingStats:
 * the configured multiplier, the achieved multiplier, and the estimated maximum
 * attainable multiplier.
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

async function launchHost(): Promise<Beebium> {
    const host = await Beebium.launch({ model: "B", timeoutMs: 20000 });
    activeHosts.add(host);
    return host;
}

// A little over one pacing window (~5s wall) so at least one sample is published.
const ONE_WINDOW_MS = 6500;

describe("Integration: Pacing stats", () => {
    it("reports the configured speed multiplier", async () => {
        const bbc = await launchHost();
        // The configured multiplier is available as soon as the server is up,
        // independent of whether the emulation loop has wired the pacing clock.
        expect((await bbc.system.getPacingStats()).speedMultiplier).toBe(1.0);

        await bbc.system.setSpeedMultiplier(2.0);
        expect((await bbc.system.getPacingStats()).speedMultiplier).toBe(2.0);
    });

    it("publishes a headroom estimate after a pacing window", async () => {
        const bbc = await launchHost();
        if (!(await bbc.debugger.isRunning())) {
            await bbc.debugger.run();
        }
        await new Promise(r => setTimeout(r, ONE_WINDOW_MS));

        const stats = await bbc.system.getPacingStats();
        // Real-time pacing of a 2 MHz machine: achieved is ~1x with ample
        // headroom, so the estimate is at least the achieved rate.
        expect(stats.achievedSpeedMultiplier).toBeGreaterThan(0);
        expect(stats.estimatedMaxSpeedMultiplier).toBeGreaterThanOrEqual(
            stats.achievedSpeedMultiplier,
        );
    });
});
