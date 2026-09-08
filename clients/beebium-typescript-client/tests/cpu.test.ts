import { describe, it, expect, vi } from "vitest";
import { CPU } from "../src/cpu.js";
import { RegisterRole } from "../src/generated/debugger.js";

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

// A 6502 CPU description, as the server would report it.
const DESCRIPTOR = {
    family: "6502",
    addressBits: 16,
    littleEndian: true,
    registers: [
        { name: "A", widthBits: 8, role: RegisterRole.REGISTER_ROLE_NONE, flagNames: [] },
        { name: "X", widthBits: 8, role: RegisterRole.REGISTER_ROLE_NONE, flagNames: [] },
        { name: "Y", widthBits: 8, role: RegisterRole.REGISTER_ROLE_NONE, flagNames: [] },
        { name: "SP", widthBits: 8, role: RegisterRole.STACK_POINTER, flagNames: [] },
        { name: "PC", widthBits: 16, role: RegisterRole.PROGRAM_COUNTER, flagNames: [] },
        {
            name: "P",
            widthBits: 8,
            role: RegisterRole.FLAGS,
            flagNames: ["C", "Z", "I", "D", "B", "", "V", "N"],
        },
    ],
    signals: ["IRQ", "NMI"],
};

// The state that matches DESCRIPTOR, as CpuState name/value pairs.
const FULL_STATE = {
    registers: [
        { name: "A", value: 0x42 },
        { name: "X", value: 0x10 },
        { name: "Y", value: 0xFE },
        { name: "SP", value: 0xFF },
        { name: "PC", value: 0xC000 },
        { name: "P", value: 0xB1 },
    ],
    signals: [
        { name: "IRQ", asserted: true, pending: true, inHandler: false },
        { name: "NMI", asserted: false, pending: false, inHandler: false },
    ],
    cycleCount: 0,
};

const EXPECTED_REGS = { a: 0x42, x: 0x10, y: 0xFE, sp: 0xFF, pc: 0xC000, p: 0xB1 };

describe("CPU", () => {
    describe("getDescriptor", () => {
        it("returns and caches the CPU description", async () => {
            const stub = createMockStub({ getCpuDescriptor: () => DESCRIPTOR });
            const cpu = new CPU(stub as any);
            const d = await cpu.getDescriptor();
            expect(d.family).toBe("6502");
            expect(d.registers.map((r: any) => r.name)).toEqual(["A", "X", "Y", "SP", "PC", "P"]);
            await cpu.getDescriptor();
            expect(stub.getCpuDescriptor).toHaveBeenCalledTimes(1); // cached
        });
    });

    describe("getRegisters", () => {
        it("maps the CpuState register pairs to a name/value map", async () => {
            const stub = createMockStub({ getCpuState: () => FULL_STATE });
            const cpu = new CPU(stub as any);
            const regs = await cpu.getRegisters();
            expect(regs).toEqual(EXPECTED_REGS);
            expect(regs.pc).toBe(0xC000);
            expect(regs["pc"]).toBe(0xC000);
        });

        it("calls getCpuState with an empty request", async () => {
            const stub = createMockStub({ getCpuState: () => FULL_STATE });
            const cpu = new CPU(stub as any);
            await cpu.getRegisters();
            expect(stub.getCpuState).toHaveBeenCalledWith({}, expect.any(Function));
        });
    });

    describe("getSignals", () => {
        it("maps the CpuState signal states, keyed by name", async () => {
            const stub = createMockStub({ getCpuState: () => FULL_STATE });
            const cpu = new CPU(stub as any);
            const signals = await cpu.getSignals();
            expect(Object.keys(signals)).toEqual(["IRQ", "NMI"]);
            expect(signals.IRQ!.pending).toBe(true);
            expect(signals.NMI!.pending).toBe(false);
        });
    });

    describe("individual register getters", () => {
        function makeCpu() {
            return new CPU(createMockStub({ getCpuState: () => FULL_STATE }) as any);
        }

        it("getA returns accumulator", async () => {
            expect(await makeCpu().getA()).toBe(0x42);
        });
        it("getX returns X register", async () => {
            expect(await makeCpu().getX()).toBe(0x10);
        });
        it("getY returns Y register", async () => {
            expect(await makeCpu().getY()).toBe(0xFE);
        });
        it("getSp returns stack pointer", async () => {
            expect(await makeCpu().getSp()).toBe(0xFF);
        });
        it("getPc returns program counter", async () => {
            expect(await makeCpu().getPc()).toBe(0xC000);
        });
        it("getP returns processor status", async () => {
            expect(await makeCpu().getP()).toBe(0xB1);
        });
    });

    describe("setRegisters", () => {
        function makeStub() {
            return createMockStub({
                getCpuDescriptor: () => DESCRIPTOR,
                setCpuState: () => FULL_STATE,
            });
        }

        it("sends the given registers as descriptor-named pairs", async () => {
            const stub = makeStub();
            const cpu = new CPU(stub as any);
            const result = await cpu.setRegisters({ a: 0x10, pc: 0x8000 });
            const sent = stub.setCpuState.mock.calls[0]![0];
            expect(sent.registers).toEqual([
                { name: "A", value: 0x10 },
                { name: "PC", value: 0x8000 },
            ]);
            // The write returns the complete new register snapshot the server
            // read back atomically after applying it.
            expect(result).toEqual(EXPECTED_REGS);
        });

        it("sends only the registers given", async () => {
            const stub = makeStub();
            const cpu = new CPU(stub as any);
            await cpu.setRegisters({ x: 0x77 });
            const sent = stub.setCpuState.mock.calls[0]![0];
            expect(sent.registers).toEqual([{ name: "X", value: 0x77 }]);
        });

        it("setA sends only the A register", async () => {
            const stub = makeStub();
            const cpu = new CPU(stub as any);
            await cpu.setA(0x99);
            const sent = stub.setCpuState.mock.calls[0]![0];
            expect(sent.registers).toEqual([{ name: "A", value: 0x99 }]);
        });

        it("setPc sends only the PC register", async () => {
            const stub = makeStub();
            const cpu = new CPU(stub as any);
            await cpu.setPc(0xD000);
            const sent = stub.setCpuState.mock.calls[0]![0];
            expect(sent.registers).toEqual([{ name: "PC", value: 0xD000 }]);
        });
    });
});
