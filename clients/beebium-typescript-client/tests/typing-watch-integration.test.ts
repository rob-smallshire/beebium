/**
 * The server reports when typing finishes; callers must not have to infer it.
 *
 * Polling and inferring completion needs a timeout, and no timeout is right for
 * every paste: throughput depends on how fast the host can emulate and on the
 * text itself, since every capital costs an extra SHIFT press.
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
    // The queue only drains while the machine is executing.
    await host.debugger.run();
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
});
