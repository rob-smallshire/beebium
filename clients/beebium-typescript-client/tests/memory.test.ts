import { describe, it, expect, vi } from "vitest";
import {
    PeekAccessor,
    BusAccessor,
    PcContextReader,
    PcContextAccessor,
    RegionPeekAccessor,
    RegionBusAccessor,
    Memory,
    AddressSpace,
} from "../src/memory.js";
import { MemoryAccessError } from "../src/exceptions.js";

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

describe("PeekAccessor", () => {
    describe("readByte", () => {
        it("calls peekMemory with address and length=1, returns first byte", async () => {
            const stub = createMockStub({
                peekMemory: (req: any) => ({
                    data: Buffer.from([0xAB]),
                }),
            });
            const accessor = new PeekAccessor(stub as any);
            const byte = await accessor.readByte(0xFE4D);
            expect(byte).toBe(0xAB);
            expect(stub.peekMemory).toHaveBeenCalledWith(
                { address: 0xFE4D, length: 1 },
                expect.any(Function),
            );
        });
    });

    describe("read", () => {
        it("calls peekMemory with correct address and length", async () => {
            const data = Buffer.from([0x01, 0x02, 0x03, 0x04]);
            const stub = createMockStub({
                peekMemory: () => ({ data }),
            });
            const accessor = new PeekAccessor(stub as any);
            const result = await accessor.read(0x1000, 4);
            expect(result).toEqual(data);
            expect(stub.peekMemory).toHaveBeenCalledWith(
                { address: 0x1000, length: 4 },
                expect.any(Function),
            );
        });
    });
});

describe("BusAccessor", () => {
    describe("readByte", () => {
        it("calls readMemory (not peekMemory)", async () => {
            const stub = createMockStub({
                readMemory: () => ({ data: Buffer.from([0x42]) }),
                peekMemory: () => { throw new Error("should not be called"); },
            });
            const accessor = new BusAccessor(stub as any);
            const byte = await accessor.readByte(0x1000);
            expect(byte).toBe(0x42);
            expect(stub.readMemory).toHaveBeenCalledWith(
                { address: 0x1000, length: 1 },
                expect.any(Function),
            );
            expect(stub.peekMemory).not.toHaveBeenCalled();
        });
    });

    describe("read", () => {
        it("calls readMemory with correct address and length", async () => {
            const data = Buffer.from([0xFF, 0xFE]);
            const stub = createMockStub({
                readMemory: () => ({ data }),
            });
            const accessor = new BusAccessor(stub as any);
            const result = await accessor.read(0x2000, 2);
            expect(result).toEqual(data);
        });
    });

    describe("writeByte", () => {
        it("calls writeMemory with single byte", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await accessor.writeByte(0x3000, 0x55);
            expect(stub.writeMemory).toHaveBeenCalledWith(
                { address: 0x3000, data: Buffer.from([0x55]) },
                expect.any(Function),
            );
        });

        it("throws MemoryAccessError for out-of-range value", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await expect(accessor.writeByte(0, 256)).rejects.toThrow(MemoryAccessError);
            await expect(accessor.writeByte(0, -1)).rejects.toThrow(MemoryAccessError);
        });
    });

    describe("write", () => {
        it("calls writeMemory with data buffer", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            const data = Buffer.from([0x01, 0x02, 0x03]);
            await accessor.write(0x4000, data);
            expect(stub.writeMemory).toHaveBeenCalledWith(
                { address: 0x4000, data },
                expect.any(Function),
            );
        });

        it("throws MemoryAccessError when write fails", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: false }),
            });
            const accessor = new BusAccessor(stub as any);
            await expect(accessor.write(0x4000, Buffer.from([0x00]))).rejects.toThrow(MemoryAccessError);
        });

        it("accepts Uint8Array", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await accessor.write(0x5000, new Uint8Array([0xAA, 0xBB]));
            expect(stub.writeMemory).toHaveBeenCalledOnce();
        });
    });

    describe("fill", () => {
        it("creates fill buffer and calls writeMemory", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await accessor.fill(0x1000, 0x1004, 0xFF);
            const callArgs = stub.writeMemory.mock.calls[0]![0];
            expect(callArgs.address).toBe(0x1000);
            expect(callArgs.data.length).toBe(4);
            expect(callArgs.data[0]).toBe(0xFF);
            expect(callArgs.data[3]).toBe(0xFF);
        });

        it("does nothing when end <= start", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await accessor.fill(0x2000, 0x1000, 0);
            expect(stub.writeMemory).not.toHaveBeenCalled();
        });

        it("defaults to filling with zero", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await accessor.fill(0x1000, 0x1002);
            const callArgs = stub.writeMemory.mock.calls[0]![0];
            expect(callArgs.data[0]).toBe(0);
            expect(callArgs.data[1]).toBe(0);
        });

        it("throws MemoryAccessError for out-of-range fill value", async () => {
            const stub = createMockStub({
                writeMemory: () => ({ success: true }),
            });
            const accessor = new BusAccessor(stub as any);
            await expect(accessor.fill(0, 1, 256)).rejects.toThrow(MemoryAccessError);
        });
    });
});

describe("PcContextReader", () => {
    it("uses simulatedPc in peekMemory request", async () => {
        const stub = createMockStub({
            peekMemory: () => ({ data: Buffer.from([0x77]) }),
        });
        const reader = new PcContextReader(stub as any, 0xD000);
        const byte = await reader.readByte(0x5000);
        expect(byte).toBe(0x77);
        expect(stub.peekMemory).toHaveBeenCalledWith(
            { address: 0x5000, length: 1, simulatedPc: 0xD000 },
            expect.any(Function),
        );
    });

    it("read passes simulatedPc with correct length", async () => {
        const data = Buffer.from([0x01, 0x02]);
        const stub = createMockStub({
            peekMemory: () => ({ data }),
        });
        const reader = new PcContextReader(stub as any, 0xC000);
        const result = await reader.read(0x3000, 2);
        expect(result).toEqual(data);
        expect(stub.peekMemory).toHaveBeenCalledWith(
            { address: 0x3000, length: 2, simulatedPc: 0xC000 },
            expect.any(Function),
        );
    });
});

describe("PcContextAccessor", () => {
    it("uses simulatedPc for reads via readMemory", async () => {
        const stub = createMockStub({
            readMemory: () => ({ data: Buffer.from([0x88]) }),
        });
        const accessor = new PcContextAccessor(stub as any, 0xD000);
        const byte = await accessor.readByte(0x5000);
        expect(byte).toBe(0x88);
        expect(stub.readMemory).toHaveBeenCalledWith(
            { address: 0x5000, length: 1, simulatedPc: 0xD000 },
            expect.any(Function),
        );
    });

    it("uses simulatedPc for writes via writeMemory", async () => {
        const stub = createMockStub({
            writeMemory: () => ({ success: true }),
        });
        const accessor = new PcContextAccessor(stub as any, 0xD000);
        await accessor.writeByte(0x5000, 0x42);
        expect(stub.writeMemory).toHaveBeenCalledWith(
            { address: 0x5000, data: Buffer.from([0x42]), simulatedPc: 0xD000 },
            expect.any(Function),
        );
    });

    it("write passes simulatedPc", async () => {
        const stub = createMockStub({
            writeMemory: () => ({ success: true }),
        });
        const accessor = new PcContextAccessor(stub as any, 0xC000);
        await accessor.write(0x4000, Buffer.from([0xAA, 0xBB]));
        expect(stub.writeMemory).toHaveBeenCalledWith(
            { address: 0x4000, data: Buffer.from([0xAA, 0xBB]), simulatedPc: 0xC000 },
            expect.any(Function),
        );
    });
});

describe("RegionPeekAccessor", () => {
    it("uses peekRegion with regionName", async () => {
        const stub = createMockStub({
            peekRegion: () => ({ data: Buffer.from([0x33]) }),
        });
        const accessor = new RegionPeekAccessor(stub as any, "shadow_ram");
        const byte = await accessor.readByte(0x3000);
        expect(byte).toBe(0x33);
        expect(stub.peekRegion).toHaveBeenCalledWith(
            { regionName: "shadow_ram", address: 0x3000, length: 1 },
            expect.any(Function),
        );
    });

    it("read passes regionName with correct length", async () => {
        const data = Buffer.from([0x10, 0x20, 0x30]);
        const stub = createMockStub({
            peekRegion: () => ({ data }),
        });
        const accessor = new RegionPeekAccessor(stub as any, "main_ram");
        const result = await accessor.read(0x1000, 3);
        expect(result).toEqual(data);
        expect(stub.peekRegion).toHaveBeenCalledWith(
            { regionName: "main_ram", address: 0x1000, length: 3 },
            expect.any(Function),
        );
    });
});

describe("RegionBusAccessor", () => {
    it("uses readRegion with regionName for reads", async () => {
        const stub = createMockStub({
            readRegion: () => ({ data: Buffer.from([0x44]) }),
        });
        const accessor = new RegionBusAccessor(stub as any, "mos_rom");
        const byte = await accessor.readByte(0xC000);
        expect(byte).toBe(0x44);
        expect(stub.readRegion).toHaveBeenCalledWith(
            { regionName: "mos_rom", address: 0xC000, length: 1 },
            expect.any(Function),
        );
    });

    it("uses writeRegion with regionName for writes", async () => {
        const stub = createMockStub({
            writeRegion: () => ({ success: true, error: "" }),
        });
        const accessor = new RegionBusAccessor(stub as any, "main_ram");
        await accessor.writeByte(0x1234, 0x56);
        expect(stub.writeRegion).toHaveBeenCalledWith(
            { regionName: "main_ram", address: 0x1234, data: Buffer.from([0x56]) },
            expect.any(Function),
        );
    });

    it("write calls writeRegion with data buffer", async () => {
        const stub = createMockStub({
            writeRegion: () => ({ success: true, error: "" }),
        });
        const accessor = new RegionBusAccessor(stub as any, "bank_0");
        await accessor.write(0x8000, Buffer.from([0xAA, 0xBB]));
        expect(stub.writeRegion).toHaveBeenCalledWith(
            { regionName: "bank_0", address: 0x8000, data: Buffer.from([0xAA, 0xBB]) },
            expect.any(Function),
        );
    });

    it("throws MemoryAccessError when writeRegion fails", async () => {
        const stub = createMockStub({
            writeRegion: () => ({ success: false, error: "read only" }),
        });
        const accessor = new RegionBusAccessor(stub as any, "mos_rom");
        await expect(accessor.write(0xC000, Buffer.from([0x00]))).rejects.toThrow(MemoryAccessError);
    });

    it("fill creates buffer and calls writeRegion", async () => {
        const stub = createMockStub({
            writeRegion: () => ({ success: true, error: "" }),
        });
        const accessor = new RegionBusAccessor(stub as any, "main_ram");
        await accessor.fill(0x0000, 0x0003, 0xEE);
        const callArgs = stub.writeRegion.mock.calls[0]![0];
        expect(callArgs.regionName).toBe("main_ram");
        expect(callArgs.address).toBe(0x0000);
        expect(callArgs.data.length).toBe(3);
        expect(callArgs.data[0]).toBe(0xEE);
    });
});

describe("Memory", () => {
    describe("getRegions", () => {
        it("fetches and maps regions", async () => {
            const stub = createMockStub({
                getMemoryRegions: () => ({
                    machineType: "ModelB",
                    regions: [
                        {
                            name: "main_ram",
                            baseAddress: 0x0000,
                            size: 0x8000,
                            readable: true,
                            writable: true,
                            hasSideEffects: false,
                            populated: true,
                            active: true,
                        },
                    ],
                }),
            });
            const mem = new Memory(stub as any);
            const regions = await mem.getRegions();
            expect(regions).toHaveLength(1);
            expect(regions[0]!.name).toBe("main_ram");
            expect(regions[0]!.baseAddress).toBe(0x0000);
            expect(regions[0]!.size).toBe(0x8000);
            expect(regions[0]!.readable).toBe(true);
            expect(regions[0]!.writable).toBe(true);
            expect(regions[0]!.hasSideEffects).toBe(false);
            expect(regions[0]!.populated).toBe(true);
            expect(regions[0]!.active).toBe(true);
        });

        it("caches results after first call", async () => {
            const stub = createMockStub({
                getMemoryRegions: () => ({
                    machineType: "ModelBPlus",
                    regions: [],
                }),
            });
            const mem = new Memory(stub as any);
            await mem.getRegions();
            await mem.getRegions();
            expect(stub.getMemoryRegions).toHaveBeenCalledOnce();
        });
    });

    describe("getMachineType", () => {
        it("returns machine type string", async () => {
            const stub = createMockStub({
                getMemoryRegions: () => ({
                    machineType: "ModelBPlus",
                    regions: [],
                }),
            });
            const mem = new Memory(stub as any);
            const type = await mem.getMachineType();
            expect(type).toBe("ModelBPlus");
        });

        it("caches results (shares cache with getRegions)", async () => {
            const stub = createMockStub({
                getMemoryRegions: () => ({
                    machineType: "ModelB",
                    regions: [{ name: "ram", baseAddress: 0, size: 0x8000, readable: true, writable: true, hasSideEffects: false, populated: true, active: true }],
                }),
            });
            const mem = new Memory(stub as any);
            await mem.getRegions();
            const type = await mem.getMachineType();
            expect(type).toBe("ModelB");
            expect(stub.getMemoryRegions).toHaveBeenCalledOnce();
        });
    });
});

describe("AddressSpace", () => {
    it("bus returns BusAccessor", () => {
        const stub = createMockStub({});
        const space = new AddressSpace(stub as any);
        const bus = space.bus;
        expect(bus).toBeInstanceOf(BusAccessor);
    });

    it("peek returns PeekAccessor", () => {
        const stub = createMockStub({});
        const space = new AddressSpace(stub as any);
        const peek = space.peek;
        expect(peek).toBeInstanceOf(PeekAccessor);
    });

    it("bus returns same instance on repeated access", () => {
        const stub = createMockStub({});
        const space = new AddressSpace(stub as any);
        const bus1 = space.bus;
        const bus2 = space.bus;
        expect(bus1).toBe(bus2);
    });

    it("peek returns same instance on repeated access", () => {
        const stub = createMockStub({});
        const space = new AddressSpace(stub as any);
        const peek1 = space.peek;
        const peek2 = space.peek;
        expect(peek1).toBe(peek2);
    });
});
