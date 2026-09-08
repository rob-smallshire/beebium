/**
 * CPU register access for the Beebium TypeScript client.
 *
 * The CPU describes its own registers and interrupt signals, so this layer
 * names no CPU family. Registers is an ordered map of register name to value;
 * existing 6502 code reading regs.a / regs.pc keeps working, and another
 * family's registers (a Z80's regs.hl) appear with no client change. The flag
 * helpers and formatRegisters stay 6502-specific, like the disassembler.
 */

import type {
    DebuggerControlClient,
    CpuDescriptor,
    CpuState,
} from "./generated/debugger.js";
import { RegisterRole } from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

/**
 * A snapshot of the CPU registers: register name (lowercased, e.g. "a", "pc",
 * "hl") to value. Reachable by property (regs.pc) or index (regs["pc"]).
 */
export type Registers = { readonly [name: string]: number };

/** The state of one CPU interrupt line, e.g. IRQ or NMI. */
export interface Signal {
    name: string;
    asserted: boolean;
    pending: boolean;
    inHandler: boolean;
}

// Flag accessor functions (6502 status register layout).

/** Carry flag (bit 0). */
export function carry(p: number): boolean {
    return !!(p & 0x01);
}

/** Zero flag (bit 1). */
export function zero(p: number): boolean {
    return !!(p & 0x02);
}

/** Interrupt disable flag (bit 2). */
export function interruptDisable(p: number): boolean {
    return !!(p & 0x04);
}

/** Decimal mode flag (bit 3). */
export function decimal(p: number): boolean {
    return !!(p & 0x08);
}

/** Break flag (bit 4). */
export function breakFlag(p: number): boolean {
    return !!(p & 0x10);
}

/** Overflow flag (bit 6). */
export function overflow(p: number): boolean {
    return !!(p & 0x40);
}

/** Negative flag (bit 7). */
export function negative(p: number): boolean {
    return !!(p & 0x80);
}

/**
 * A CPU flags register decoded by name, built from the descriptor's flag
 * names (bit 0 first, "" for an unused bit). Flags are read by name --
 * status.flag("C") -- and rendered by name. For a 6502 the familiar names
 * (carry, zero, interruptDisable, decimal, breakFlag, overflow, negative) are
 * available as getters, but only when the descriptor carries the matching
 * flag; on a CPU whose flags use other names they throw rather than mislead.
 */
export class StatusRegister {
    readonly value: number;
    readonly flagNames: readonly string[];
    private readonly bitOf: Map<string, number>;

    // 6502 getter name -> descriptor flag name.
    private static readonly ALIASES: Record<string, string> = {
        carry: "C",
        zero: "Z",
        interruptDisable: "I",
        decimal: "D",
        breakFlag: "B",
        overflow: "V",
        negative: "N",
    };

    constructor(value: number, flagNames: readonly string[]) {
        this.value = value;
        this.flagNames = flagNames;
        this.bitOf = new Map();
        flagNames.forEach((name, i) => {
            if (name) this.bitOf.set(name, i);
        });
    }

    /** The flag of the given descriptor name (e.g. "C", "N"). Throws if absent. */
    flag(name: string): boolean {
        const bit = this.bitOf.get(name);
        if (bit === undefined) {
            throw new Error(`no flag ${name}`);
        }
        return !!(this.value & (1 << bit));
    }

    /** Whether this flags register has a flag of the given name. */
    has(name: string): boolean {
        return this.bitOf.has(name);
    }

    private alias(getter: string): boolean {
        return this.flag(StatusRegister.ALIASES[getter]!);
    }

    get carry(): boolean {
        return this.alias("carry");
    }
    get zero(): boolean {
        return this.alias("zero");
    }
    get interruptDisable(): boolean {
        return this.alias("interruptDisable");
    }
    get decimal(): boolean {
        return this.alias("decimal");
    }
    get breakFlag(): boolean {
        return this.alias("breakFlag");
    }
    get overflow(): boolean {
        return this.alias("overflow");
    }
    get negative(): boolean {
        return this.alias("negative");
    }

    /** Render flags by name, MSB first: set uppercase, clear lowercase, "-" unused. */
    toString(): string {
        let s = "";
        for (let bit = this.flagNames.length - 1; bit >= 0; bit--) {
            const name = this.flagNames[bit]!;
            if (!name) {
                s += "-";
            } else if (this.value & (1 << bit)) {
                s += name.toUpperCase();
            } else {
                s += name.toLowerCase();
            }
        }
        return s;
    }
}

/** Format the 6502 registers as a human-readable string. */
export function formatRegisters(r: Registers): string {
    const p = r.p ?? 0;
    const flags =
        (negative(p) ? "N" : "n") +
        (overflow(p) ? "V" : "v") +
        "-" +
        (breakFlag(p) ? "B" : "b") +
        (decimal(p) ? "D" : "d") +
        (interruptDisable(p) ? "I" : "i") +
        (zero(p) ? "Z" : "z") +
        (carry(p) ? "C" : "c");

    const hex = (v: number | undefined, width: number): string =>
        (v ?? 0).toString(16).toUpperCase().padStart(width, "0");
    return (
        `A=${hex(r.a, 2)} ` +
        `X=${hex(r.x, 2)} ` +
        `Y=${hex(r.y, 2)} ` +
        `SP=${hex(r.sp, 2)} ` +
        `PC=${hex(r.pc, 4)} ` +
        `P=${hex(r.p, 2)} [${flags}]`
    );
}

function toRegisters(state: CpuState): Registers {
    const regs: Record<string, number> = {};
    for (const rv of state.registers) {
        regs[rv.name.toLowerCase()] = rv.value;
    }
    return regs;
}

function toSignals(state: CpuState): Record<string, Signal> {
    const signals: Record<string, Signal> = {};
    for (const ss of state.signals) {
        signals[ss.name] = {
            name: ss.name,
            asserted: ss.asserted,
            pending: ss.pending,
            inHandler: ss.inHandler,
        };
    }
    return signals;
}

/**
 * CPU register access.
 *
 * Reads return a coherent snapshot; writes are atomic and return the resulting
 * snapshot.
 */
export class CPU {
    private readonly stub: DebuggerControlClient;
    private descriptorCache: CpuDescriptor | undefined;

    constructor(stub: DebuggerControlClient) {
        this.stub = stub;
    }

    /** The CPU's self-description (registers in display order, and signals). */
    async getDescriptor(): Promise<CpuDescriptor> {
        if (this.descriptorCache === undefined) {
            this.descriptorCache = await promisify<{}, CpuDescriptor>(
                this.stub as unknown as Record<string, Function>,
                "getCpuDescriptor",
                {},
            );
        }
        return this.descriptorCache;
    }

    /** Get all CPU registers as one coherent snapshot. */
    async getRegisters(): Promise<Registers> {
        const response = await promisify<{}, CpuState>(
            this.stub as unknown as Record<string, Function>,
            "getCpuState",
            {},
        );
        return toRegisters(response);
    }

    /** The CPU's interrupt lines and their state, keyed by name. */
    async getSignals(): Promise<Record<string, Signal>> {
        const response = await promisify<{}, CpuState>(
            this.stub as unknown as Record<string, Function>,
            "getCpuState",
            {},
        );
        return toSignals(response);
    }

    /** Read the flags register, decoded by the descriptor's flag names. */
    async getStatus(): Promise<StatusRegister> {
        const descriptor = await this.getDescriptor();
        const flagsReg = descriptor.registers.find((r) => r.role === RegisterRole.FLAGS);
        if (flagsReg === undefined) {
            throw new Error("this CPU has no flags register");
        }
        const regs = await this.getRegisters();
        const value = regs[flagsReg.name.toLowerCase()] ?? 0;
        return new StatusRegister(value, flagsReg.flagNames);
    }

    /** Get the accumulator register. */
    async getA(): Promise<number> {
        return (await this.getRegisters()).a ?? 0;
    }

    /** Get the X index register. */
    async getX(): Promise<number> {
        return (await this.getRegisters()).x ?? 0;
    }

    /** Get the Y index register. */
    async getY(): Promise<number> {
        return (await this.getRegisters()).y ?? 0;
    }

    /** Get the stack pointer. */
    async getSp(): Promise<number> {
        return (await this.getRegisters()).sp ?? 0;
    }

    /** Get the program counter. */
    async getPc(): Promise<number> {
        return (await this.getRegisters()).pc ?? 0;
    }

    /** Get the processor status register. */
    async getP(): Promise<number> {
        return (await this.getRegisters()).p ?? 0;
    }

    /** Set the accumulator register. */
    async setA(value: number): Promise<void> {
        await this.setRegisters({ a: value });
    }

    /** Set the X index register. */
    async setX(value: number): Promise<void> {
        await this.setRegisters({ x: value });
    }

    /** Set the Y index register. */
    async setY(value: number): Promise<void> {
        await this.setRegisters({ y: value });
    }

    /** Set the stack pointer. */
    async setSp(value: number): Promise<void> {
        await this.setRegisters({ sp: value });
    }

    /** Set the program counter. */
    async setPc(value: number): Promise<void> {
        await this.setRegisters({ pc: value });
    }

    /** Set the processor status register. */
    async setP(value: number): Promise<void> {
        await this.setRegisters({ p: value });
    }

    /**
     * Set one or more CPU registers, named as lowercase keys. Only the given
     * registers are updated.
     *
     * Returns the complete new register state, read back atomically by the
     * server after applying the write (in the same request, no separate
     * fetch), so it reflects the write with no race against a running CPU. An
     * unknown register name is rejected by the server, naming it.
     */
    async setRegisters(values: Record<string, number>): Promise<Registers> {
        const descriptor = await this.getDescriptor();
        const byLower = new Map<string, string>();
        for (const reg of descriptor.registers) {
            byLower.set(reg.name.toLowerCase(), reg.name);
        }
        const request: CpuState = { registers: [], signals: [], cycleCount: 0 };
        for (const [key, value] of Object.entries(values)) {
            request.registers.push({
                name: byLower.get(key.toLowerCase()) ?? key,
                value,
            });
        }
        const response = await promisify<CpuState, CpuState>(
            this.stub as unknown as Record<string, Function>,
            "setCpuState",
            request,
        );
        return toRegisters(response);
    }
}

// RegisterRole re-exported for callers that inspect the descriptor.
export { RegisterRole };
