/**
 * The server reports when typing finishes; callers must not have to infer it.
 *
 * Polling and inferring completion needs a timeout, and no timeout is right for
 * every paste: throughput depends on how fast the host can emulate and on the
 * text itself, since every capital costs an extra SHIFT press.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";
import { DebuggerError } from "../src/exceptions.js";

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

describe("watchTypingStatus", () => {
    it("pushes an initial snapshot without waiting for a change", async () => {
        const bbc = await launchHost();
        await bbc.keyboard.clearTyping();

        for await (const status of bbc.keyboard.watchTypingStatus()) {
            expect(status.idle).toBe(true);
            expect(status.pendingCharacters).toBe(0);
            break;
        }
    });

    it("reports the pending count falling to zero", async () => {
        const bbc = await launchHost();
        await bbc.keyboard.clearTyping();
        await bbc.keyboard.type("PRINT 1\r".repeat(3));

        const seen: number[] = [];
        for await (const status of bbc.keyboard.watchTypingStatus()) {
            seen.push(status.pendingCharacters);
            if (status.idle) break;
        }

        expect(seen[0]).toBeGreaterThan(0);
        expect(seen[seen.length - 1]).toBe(0);
        expect(seen).toEqual([...seen].sort((a, b) => b - a));
    });
});

describe("waitForTyping", () => {
    it("returns when the queue has drained", async () => {
        const bbc = await launchHost();
        await bbc.keyboard.clearTyping();
        await bbc.keyboard.type("PRINT 2\r".repeat(4));

        await bbc.keyboard.waitForTyping();

        const status = await bbc.keyboard.typingStatus();
        expect(status.idle).toBe(true);
        expect(status.pendingCharacters).toBe(0);
    });

    it("returns immediately when nothing is queued", async () => {
        const bbc = await launchHost();
        await bbc.keyboard.clearTyping();
        await bbc.keyboard.waitForTyping();
    });

    it("returns when the queue is cleared while waiting", async () => {
        // Cancelling a paste must end the wait, not strand the caller.
        const bbc = await launchHost();
        await bbc.keyboard.clearTyping();
        await bbc.keyboard.type("PRINT 3\r".repeat(200));

        let clearedCount = 0;
        const cancelled = new Promise<void>((resolve) => {
            setTimeout(() => {
                void bbc.keyboard.clearTyping().then((n) => {
                    clearedCount = n;
                    resolve();
                });
            }, 1000);
        });

        await bbc.keyboard.waitForTyping();
        await cancelled;

        expect(clearedCount).toBeGreaterThan(0);
        expect((await bbc.keyboard.typingStatus()).idle).toBe(true);
    });

    it("rejects at once when the machine is stopped with characters pending", async () => {
        const bbc = await launchHost();
        await bbc.keyboard.clearTyping();
        await bbc.debugger.stop();
        await bbc.keyboard.type("PRINT 4\r");

        const started = Date.now();
        await expect(bbc.keyboard.waitForTyping()).rejects.toThrow(DebuggerError);
        await expect(bbc.keyboard.waitForTyping()).rejects.toThrow(
            /machine is stopped.*debugger\.run\(\)/,
        );
        expect(Date.now() - started).toBeLessThan(5000);
        expect((await bbc.keyboard.typingStatus()).pendingCharacters).toBeGreaterThan(0);
    });
});

describe("guest readiness", () => {
    it("is not ready after a reset and ready once the guest asks for input", async () => {
        const bbc = await launchHost();
        await bbc.debugger.reset();
        expect((await bbc.keyboard.typingStatus()).guestReady).toBe(false);
        await expect(bbc.waitUntilReady()).rejects.toThrow(DebuggerError);

        await bbc.debugger.run();
        await bbc.waitUntilReady();
        expect((await bbc.keyboard.typingStatus()).guestReady).toBe(true);
    });
});

describe("readyWithin", () => {
    it("rejects a wait whose guest is not ready within the budget", async () => {
        const bbc = await launchHost();
        await bbc.waitUntilReady();
        await bbc.system.setSpeedMultiplier(0.0);
        await bbc.keyboard.breakDown();  // running, but the guest is held in reset
        await bbc.keyboard.type("PRINT 1\r");

        await expect(bbc.keyboard.waitForTyping({ readyWithin: 1.0 })).rejects.toThrow(
            /guest has not read the keyboard since reset \(1 emulated s\); type-ahead is held with 8 characters pending/,
        );
        await expect(bbc.waitUntilReady({ readyWithin: 1.0 })).rejects.toThrow(DebuggerError);
        const status = await bbc.keyboard.typingStatus();
        expect(status.guestReady).toBe(false);
        expect(status.cyclesSinceReset).toBeGreaterThan(2_000_000);

        await bbc.keyboard.breakUp();
        await bbc.keyboard.waitForTyping({ readyWithin: 30.0 });
        expect((await bbc.keyboard.typingStatus()).guestReady).toBe(true);
    });
});
