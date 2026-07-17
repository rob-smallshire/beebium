/**
 * CPU register access interface for the Beebium TypeScript client.
 *
 * Provides reading and writing of 6502 CPU registers.
 */

import type { DebuggerControlClient } from "./generated/debugger.js";
import type { Cpu6502State } from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

export interface Registers {
    a: number;
    x: number;
    y: number;
    sp: number;
    pc: number;
    p: number;
    inNmiHandler: boolean;
    inIrqHandler: boolean;
    nmiPending: boolean;
    irqPending: boolean;
    deviceIrqFlags: number;
    deviceNmiFlags: number;
}

// Flag accessor functions

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

/** Format registers as a human-readable string. */
export function formatRegisters(r: Registers): string {
    const p = r.p;
    const flags =
        (negative(p) ? "N" : "n") +
        (overflow(p) ? "V" : "v") +
        "-" +
        (breakFlag(p) ? "B" : "b") +
        (decimal(p) ? "D" : "d") +
        (interruptDisable(p) ? "I" : "i") +
        (zero(p) ? "Z" : "z") +
        (carry(p) ? "C" : "c");

    return (
        `A=${r.a.toString(16).toUpperCase().padStart(2, "0")} ` +
        `X=${r.x.toString(16).toUpperCase().padStart(2, "0")} ` +
        `Y=${r.y.toString(16).toUpperCase().padStart(2, "0")} ` +
        `SP=${r.sp.toString(16).toUpperCase().padStart(2, "0")} ` +
        `PC=${r.pc.toString(16).toUpperCase().padStart(4, "0")} ` +
        `P=${r.p.toString(16).toUpperCase().padStart(2, "0")} [${flags}]`
    );
}

function toRegisters(proto: Cpu6502State): Registers {
    return {
        a: proto.a,
        x: proto.x,
        y: proto.y,
        sp: proto.sp,
        pc: proto.pc,
        p: proto.p,
        inNmiHandler: proto.inNmiHandler,
        inIrqHandler: proto.inIrqHandler,
        nmiPending: proto.nmiPending,
        irqPending: proto.irqPending,
        deviceIrqFlags: proto.deviceIrqFlags,
        deviceNmiFlags: proto.deviceNmiFlags,
    };
}

/**
 * CPU register access interface.
 *
 * Provides reading and writing of 6502 CPU registers.
 */
export class CPU {
    private readonly stub: DebuggerControlClient;

    constructor(stub: DebuggerControlClient) {
        this.stub = stub;
    }

    /** Get all CPU registers. */
    async getRegisters(): Promise<Registers> {
        const response = await promisify<{}, Cpu6502State>(
            this.stub as unknown as Record<string, Function>,
            "get6502State",
            {},
        );
        return toRegisters(response);
    }

    /** Get the accumulator register. */
    async getA(): Promise<number> {
        return (await this.getRegisters()).a;
    }

    /** Get the X index register. */
    async getX(): Promise<number> {
        return (await this.getRegisters()).x;
    }

    /** Get the Y index register. */
    async getY(): Promise<number> {
        return (await this.getRegisters()).y;
    }

    /** Get the stack pointer. */
    async getSp(): Promise<number> {
        return (await this.getRegisters()).sp;
    }

    /** Get the program counter. */
    async getPc(): Promise<number> {
        return (await this.getRegisters()).pc;
    }

    /** Get the processor status register. */
    async getP(): Promise<number> {
        return (await this.getRegisters()).p;
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
     * Set one or more CPU registers. Only specified fields are updated.
     *
     * Returns the complete new register state, read back atomically by the
     * server after applying the write (in the same request, no separate
     * fetch), so it reflects the write with no race against a running CPU.
     */
    async setRegisters(regs: Partial<Registers>): Promise<Registers> {
        const request: Record<string, number | undefined> = {};
        if (regs.a !== undefined) request.a = regs.a;
        if (regs.x !== undefined) request.x = regs.x;
        if (regs.y !== undefined) request.y = regs.y;
        if (regs.sp !== undefined) request.sp = regs.sp;
        if (regs.pc !== undefined) request.pc = regs.pc;
        if (regs.p !== undefined) request.p = regs.p;

        const response = await promisify<Record<string, number | undefined>, Cpu6502State>(
            this.stub as unknown as Record<string, Function>,
            "set6502State",
            request,
        );
        return toRegisters(response);
    }
}
