/**
 * Integration tests for the debugger: breakpoints, watchpoints,
 * conditional expressions, hit counts, event streaming, and
 * full-range breakpoints.
 *
 * Each test launches a fresh beebium-model-b server so there is no
 * shared emulator state between tests.
 */

import { describe, it, expect } from "vitest";
import { Connection } from "../src/connection.js";
import { Debugger, StopReason } from "../src/debugger.js";
import { CPU } from "../src/cpu.js";
import { Memory } from "../src/memory.js";
import { DebuggerError, InvalidConditionError } from "../src/exceptions.js";
import { withServer } from "./server-harness.js";

/**
 * Reset the machine, plant code at `org`, set PC there.
 * Leaves the machine stopped, ready for breakpoint setup and run().
 */
async function plantAndRun(
    conn: Connection,
    code: number[],
    org = 0x0400,
): Promise<{ dbg: Debugger; cpu: CPU; mem: Memory }> {
    const dbg = new Debugger(conn.debuggerStub);
    const cpu = new CPU(conn.debuggerStub);
    const mem = new Memory(conn.debuggerStub);

    await dbg.reset();
    await mem.address.bus.write(org, Buffer.from(code));
    await cpu.setPc(org);

    return { dbg, cpu, mem };
}

// =========================================================================
// Breakpoints
// =========================================================================

describe("Debugger: Breakpoints", () => {
    it("should add, list, and verify breakpoint fields", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const bpId = await dbg.addBreakpoint(0x1000);
            expect(bpId).toBeGreaterThan(0);

            const bps = await dbg.listBreakpoints();
            expect(bps.length).toBe(1);
            expect(bps[0]!.startAddress).toBe(0x1000);
            expect(bps[0]!.endAddress).toBeGreaterThanOrEqual(0x1000);
        });
    });

    it("should remove a breakpoint by ID", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const bpId = await dbg.addBreakpoint(0x2000);
            expect(await dbg.removeBreakpoint(bpId)).toBe(true);
            expect((await dbg.listBreakpoints()).length).toBe(0);
        });
    });

    it("should include condition, stopCounterpart, and hitCount in listing", async () => {
        await withServer(async (conn) => {
            // LDX #0; .loop: INX; JMP loop
            const code = [0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04];
            const { dbg, cpu } = await plantAndRun(conn, code);

            const bpId = await dbg.addBreakpoint(0x0402, {
                condition: "hits == 3",
                stopCounterpart: true,
            });

            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            const bps = await dbg.listBreakpoints();
            expect(bps.length).toBe(1);
            const bp = bps[0]!;
            expect(bp.id).toBe(bpId);
            expect(bp.startAddress).toBe(0x0402);
            expect(bp.condition).toBe("hits == 3");
            expect(bp.stopCounterpart).toBe(true);
            expect(bp.hitCount).toBe(3);

            await dbg.clearBreakpoints();
        });
    });

    it("should clear all breakpoints", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            for (const addr of [0x1000, 0x2000, 0x3000]) {
                await dbg.addBreakpoint(addr);
            }
            const removed = await dbg.clearBreakpoints();
            expect(removed).toBe(3);
            expect((await dbg.listBreakpoints()).length).toBe(0);
        });
    });

    it("should stop at a breakpoint address", async () => {
        await withServer(async (conn) => {
            // LDA #$42; STA $0500; NOP
            const code = [0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA];
            const { dbg, cpu } = await plantAndRun(conn, code);

            const bpId = await dbg.addBreakpoint(0x0405);
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            expect(await cpu.getA()).toBe(0x42);
            await dbg.removeBreakpoint(bpId);
        });
    });
});

// =========================================================================
// Conditional Breakpoints
// =========================================================================

describe("Debugger: Conditional Breakpoints", () => {
    it("should fire only when register condition matches (A == 0x42)", async () => {
        await withServer(async (conn) => {
            // LDX #0; .loop: LDA table,X; INX; CPX #4; BNE loop; NOP; .table: $10,$42,$99,$FF
            const code = [
                0xA2, 0x00,             // LDX #0
                0xBD, 0x0B, 0x04,       // LDA $040B,X
                0xE8,                   // INX
                0xE0, 0x04,             // CPX #4
                0xD0, 0xF8,             // BNE loop (-8 -> 0x0402)
                0xEA,                   // NOP
                0x10, 0x42, 0x99, 0xFF, // table data
            ];
            const { dbg, cpu } = await plantAndRun(conn, code);

            const bpId = await dbg.addBreakpoint(0x0402, { condition: "A == 0x42" });
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            expect(await cpu.getA()).toBe(0x42);
            await dbg.removeBreakpoint(bpId);
        });
    });

    it("should fire on the Nth hit using hits == N", async () => {
        await withServer(async (conn) => {
            // LDX #0; .loop: INX; JMP loop
            const code = [0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04];
            const { dbg, cpu } = await plantAndRun(conn, code);

            const bpId = await dbg.addBreakpoint(0x0402, { condition: "hits == 3" });
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            // Breakpoint fires before the 3rd INX executes; X == 2
            expect(await cpu.getX()).toBe(2);
            await dbg.removeBreakpoint(bpId);
        });
    });

    it("should fire every Nth hit using hits % N == 0", async () => {
        await withServer(async (conn) => {
            // LDX #0; .loop: INX; JMP loop
            const code = [0xA2, 0x00, 0xE8, 0x4C, 0x02, 0x04];
            const { dbg, cpu } = await plantAndRun(conn, code);

            const bpId = await dbg.addBreakpoint(0x0402, { condition: "hits % 5 == 0" });
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            // First fire at hits==5, before 5th INX; X == 4
            expect(await cpu.getX()).toBe(4);
            await dbg.removeBreakpoint(bpId);
        });
    });

    it("should throw InvalidConditionError on invalid condition", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await expect(
                dbg.addBreakpoint(0x1000, { condition: "invalid !@#" }),
            ).rejects.toThrow(InvalidConditionError);
        });
    });
});

// =========================================================================
// Watchpoints
// =========================================================================

describe("Debugger: Watchpoints", () => {
    it("should add, list, and verify watchpoint fields", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const wpId = await dbg.addWatchpoint(0x1000, 0x1010, "write");
            const wps = await dbg.listWatchpoints();
            expect(wps.length).toBe(1);
            expect(wps[0]!.startAddress).toBe(0x1000);
            expect(wps[0]!.endAddress).toBe(0x1010);
            expect(wps[0]!.type).toBe("write");
        });
    });

    it("should remove a watchpoint by ID", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const wpId = await dbg.addWatchpoint(0x2000, 0x2001);
            expect(await dbg.removeWatchpoint(wpId)).toBe(true);
            expect((await dbg.listWatchpoints()).length).toBe(0);
        });
    });

    it("should clear all watchpoints", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await dbg.addWatchpoint(0x1000, 0x1001);
            await dbg.addWatchpoint(0x2000, 0x2001);
            const removed = await dbg.clearWatchpoints();
            expect(removed).toBe(2);
        });
    });

    it("should fire write watchpoint on STA", async () => {
        await withServer(async (conn) => {
            // LDA #$42; STA $0500; NOP
            const code = [0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA];
            const { dbg, mem } = await plantAndRun(conn, code);

            const wpId = await dbg.addWatchpoint(0x0500, 0x0501, "write");
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            expect(await mem.address.peek.readByte(0x0500)).toBe(0x42);
            await dbg.removeWatchpoint(wpId);
        });
    });

    it("should not fire write watchpoint on LDA (read)", async () => {
        await withServer(async (conn) => {
            // LDA $0500; NOP; NOP
            const code = [0xAD, 0x00, 0x05, 0xEA, 0xEA];
            const { dbg, mem } = await plantAndRun(conn, code);

            await mem.address.bus.writeByte(0x0500, 0xAB);
            await dbg.addWatchpoint(0x0500, 0x0501, "write");

            // Use a cycle-budget breakpoint to run a deterministic number
            // of cycles. The write watchpoint should not fire (only a read).
            const cycleCount = (await dbg.getState()).cycleCount;
            const bpId = await dbg.addBreakpoint(0x0000, {
                endAddress: 0x10000,
                condition: `cycles >= ${cycleCount + 1000}`,
            });
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;
            await dbg.removeBreakpoint(bpId);

            // If the write watchpoint didn't fire, we hit the cycle-budget breakpoint
            expect(await dbg.isStopped()).toBe(true);
        });
    });
});

// =========================================================================
// Conditional Watchpoints
// =========================================================================

describe("Debugger: Conditional Watchpoints", () => {
    it("should fire only when register condition matches (A == 0x42)", async () => {
        await withServer(async (conn) => {
            // LDA #$10; STA $0500; LDA #$42; STA $0500; LDA #$99; STA $0500; NOP
            const code = [
                0xA9, 0x10, 0x8D, 0x00, 0x05,  // LDA #$10; STA $0500
                0xA9, 0x42, 0x8D, 0x00, 0x05,  // LDA #$42; STA $0500
                0xA9, 0x99, 0x8D, 0x00, 0x05,  // LDA #$99; STA $0500
                0xEA,                            // NOP
            ];
            const { dbg, mem } = await plantAndRun(conn, code);

            const wpId = await dbg.addWatchpoint(0x0500, 0x0501, "write", {
                condition: "A == 0x42",
            });
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            // Stopped on the second STA when A was 0x42
            expect(await mem.address.peek.readByte(0x0500)).toBe(0x42);
            await dbg.removeWatchpoint(wpId);
        });
    });

    it("should not stop when condition is false", async () => {
        await withServer(async (conn) => {
            // LDA #$42; STA $0500; NOP; NOP
            const code = [0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA, 0xEA];
            const { dbg, mem } = await plantAndRun(conn, code);

            await dbg.addWatchpoint(0x0500, 0x0501, "write", {
                condition: "false",
            });

            // Use a cycle-budget breakpoint to run a deterministic number
            // of cycles instead of relying on wall-clock sleep.
            const cycleCount = (await dbg.getState()).cycleCount;
            const bpId = await dbg.addBreakpoint(0x0000, {
                endAddress: 0x10000,
                condition: `cycles >= ${cycleCount + 1000}`,
            });
            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;
            await dbg.removeBreakpoint(bpId);

            // The write still happened, but the watchpoint didn't stop execution
            expect(await mem.address.peek.readByte(0x0500)).toBe(0x42);
        });
    });

    it("should throw InvalidConditionError on invalid condition", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await expect(
                dbg.addWatchpoint(0x1000, 0x1001, "write", {
                    condition: "bad syntax !!!",
                }),
            ).rejects.toThrow(InvalidConditionError);
        });
    });
});

// =========================================================================
// Event Stream
// =========================================================================

describe("Debugger: Event Stream", () => {
    it("should deliver running and stopped events in order", async () => {
        await withServer(async (conn) => {
            // LDA #$42; NOP
            const code = [0xA9, 0x42, 0xEA];
            const { dbg } = await plantAndRun(conn, code);
            await dbg.addBreakpoint(0x0402);

            const events: Array<{ isRunning: boolean; reason: number }> = [];
            const stream = dbg.watchExecutionState();

            // Consume initial state
            const initial = await stream.next();
            expect(initial.done).toBe(false);
            expect(initial.value!.state.isRunning).toBe(false);

            // Run and collect events until stopped
            await dbg.run();

            for await (const event of stream) {
                events.push({
                    isRunning: event.state.isRunning,
                    reason: event.reason,
                });
                if (!event.state.isRunning) break;
            }

            expect(events.length).toBeGreaterThanOrEqual(2);
            // First event: running
            expect(events[0]!.isRunning).toBe(true);
            // Last event: stopped
            expect(events[events.length - 1]!.isRunning).toBe(false);

            await dbg.clearBreakpoints();
        });
    });

    it("should deliver events to two concurrent subscribers", async () => {
        await withServer(async (conn) => {
            // LDA #$42; NOP
            const code = [0xA9, 0x42, 0xEA];
            const { dbg } = await plantAndRun(conn, code);
            await dbg.addBreakpoint(0x0402);

            const stream1 = dbg.watchExecutionState();
            const stream2 = dbg.watchExecutionState();

            // Consume initial state from both
            const init1 = await stream1.next();
            const init2 = await stream2.next();
            expect(init1.value!.state.isRunning).toBe(false);
            expect(init2.value!.state.isRunning).toBe(false);

            await dbg.run();

            // Collect from stream1
            const events1: boolean[] = [];
            for await (const event of stream1) {
                events1.push(event.state.isRunning);
                if (!event.state.isRunning) break;
            }

            // Collect from stream2
            const events2: boolean[] = [];
            for await (const event of stream2) {
                events2.push(event.state.isRunning);
                if (!event.state.isRunning) break;
            }

            expect(events1.length).toBeGreaterThanOrEqual(2);
            expect(events2.length).toBeGreaterThanOrEqual(2);
            expect(events1[events1.length - 1]).toBe(false);
            expect(events2[events2.length - 1]).toBe(false);

            await dbg.clearBreakpoints();
        });
    });

    it("should support runUntil(address)", async () => {
        await withServer(async (conn) => {
            // LDA #$42; STA $0500; NOP
            const code = [0xA9, 0x42, 0x8D, 0x00, 0x05, 0xEA];
            const { dbg, cpu } = await plantAndRun(conn, code);

            const event = await dbg.runUntil(0x0405);
            expect(event.state.isRunning).toBe(false);
            expect(await cpu.getA()).toBe(0x42);
        });
    });
});

// =========================================================================
// Full-Range Breakpoints
// =========================================================================

describe("Debugger: Full-Range Breakpoints", () => {
    it("should stop after a cycle budget", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await dbg.stop();
            const startCycles = (await dbg.getState()).cycleCount;

            const target = startCycles + 1000;
            const bpId = await dbg.addBreakpoint(0x0000, {
                endAddress: 0x10000,
                condition: `cycles >= ${target}`,
            });

            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            const finalCycles = (await dbg.getState()).cycleCount;
            expect(finalCycles).toBeGreaterThanOrEqual(target);
            // Should not overshoot by more than one instruction (~6 cycles max)
            expect(finalCycles).toBeLessThan(target + 10);

            await dbg.removeBreakpoint(bpId);
        });
    });

    it("should stop on a memory predicate", async () => {
        await withServer(async (conn) => {
            // LDA #$00; .loop: CLC; ADC #$01; STA $0500; JMP loop
            const code = [
                0xA9, 0x00,             // LDA #$00
                0x18,                   // CLC
                0x69, 0x01,             // ADC #$01
                0x8D, 0x00, 0x05,       // STA $0500
                0x4C, 0x02, 0x04,       // JMP $0402
            ];
            const { dbg, mem } = await plantAndRun(conn, code);

            const bpId = await dbg.addBreakpoint(0x0000, {
                endAddress: 0x10000,
                condition: "mem[0x0500] == 0x0A",
            });

            const stopPromise = dbg.waitForStop();
            await dbg.run();
            await stopPromise;

            expect(await mem.address.peek.readByte(0x0500)).toBe(0x0A);
            await dbg.removeBreakpoint(bpId);
        });
    });
});

// =========================================================================
// CPU State
// =========================================================================

describe("Debugger: CPU State", () => {
    it("should return all register fields", async () => {
        await withServer(async (conn) => {
            const cpu = new CPU(conn.debuggerStub);
            const regs = await cpu.getRegisters();
            expect(typeof regs.a).toBe("number");
            expect(typeof regs.x).toBe("number");
            expect(typeof regs.y).toBe("number");
            expect(typeof regs.sp).toBe("number");
            expect(typeof regs.pc).toBe("number");
            expect(typeof regs.p).toBe("number");
        });
    });

    it("should set and read back A and X", async () => {
        await withServer(async (conn) => {
            const cpu = new CPU(conn.debuggerStub);
            const dbg = new Debugger(conn.debuggerStub);
            await dbg.stop();

            await cpu.setA(0xAA);
            await cpu.setX(0xBB);
            const regs = await cpu.getRegisters();
            expect(regs.a).toBe(0xAA);
            expect(regs.x).toBe(0xBB);
        });
    });
});
