/**
 * Memory access for the Beebium TypeScript client.
 *
 * This module provides a typed interface for reading and writing memory in the
 * emulated BBC Micro. Memory access is explicit about side effects:
 *
 * Address space access (16-bit flat address space):
 *   await bbc.memory.address.bus.readByte(0x1000)        // Side-effecting read
 *   await bbc.memory.address.peek.readByte(0xFE4D)       // Side-effect-free read
 *   await bbc.memory.address.bus.writeByte(0x1000, 0x42)  // Write through bus
 *
 * PC-context access (for B+ shadow RAM routing):
 *   await bbc.memory.address.bus.withPc(0xD000).readByte(0x5000)   // What MOS code would see
 *   await bbc.memory.address.peek.withPc(0xD000).readByte(0x5000)  // Side-effect-free, MOS view
 *
 * Region-based access (by named memory region, absolute addresses):
 *   await bbc.memory.region("main_ram").bus.readByte(0x1234)       // Read from main RAM
 *   await bbc.memory.region("shadow_ram").peek.readByte(0x3000)    // Peek shadow RAM
 *
 * File I/O and utilities (on accessors):
 *   await bbc.memory.address.bus.load(0x1000, "file.bin")          // Load file
 *   await bbc.memory.address.peek.save(0x1000, 100, "out.bin")    // Save range
 *   await bbc.memory.address.bus.fill(0x1000, 0x2000, 0)          // Fill with zeros
 *
 * Discovery:
 *   await bbc.memory.getRegions()     // List of MemoryRegionInfo
 *   await bbc.memory.getMachineType() // "ModelB", "ModelBPlus", etc.
 */

import { readFile, writeFile } from "node:fs/promises";

import type {
    DebuggerControlClient,
    PeekMemoryRequest,
    PeekMemoryResponse,
    ReadMemoryRequest,
    ReadMemoryResponse,
    WriteMemoryRequest,
    WriteMemoryResponse,
    GetMemoryRegionsRequest,
    GetMemoryRegionsResponse,
    RegionAccessRequest,
    RegionAccessResponse,
    WriteRegionRequest,
    WriteRegionResponse,
    MemoryRegionInfo as ProtoMemoryRegionInfo,
} from "./generated/debugger.js";

import { promisify } from "./call-utils.js";
import { MemoryAccessError } from "./exceptions.js";

// =============================================================================
// Memory Region Info
// =============================================================================

/** Information about a memory region. */
export interface MemoryRegionInfo {
    name: string;
    baseAddress: number;
    size: number;
    readable: boolean;
    writable: boolean;
    hasSideEffects: boolean;
    populated: boolean;
    active: boolean;
}

// =============================================================================
// Helpers
// =============================================================================

/** Format a 16-bit address as a hex string with leading $ sign. */
function hexAddr(address: number): string {
    return "$" + address.toString(16).toUpperCase().padStart(4, "0");
}

// =============================================================================
// PC-Context Accessors (for B+ shadow RAM routing)
// =============================================================================

/**
 * Side-effect-free memory access with simulated PC context.
 *
 * On BBC Model B+, memory access to 0x3000-0x7FFF is routed based on the
 * program counter. This accessor simulates access from a specified PC address.
 * For Model B (no shadow RAM), this behaves identically to PeekAccessor.
 */
export class PcContextReader {
    constructor(
        private readonly stub: DebuggerControlClient,
        private readonly pc: number,
    ) {}

    /** Read a single byte without side effects, using the simulated PC. */
    async readByte(address: number): Promise<number> {
        const data = await this.read(address, 1);
        return data[0]!;
    }

    /** Read a sequence of bytes without side effects, using the simulated PC. */
    async read(address: number, length: number): Promise<Buffer> {
        const request: PeekMemoryRequest = {
            address,
            length,
            simulatedPc: this.pc,
        };
        const response = await promisify<PeekMemoryRequest, PeekMemoryResponse>(
            this.stub as unknown as Record<string, Function>,
            "peekMemory",
            request,
        );
        return response.data;
    }
}

/**
 * Side-effecting memory access with simulated PC context.
 *
 * On BBC Model B+, memory access to 0x3000-0x7FFF is routed based on the
 * program counter. This accessor simulates access from a specified PC address.
 * For Model B (no shadow RAM), this behaves identically to BusAccessor.
 */
export class PcContextAccessor extends PcContextReader {
    constructor(
        private readonly _stub: DebuggerControlClient,
        private readonly _pc: number,
    ) {
        super(_stub, _pc);
    }

    /** Read a single byte through the bus, using the simulated PC. */
    override async readByte(address: number): Promise<number> {
        const data = await this.read(address, 1);
        return data[0]!;
    }

    /** Read a sequence of bytes through the bus, using the simulated PC. */
    override async read(address: number, length: number): Promise<Buffer> {
        const request: ReadMemoryRequest = {
            address,
            length,
            simulatedPc: this._pc,
        };
        const response = await promisify<ReadMemoryRequest, ReadMemoryResponse>(
            this._stub as unknown as Record<string, Function>,
            "readMemory",
            request,
        );
        return response.data;
    }

    /** Write a single byte through the bus, using the simulated PC. */
    async writeByte(address: number, value: number): Promise<void> {
        if (value < 0 || value > 255) {
            throw new MemoryAccessError("Byte value must be 0-255");
        }
        await this.write(address, Buffer.from([value]));
    }

    /** Write a sequence of bytes through the bus, using the simulated PC. */
    async write(address: number, data: Buffer | Uint8Array): Promise<void> {
        const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
        const request: WriteMemoryRequest = {
            address,
            data: buf,
            simulatedPc: this._pc,
        };
        const response = await promisify<WriteMemoryRequest, WriteMemoryResponse>(
            this._stub as unknown as Record<string, Function>,
            "writeMemory",
            request,
        );
        if (!response.success) {
            throw new MemoryAccessError(
                `Failed to write ${buf.length} bytes at ${hexAddr(address)} with PC=${hexAddr(this._pc)}`,
            );
        }
    }
}

// =============================================================================
// Address Space Accessors (16-bit flat address space)
// =============================================================================

/**
 * Side-effect-free memory access (read-only).
 *
 * Reading I/O addresses through this accessor does not trigger hardware
 * side effects. This is useful for inspecting memory-mapped I/O registers
 * without affecting emulator state.
 */
export class PeekAccessor {
    constructor(private readonly stub: DebuggerControlClient) {}

    /** Read a single byte without side effects. */
    async readByte(address: number): Promise<number> {
        const data = await this.read(address, 1);
        return data[0]!;
    }

    /** Read a sequence of bytes without side effects. */
    async read(address: number, length: number): Promise<Buffer> {
        const request: PeekMemoryRequest = { address, length };
        const response = await promisify<PeekMemoryRequest, PeekMemoryResponse>(
            this.stub as unknown as Record<string, Function>,
            "peekMemory",
            request,
        );
        return response.data;
    }

    /**
     * Save a memory range to a binary file without side effects.
     *
     * @param address - Starting address to save from.
     * @param length - Number of bytes to save.
     * @param filepath - Path to save the binary file.
     */
    async save(address: number, length: number, filepath: string): Promise<void> {
        const data = await this.read(address, length);
        await writeFile(filepath, data);
    }

    /**
     * Return a PC-context reader for B+ shadow RAM routing.
     *
     * On BBC Model B+, memory access to 0x3000-0x7FFF is routed differently
     * depending on where the executing code resides:
     * - MOS code (0xC000-0xDFFF) sees shadow RAM
     * - Paged RAM code (0xA000-0xAFFF when enabled) sees shadow RAM
     * - All other code sees main RAM
     *
     * @param pc - The simulated program counter (0x0000-0xFFFF).
     * @returns A PcContextReader that routes memory access based on the PC.
     */
    withPc(pc: number): PcContextReader {
        return new PcContextReader(this.stub, pc);
    }
}

/**
 * Side-effecting memory access (through memory bus).
 *
 * Reads and writes go through the memory bus exactly like the 6502 CPU
 * would access memory. Reading or writing I/O addresses may trigger
 * hardware side effects.
 */
export class BusAccessor extends PeekAccessor {
    constructor(private readonly _stub: DebuggerControlClient) {
        super(_stub);
    }

    /** Read a single byte through the bus (may trigger side effects). */
    override async readByte(address: number): Promise<number> {
        const data = await this.read(address, 1);
        return data[0]!;
    }

    /** Read a sequence of bytes through the bus (may trigger side effects). */
    override async read(address: number, length: number): Promise<Buffer> {
        const request: ReadMemoryRequest = { address, length };
        const response = await promisify<ReadMemoryRequest, ReadMemoryResponse>(
            this._stub as unknown as Record<string, Function>,
            "readMemory",
            request,
        );
        return response.data;
    }

    /** Write a single byte through the bus. */
    async writeByte(address: number, value: number): Promise<void> {
        if (value < 0 || value > 255) {
            throw new MemoryAccessError("Byte value must be 0-255");
        }
        await this.write(address, Buffer.from([value]));
    }

    /** Write a sequence of bytes through the bus. */
    async write(address: number, data: Buffer | Uint8Array): Promise<void> {
        const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
        const request: WriteMemoryRequest = { address, data: buf };
        const response = await promisify<WriteMemoryRequest, WriteMemoryResponse>(
            this._stub as unknown as Record<string, Function>,
            "writeMemory",
            request,
        );
        if (!response.success) {
            throw new MemoryAccessError(
                `Failed to write ${buf.length} bytes at ${hexAddr(address)}`,
            );
        }
    }

    /**
     * Load a binary file into memory starting at address.
     *
     * @param address - Starting address to load at.
     * @param filepath - Path to the binary file.
     * @param maxLength - Maximum bytes to load (default 64KB).
     * @returns The number of bytes written.
     */
    async load(address: number, filepath: string, maxLength = 0x10000): Promise<number> {
        let data = await readFile(filepath);
        if (data.length > maxLength) {
            data = data.subarray(0, maxLength);
        }
        await this.write(address, data);
        return data.length;
    }

    /**
     * Fill a memory range with a byte value.
     *
     * @param start - Start address (inclusive).
     * @param end - End address (exclusive).
     * @param value - The byte value to fill with (default 0).
     */
    async fill(start: number, end: number, value = 0): Promise<void> {
        const length = end - start;
        if (length <= 0) {
            return;
        }
        if (value < 0 || value > 255) {
            throw new MemoryAccessError("Fill value must be 0-255");
        }
        const buf = Buffer.alloc(length, value);
        await this.write(start, buf);
    }

    /**
     * Save a memory range to a binary file through the bus.
     *
     * Note: this uses bus reads which may trigger side effects. For
     * side-effect-free saves, use the peek accessor instead.
     *
     * @param address - Starting address to save from.
     * @param length - Number of bytes to save.
     * @param filepath - Path to save the binary file.
     */
    override async save(address: number, length: number, filepath: string): Promise<void> {
        const data = await this.read(address, length);
        await writeFile(filepath, data);
    }

    /**
     * Return a PC-context accessor for B+ shadow RAM routing.
     *
     * @param pc - The simulated program counter (0x0000-0xFFFF).
     * @returns A PcContextAccessor that routes memory access based on the PC.
     */
    override withPc(pc: number): PcContextAccessor {
        return new PcContextAccessor(this._stub, pc);
    }
}

// =============================================================================
// Region Accessors (named memory regions with absolute addresses)
// =============================================================================

/**
 * Side-effect-free region access (read-only).
 *
 * Reading from this accessor does not trigger hardware side effects.
 * Addresses are absolute (matching the region's hardware mapping).
 */
export class RegionPeekAccessor {
    constructor(
        private readonly stub: DebuggerControlClient,
        private readonly regionName: string,
    ) {}

    /** Read a single byte from the region without side effects. */
    async readByte(address: number): Promise<number> {
        const data = await this.read(address, 1);
        return data[0]!;
    }

    /** Read a sequence of bytes from the region without side effects. */
    async read(address: number, length: number): Promise<Buffer> {
        const request: RegionAccessRequest = {
            regionName: this.regionName,
            address,
            length,
        };
        const response = await promisify<RegionAccessRequest, RegionAccessResponse>(
            this.stub as unknown as Record<string, Function>,
            "peekRegion",
            request,
        );
        return response.data;
    }

    /**
     * Save a region memory range to a binary file without side effects.
     *
     * @param address - Starting address to save from.
     * @param length - Number of bytes to save.
     * @param filepath - Path to save the binary file.
     */
    async save(address: number, length: number, filepath: string): Promise<void> {
        const data = await this.read(address, length);
        await writeFile(filepath, data);
    }
}

/**
 * Side-effecting region access (through memory bus).
 *
 * Reads and writes go through the region access interface.
 * Addresses are absolute (matching the region's hardware mapping).
 */
export class RegionBusAccessor extends RegionPeekAccessor {
    constructor(
        private readonly _stub: DebuggerControlClient,
        private readonly _regionName: string,
    ) {
        super(_stub, _regionName);
    }

    /** Read a single byte from the region (may trigger side effects). */
    override async readByte(address: number): Promise<number> {
        const data = await this.read(address, 1);
        return data[0]!;
    }

    /** Read a sequence of bytes from the region (may trigger side effects). */
    override async read(address: number, length: number): Promise<Buffer> {
        const request: RegionAccessRequest = {
            regionName: this._regionName,
            address,
            length,
        };
        const response = await promisify<RegionAccessRequest, RegionAccessResponse>(
            this._stub as unknown as Record<string, Function>,
            "readRegion",
            request,
        );
        return response.data;
    }

    /** Write a single byte to the region through the bus. */
    async writeByte(address: number, value: number): Promise<void> {
        if (value < 0 || value > 255) {
            throw new MemoryAccessError("Byte value must be 0-255");
        }
        await this.write(address, Buffer.from([value]));
    }

    /** Write a sequence of bytes to the region through the bus. */
    async write(address: number, data: Buffer | Uint8Array): Promise<void> {
        const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
        const request: WriteRegionRequest = {
            regionName: this._regionName,
            address,
            data: buf,
        };
        const response = await promisify<WriteRegionRequest, WriteRegionResponse>(
            this._stub as unknown as Record<string, Function>,
            "writeRegion",
            request,
        );
        if (!response.success) {
            throw new MemoryAccessError(
                `Failed to write ${buf.length} bytes to region '${this._regionName}' at ${hexAddr(address)}: ${response.error}`,
            );
        }
    }

    /**
     * Load a binary file into region memory starting at address.
     *
     * @param address - Starting address to load at.
     * @param filepath - Path to the binary file.
     * @param maxLength - Maximum bytes to load (default 64KB).
     * @returns The number of bytes written.
     */
    async load(address: number, filepath: string, maxLength = 0x10000): Promise<number> {
        let data = await readFile(filepath);
        if (data.length > maxLength) {
            data = data.subarray(0, maxLength);
        }
        await this.write(address, data);
        return data.length;
    }

    /**
     * Fill a region memory range with a byte value.
     *
     * @param start - Start address (inclusive).
     * @param end - End address (exclusive).
     * @param value - The byte value to fill with (default 0).
     */
    async fill(start: number, end: number, value = 0): Promise<void> {
        const length = end - start;
        if (length <= 0) {
            return;
        }
        if (value < 0 || value > 255) {
            throw new MemoryAccessError("Fill value must be 0-255");
        }
        const buf = Buffer.alloc(length, value);
        await this.write(start, buf);
    }

    /**
     * Save a region memory range to a binary file through the bus.
     *
     * @param address - Starting address to save from.
     * @param length - Number of bytes to save.
     * @param filepath - Path to save the binary file.
     */
    override async save(address: number, length: number, filepath: string): Promise<void> {
        const data = await this.read(address, length);
        await writeFile(filepath, data);
    }
}

// =============================================================================
// Address Space (groups bus + peek)
// =============================================================================

/**
 * 16-bit flat address space access.
 *
 * Provides explicit access modes for reading and writing via addresses:
 * - `bus` - Side-effecting access (through memory bus like real hardware)
 * - `peek` - Side-effect-free access (read-only)
 */
export class AddressSpace {
    private _bus: BusAccessor | undefined;
    private _peek: PeekAccessor | undefined;

    constructor(private readonly stub: DebuggerControlClient) {}

    /** Side-effecting memory access (through memory bus). */
    get bus(): BusAccessor {
        if (this._bus === undefined) {
            this._bus = new BusAccessor(this.stub);
        }
        return this._bus;
    }

    /** Side-effect-free memory access (read-only). */
    get peek(): PeekAccessor {
        if (this._peek === undefined) {
            this._peek = new PeekAccessor(this.stub);
        }
        return this._peek;
    }
}

// =============================================================================
// Region (groups bus + peek for a named region)
// =============================================================================

/**
 * Named memory region access.
 *
 * Provides access to a specific memory region by name. Addresses are
 * absolute, matching the region's hardware mapping (e.g., main_ram uses
 * 0x0000-0x7FFF, mos_rom uses 0xC000-0xFFFF).
 *
 * - `bus` - Side-effecting access
 * - `peek` - Side-effect-free access (read-only)
 */
export class Region {
    private _bus: RegionBusAccessor | undefined;
    private _peek: RegionPeekAccessor | undefined;

    constructor(
        private readonly stub: DebuggerControlClient,
        readonly name: string,
    ) {}

    /** Side-effecting region access. */
    get bus(): RegionBusAccessor {
        if (this._bus === undefined) {
            this._bus = new RegionBusAccessor(this.stub, this.name);
        }
        return this._bus;
    }

    /** Side-effect-free region access (read-only). */
    get peek(): RegionPeekAccessor {
        if (this._peek === undefined) {
            this._peek = new RegionPeekAccessor(this.stub, this.name);
        }
        return this._peek;
    }
}

// =============================================================================
// Memory - Top-level memory access interface
// =============================================================================

/**
 * Memory access namespace.
 *
 * Provides access to emulator memory through two interfaces:
 *
 * Address space (16-bit flat address):
 *   await bbc.memory.address.bus.readByte(0x1000)           // Read with side effects
 *   await bbc.memory.address.peek.readByte(0xFE4D)          // Read without side effects
 *   await bbc.memory.address.bus.writeByte(0x1000, 0x42)    // Write through bus
 *
 * Named regions (absolute addresses within region):
 *   await bbc.memory.region("main_ram").bus.readByte(0x1234)       // Read from main RAM
 *   await bbc.memory.region("shadow_ram").peek.readByte(0x3000)    // Peek shadow RAM
 *
 * File I/O and utilities (on accessors):
 *   await bbc.memory.address.bus.load(0x1000, "file.bin")     // Load file
 *   await bbc.memory.address.peek.save(0x1000, 100, "out.bin")  // Save range
 *   await bbc.memory.address.bus.fill(0x1000, 0x2000, 0)     // Fill with zeros
 *
 * Discovery:
 *   await bbc.memory.getRegions()     // List of MemoryRegionInfo
 *   await bbc.memory.getMachineType() // "ModelB", "ModelBPlus", etc.
 */
export class Memory {
    private _address: AddressSpace | undefined;
    private _regionsCache: MemoryRegionInfo[] | undefined;
    private _machineTypeCache: string | undefined;

    constructor(private readonly stub: DebuggerControlClient) {}

    /**
     * 16-bit flat address space access.
     *
     * Returns an AddressSpace with `.bus` and `.peek` accessors.
     */
    get address(): AddressSpace {
        if (this._address === undefined) {
            this._address = new AddressSpace(this.stub);
        }
        return this._address;
    }

    /**
     * Access a named memory region.
     *
     * Addresses within a region are absolute, matching the region's
     * hardware mapping. For example:
     * - main_ram: 0x0000-0x7FFF
     * - mos_rom: 0xC000-0xFFFF
     * - bank_0 through bank_15: 0x8000-0xBFFF
     *
     * @param name - The region name (e.g., "main_ram", "shadow_ram", "bank_0").
     * @returns A Region object with `.bus` and `.peek` accessors.
     */
    region(name: string): Region {
        return new Region(this.stub, name);
    }

    /**
     * List of available memory regions.
     *
     * Returns information about all memory regions available for the
     * current machine type. The result is cached after the first call.
     */
    async getRegions(): Promise<MemoryRegionInfo[]> {
        if (this._regionsCache === undefined) {
            await this.fetchRegions();
        }
        return this._regionsCache!;
    }

    /**
     * Machine type string.
     *
     * Returns the machine type, e.g., "ModelB" or "ModelBPlus".
     * The result is cached after the first call.
     */
    async getMachineType(): Promise<string> {
        if (this._machineTypeCache === undefined) {
            await this.fetchRegions();
        }
        return this._machineTypeCache!;
    }

    /** Fetch region info from the server and populate both caches. */
    private async fetchRegions(): Promise<void> {
        const request: GetMemoryRegionsRequest = {};
        const response = await promisify<GetMemoryRegionsRequest, GetMemoryRegionsResponse>(
            this.stub as unknown as Record<string, Function>,
            "getMemoryRegions",
            request,
        );
        this._machineTypeCache = response.machineType;
        this._regionsCache = response.regions.map(
            (r: ProtoMemoryRegionInfo): MemoryRegionInfo => ({
                name: r.name,
                baseAddress: r.baseAddress,
                size: r.size,
                readable: r.readable,
                writable: r.writable,
                hasSideEffects: r.hasSideEffects,
                populated: r.populated,
                active: r.active,
            }),
        );
    }
}
