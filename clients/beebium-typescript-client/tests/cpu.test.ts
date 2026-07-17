import { describe, it, expect, vi } from "vitest";
import { CPU } from "../src/cpu.js";

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

const FULL_STATE = {
    a: 0x42,
    x: 0x10,
    y: 0xFE,
    sp: 0xFF,
    pc: 0xC000,
    p: 0xB1,
    inNmiHandler: true,
    inIrqHandler: false,
    nmiPending: false,
    irqPending: true,
    deviceIrqFlags: 0x03,
    deviceNmiFlags: 0x01,
};

describe("CPU", () => {
    describe("getRegisters", () => {
        it("maps all fields from Get6502State response", async () => {
            const stub = createMockStub({
                get6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            const regs = await cpu.getRegisters();
            expect(regs).toEqual(FULL_STATE);
        });

        it("calls get6502State with empty request", async () => {
            const stub = createMockStub({
                get6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.getRegisters();
            expect(stub.get6502State).toHaveBeenCalledWith(
                {},
                expect.any(Function),
            );
        });
    });

    describe("individual register getters", () => {
        function makeStubAndCpu() {
            const stub = createMockStub({
                get6502State: () => FULL_STATE,
            });
            return { stub, cpu: new CPU(stub as any) };
        }

        it("getA returns accumulator", async () => {
            const { cpu } = makeStubAndCpu();
            expect(await cpu.getA()).toBe(0x42);
        });

        it("getX returns X register", async () => {
            const { cpu } = makeStubAndCpu();
            expect(await cpu.getX()).toBe(0x10);
        });

        it("getY returns Y register", async () => {
            const { cpu } = makeStubAndCpu();
            expect(await cpu.getY()).toBe(0xFE);
        });

        it("getSp returns stack pointer", async () => {
            const { cpu } = makeStubAndCpu();
            expect(await cpu.getSp()).toBe(0xFF);
        });

        it("getPc returns program counter", async () => {
            const { cpu } = makeStubAndCpu();
            expect(await cpu.getPc()).toBe(0xC000);
        });

        it("getP returns processor status", async () => {
            const { cpu } = makeStubAndCpu();
            expect(await cpu.getP()).toBe(0xB1);
        });
    });

    describe("setA", () => {
        it("sends Set6502State with only a field", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setA(0x99);
            expect(stub.set6502State).toHaveBeenCalledWith(
                { a: 0x99 },
                expect.any(Function),
            );
        });
    });

    describe("setPc", () => {
        it("sends Set6502State with only pc field", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setPc(0xD000);
            expect(stub.set6502State).toHaveBeenCalledWith(
                { pc: 0xD000 },
                expect.any(Function),
            );
        });
    });

    describe("setX", () => {
        it("sends Set6502State with only x field", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setX(0x55);
            expect(stub.set6502State).toHaveBeenCalledWith(
                { x: 0x55 },
                expect.any(Function),
            );
        });
    });

    describe("setY", () => {
        it("sends Set6502State with only y field", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setY(0xAA);
            expect(stub.set6502State).toHaveBeenCalledWith(
                { y: 0xAA },
                expect.any(Function),
            );
        });
    });

    describe("setSp", () => {
        it("sends Set6502State with only sp field", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setSp(0xFD);
            expect(stub.set6502State).toHaveBeenCalledWith(
                { sp: 0xFD },
                expect.any(Function),
            );
        });
    });

    describe("setP", () => {
        it("sends Set6502State with only p field", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setP(0x30);
            expect(stub.set6502State).toHaveBeenCalledWith(
                { p: 0x30 },
                expect.any(Function),
            );
        });
    });

    describe("setRegisters", () => {
        it("sends partial fields correctly", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            const result = await cpu.setRegisters({ a: 0x10, pc: 0x8000 });
            expect(stub.set6502State).toHaveBeenCalledWith(
                { a: 0x10, pc: 0x8000 },
                expect.any(Function),
            );
            // The write returns the complete new register snapshot the server
            // read back atomically after applying it.
            expect(result).toEqual(FULL_STATE);
        });

        it("does not send undefined fields", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setRegisters({ x: 0x77 });
            const callArgs = stub.set6502State.mock.calls[0]![0];
            expect(callArgs).toEqual({ x: 0x77 });
            expect("a" in callArgs).toBe(false);
            expect("pc" in callArgs).toBe(false);
        });

        it("sends all fields when all specified", async () => {
            const stub = createMockStub({
                set6502State: () => FULL_STATE,
            });
            const cpu = new CPU(stub as any);
            await cpu.setRegisters({ a: 1, x: 2, y: 3, sp: 4, pc: 5, p: 6 });
            expect(stub.set6502State).toHaveBeenCalledWith(
                { a: 1, x: 2, y: 3, sp: 4, pc: 5, p: 6 },
                expect.any(Function),
            );
        });
    });
});
