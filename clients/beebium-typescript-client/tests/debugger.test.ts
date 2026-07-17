import { describe, it, expect, vi } from "vitest";
import { Debugger } from "../src/debugger.js";
import { DebuggerError } from "../src/exceptions.js";

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                const response = handler(request);
                callback(null, response);
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

describe("Debugger", () => {
    describe("getState", () => {
        it("maps response fields correctly", async () => {
            const stub = createMockStub({
                getState: () => ({
                    isRunning: true,
                    cycleCount: 123456,
                    haltReason: "breakpoint",
                    sequence: 42,
                }),
            });
            const dbg = new Debugger(stub as any);
            const state = await dbg.getState();
            expect(state).toEqual({
                isRunning: true,
                cycleCount: 123456,
                haltReason: "breakpoint",
                sequence: 42,
            });
        });

        it("maps state with empty haltReason", async () => {
            const stub = createMockStub({
                getState: () => ({
                    isRunning: false,
                    cycleCount: 0,
                    haltReason: "",
                    sequence: 0,
                }),
            });
            const dbg = new Debugger(stub as any);
            const state = await dbg.getState();
            expect(state.isRunning).toBe(false);
            expect(state.haltReason).toBe("");
        });
    });

    describe("run", () => {
        it("calls Run method successfully", async () => {
            const stub = createMockStub({
                run: () => ({ success: true, error: "" }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.run()).resolves.toBeUndefined();
            expect(stub.run).toHaveBeenCalledOnce();
        });

        it("throws DebuggerError when success is false", async () => {
            const stub = createMockStub({
                run: () => ({ success: false, error: "already running" }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.run()).rejects.toThrow(DebuggerError);
            await expect(dbg.run()).rejects.toThrow("run failed: already running");
        });
    });

    describe("stop", () => {
        it("returns execution state after stopping", async () => {
            const stub = createMockStub({
                stop: () => ({
                    success: true,
                    state: {
                        isRunning: false,
                        cycleCount: 999,
                        haltReason: "user",
                        sequence: 5,
                    },
                }),
            });
            const dbg = new Debugger(stub as any);
            const state = await dbg.stop();
            expect(state).toEqual({
                isRunning: false,
                cycleCount: 999,
                haltReason: "user",
                sequence: 5,
            });
        });

        it("throws DebuggerError when success is false", async () => {
            const stub = createMockStub({
                stop: () => ({ success: false }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.stop()).rejects.toThrow(DebuggerError);
            await expect(dbg.stop()).rejects.toThrow("stop failed");
        });

        it("throws DebuggerError when no state returned", async () => {
            const stub = createMockStub({
                stop: () => ({ success: true, state: undefined }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.stop()).rejects.toThrow(DebuggerError);
            await expect(dbg.stop()).rejects.toThrow("stop returned no state");
        });
    });

    describe("reset", () => {
        it("calls Reset successfully", async () => {
            const stub = createMockStub({
                reset: () => ({ success: true }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.reset()).resolves.toBeUndefined();
            expect(stub.reset).toHaveBeenCalledOnce();
        });

        it("throws DebuggerError when success is false", async () => {
            const stub = createMockStub({
                reset: () => ({ success: false }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.reset()).rejects.toThrow(DebuggerError);
            await expect(dbg.reset()).rejects.toThrow("reset failed");
        });
    });

    describe("step", () => {
        it("calls StepInstruction with correct count", async () => {
            const stub = createMockStub({
                stepInstruction: (req: any) => ({
                    success: true,
                    error: "",
                    instructionsExecuted: req.count,
                    cyclesExecuted: req.count * 3,
                    state: {
                        isRunning: false,
                        cycleCount: 100,
                        haltReason: "",
                        sequence: 1,
                    },
                }),
            });
            const dbg = new Debugger(stub as any);
            const result = await dbg.step(5);
            expect(result.instructionsExecuted).toBe(5);
            expect(result.cyclesExecuted).toBe(15);
            expect(result.state.cycleCount).toBe(100);
            expect(stub.stepInstruction).toHaveBeenCalledWith(
                { count: 5 },
                expect.any(Function),
            );
        });

        it("defaults to count=1", async () => {
            const stub = createMockStub({
                stepInstruction: (req: any) => ({
                    success: true,
                    error: "",
                    instructionsExecuted: req.count,
                    cyclesExecuted: 2,
                    state: {
                        isRunning: false,
                        cycleCount: 50,
                        haltReason: "",
                        sequence: 1,
                    },
                }),
            });
            const dbg = new Debugger(stub as any);
            await dbg.step();
            expect(stub.stepInstruction).toHaveBeenCalledWith(
                { count: 1 },
                expect.any(Function),
            );
        });

        it("throws DebuggerError on failure", async () => {
            const stub = createMockStub({
                stepInstruction: () => ({
                    success: false,
                    error: "CPU halted",
                    instructionsExecuted: 0,
                    cyclesExecuted: 0,
                    state: undefined,
                }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.step()).rejects.toThrow(DebuggerError);
            await expect(dbg.step()).rejects.toThrow("step failed: CPU halted");
        });

        it("provides default state when response has no state", async () => {
            const stub = createMockStub({
                stepInstruction: () => ({
                    success: true,
                    error: "",
                    instructionsExecuted: 1,
                    cyclesExecuted: 2,
                    state: undefined,
                }),
            });
            const dbg = new Debugger(stub as any);
            const result = await dbg.step();
            expect(result.state).toEqual({
                isRunning: false,
                cycleCount: 0,
                haltReason: "",
                sequence: 0,
            });
        });
    });

    describe("stepCycles", () => {
        it("calls StepCycle with correct count", async () => {
            const stub = createMockStub({
                stepCycle: (req: any) => ({
                    success: true,
                    error: "",
                    instructionsExecuted: 1,
                    cyclesExecuted: req.count,
                    state: {
                        isRunning: false,
                        cycleCount: 200,
                        haltReason: "",
                        sequence: 3,
                    },
                }),
            });
            const dbg = new Debugger(stub as any);
            const result = await dbg.stepCycles(10);
            expect(result.cyclesExecuted).toBe(10);
            expect(stub.stepCycle).toHaveBeenCalledWith(
                { count: 10 },
                expect.any(Function),
            );
        });

        it("throws DebuggerError on failure", async () => {
            const stub = createMockStub({
                stepCycle: () => ({
                    success: false,
                    error: "in reset",
                    instructionsExecuted: 0,
                    cyclesExecuted: 0,
                    state: undefined,
                }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.stepCycles(1)).rejects.toThrow(DebuggerError);
            await expect(dbg.stepCycles(1)).rejects.toThrow("stepCycles failed: in reset");
        });
    });

    describe("addBreakpoint", () => {
        it("sends correct address and returns id", async () => {
            const stub = createMockStub({
                addBreakpoint: (req: any) => ({
                    success: true,
                    id: 7,
                }),
            });
            const dbg = new Debugger(stub as any);
            const id = await dbg.addBreakpoint(0xC000);
            expect(id).toBe(7);
            expect(stub.addBreakpoint).toHaveBeenCalledWith(
                { startAddress: 0xC000, endAddress: 0, condition: "", stopCounterpart: false },
                expect.any(Function),
            );
        });

        it("throws DebuggerError on failure", async () => {
            const stub = createMockStub({
                addBreakpoint: () => ({ success: false, id: 0 }),
            });
            const dbg = new Debugger(stub as any);
            await expect(dbg.addBreakpoint(0x1234)).rejects.toThrow(DebuggerError);
            await expect(dbg.addBreakpoint(0x1234)).rejects.toThrow("addBreakpoint failed at address 0x1234");
        });
    });

    describe("removeBreakpoint", () => {
        it("sends correct id and returns success", async () => {
            const stub = createMockStub({
                removeBreakpoint: () => ({ success: true }),
            });
            const dbg = new Debugger(stub as any);
            const result = await dbg.removeBreakpoint(3);
            expect(result).toBe(true);
            expect(stub.removeBreakpoint).toHaveBeenCalledWith(
                { id: 3 },
                expect.any(Function),
            );
        });

        it("returns false when breakpoint not found", async () => {
            const stub = createMockStub({
                removeBreakpoint: () => ({ success: false }),
            });
            const dbg = new Debugger(stub as any);
            const result = await dbg.removeBreakpoint(99);
            expect(result).toBe(false);
        });
    });

    describe("listBreakpoints", () => {
        it("maps breakpoints array", async () => {
            const stub = createMockStub({
                listBreakpoints: () => ({
                    breakpoints: [
                        { id: 1, startAddress: 0x1000, endAddress: 0x1001, condition: "", stopCounterpart: false, hitCount: 0 },
                        { id: 2, startAddress: 0xC000, endAddress: 0xC001, condition: "", stopCounterpart: false, hitCount: 0 },
                        { id: 3, startAddress: 0xFFFC, endAddress: 0xFFFD, condition: "", stopCounterpart: false, hitCount: 0 },
                    ],
                }),
            });
            const dbg = new Debugger(stub as any);
            const bps = await dbg.listBreakpoints();
            expect(bps).toEqual([
                { id: 1, startAddress: 0x1000, endAddress: 0x1001, condition: "", stopCounterpart: false, hitCount: 0 },
                { id: 2, startAddress: 0xC000, endAddress: 0xC001, condition: "", stopCounterpart: false, hitCount: 0 },
                { id: 3, startAddress: 0xFFFC, endAddress: 0xFFFD, condition: "", stopCounterpart: false, hitCount: 0 },
            ]);
        });

        it("returns empty array when no breakpoints", async () => {
            const stub = createMockStub({
                listBreakpoints: () => ({ breakpoints: [] }),
            });
            const dbg = new Debugger(stub as any);
            const bps = await dbg.listBreakpoints();
            expect(bps).toEqual([]);
        });
    });

    describe("clearBreakpoints", () => {
        it("returns countRemoved", async () => {
            const stub = createMockStub({
                clearBreakpoints: () => ({ countRemoved: 5 }),
            });
            const dbg = new Debugger(stub as any);
            const count = await dbg.clearBreakpoints();
            expect(count).toBe(5);
        });

        it("returns zero when no breakpoints to clear", async () => {
            const stub = createMockStub({
                clearBreakpoints: () => ({ countRemoved: 0 }),
            });
            const dbg = new Debugger(stub as any);
            const count = await dbg.clearBreakpoints();
            expect(count).toBe(0);
        });
    });

    describe("isRunning / isStopped", () => {
        it("isRunning returns true when running", async () => {
            const stub = createMockStub({
                getState: () => ({
                    isRunning: true,
                    cycleCount: 0,
                    haltReason: "",
                    sequence: 0,
                }),
            });
            const dbg = new Debugger(stub as any);
            expect(await dbg.isRunning()).toBe(true);
            expect(await dbg.isStopped()).toBe(false);
        });

        it("isStopped returns true when stopped", async () => {
            const stub = createMockStub({
                getState: () => ({
                    isRunning: false,
                    cycleCount: 0,
                    haltReason: "",
                    sequence: 0,
                }),
            });
            const dbg = new Debugger(stub as any);
            expect(await dbg.isStopped()).toBe(true);
            expect(await dbg.isRunning()).toBe(false);
        });
    });
});
