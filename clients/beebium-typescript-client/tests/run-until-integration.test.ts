/**
 * Integration tests for Beebium.runUntilOrTimeout().
 *
 * Verifies the chunk-based execution with cycle-budget breakpoints,
 * predicate evaluation between chunks, and timeout behaviour.
 *
 * Each test launches a fresh beebium-model-b server.
 */

import { describe, it, expect } from "vitest";
import { Beebium } from "../src/client.js";
import { Connection } from "../src/connection.js";
import { ServerProcess } from "../src/server-process.js";
import { withServer } from "./server-harness.js";

const isCI = !!process.env["CI"];

/**
 * Launch a Beebium client wrapping a fresh server.
 * The server is tracked by withServer for cleanup.
 */
async function withBeebium(
    body: (bbc: Beebium) => Promise<void>,
): Promise<void> {
    await withServer(async (conn, server) => {
        const bbc = new Beebium(conn, server, server.provenanceUuid);
        await body(bbc);
    });
}

/**
 * Plant code at `org`, set PC there, disable interrupts.
 * Returns with the machine stopped, ready for runUntilOrTimeout.
 */
async function plantCode(bbc: Beebium, code: number[], org = 0x0400): Promise<void> {
    await bbc.debugger.reset();
    // SEI prefix to disable interrupts, then user code
    const fullCode = [0x78, ...code];
    await bbc.memory.address.bus.write(org, Buffer.from(fullCode));
    await bbc.cpu.setPc(org);
    // Step past SEI
    await bbc.debugger.step(1);
}

// =========================================================================
// runUntilOrTimeout
// =========================================================================

describe("Beebium: runUntilOrTimeout", () => {
    it("should resolve when predicate returns true", async () => {
        await withBeebium(async (bbc) => {
            // LDA #$42; STA $0500; JMP $0401 (loop back to LDA)
            // Predicate checks for $42 at $0500.
            await plantCode(bbc, [0xA9, 0x42, 0x8D, 0x00, 0x05, 0x4C, 0x01, 0x04]);

            const result = await bbc.runUntilOrTimeout(
                async () => (await bbc.memory.address.peek.readByte(0x0500)) === 0x42,
                5,
            );

            expect(result).toBe(true);
            expect(await bbc.memory.address.peek.readByte(0x0500)).toBe(0x42);
        });
    }, 30000);

    it.skipIf(isCI)("should return false on timeout when predicate never true", async () => {
        await withBeebium(async (bbc) => {
            // NOP; JMP $0401 (infinite loop, $0500 stays at 0)
            await plantCode(bbc, [0xEA, 0x4C, 0x01, 0x04]);

            const result = await bbc.runUntilOrTimeout(
                async () => (await bbc.memory.address.peek.readByte(0x0500)) === 0x42,
                0.5, // Short timeout
            );

            expect(result).toBe(false);
        });
    }, 30000);

    it("should stop the machine after predicate is satisfied", async () => {
        await withBeebium(async (bbc) => {
            // LDA #$42; STA $0500; JMP $0401
            await plantCode(bbc, [0xA9, 0x42, 0x8D, 0x00, 0x05, 0x4C, 0x01, 0x04]);

            await bbc.runUntilOrTimeout(
                async () => (await bbc.memory.address.peek.readByte(0x0500)) === 0x42,
                5,
            );

            const state = await bbc.debugger.getState();
            expect(state.isRunning).toBe(false);
        });
    }, 30000);

    it("should stop the machine after timeout", async () => {
        await withBeebium(async (bbc) => {
            // NOP; JMP $0401 (infinite loop)
            await plantCode(bbc, [0xEA, 0x4C, 0x01, 0x04]);

            await bbc.runUntilOrTimeout(
                async () => false,
                0.5,
            );

            const state = await bbc.debugger.getState();
            expect(state.isRunning).toBe(false);
        });
    }, 30000);

    it("should advance cycles during execution", async () => {
        await withBeebium(async (bbc) => {
            // NOP; JMP $0401 (infinite loop)
            await plantCode(bbc, [0xEA, 0x4C, 0x01, 0x04]);

            const cyclesBefore = (await bbc.debugger.getState()).cycleCount;

            await bbc.runUntilOrTimeout(
                async () => false,
                0.5,
            );

            const cyclesAfter = (await bbc.debugger.getState()).cycleCount;
            // 0.5s at 2MHz = ~1,000,000 cycles
            expect(cyclesAfter - cyclesBefore).toBeGreaterThan(500_000);
        });
    }, 30000);

    it("should work from a stopped state", async () => {
        await withBeebium(async (bbc) => {
            // LDA #$42; STA $0500; JMP $0401
            await plantCode(bbc, [0xA9, 0x42, 0x8D, 0x00, 0x05, 0x4C, 0x01, 0x04]);

            // Explicitly stop to ensure we start from stopped
            if (await bbc.debugger.isRunning()) {
                await bbc.debugger.stop();
            }

            const state = await bbc.debugger.getState();
            expect(state.isRunning).toBe(false);

            // Should still work despite starting from stopped
            const result = await bbc.runUntilOrTimeout(
                async () => (await bbc.memory.address.peek.readByte(0x0500)) === 0x42,
                5,
            );

            expect(result).toBe(true);
        });
    }, 30000);

    it("should work with small chunk sizes", async () => {
        await withBeebium(async (bbc) => {
            // LDA #$42; STA $0500; JMP $0401
            await plantCode(bbc, [0xA9, 0x42, 0x8D, 0x00, 0x05, 0x4C, 0x01, 0x04]);

            const result = await bbc.runUntilOrTimeout(
                async () => (await bbc.memory.address.peek.readByte(0x0500)) === 0x42,
                5,
                { chunkSeconds: 0.01 }, // Very small chunks
            );

            expect(result).toBe(true);
        });
    }, 30000);
});
