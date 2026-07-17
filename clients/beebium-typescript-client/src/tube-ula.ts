/**
 * Tube ULA device inspection for the Beebium TypeScript client.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    TubeState as ProtoTubeState,
    TubeControlFlags as ProtoTubeControlFlags,
    TubeLatchState as ProtoTubeLatchState,
    TubeFifo24State as ProtoTubeFifo24State,
    TubeReg3State as ProtoTubeReg3State,
    TubeHostStatus as ProtoTubeHostStatus,
    TubeParasiteStatus as ProtoTubeParasiteStatus,
    TubeInterrupts as ProtoTubeInterrupts,
    TubeTransferCounters as ProtoTubeTransferCounters,
    GetTubeStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

// -- Interfaces ---------------------------------------------------------------

export interface TubeControlFlags {
    /** Bit 0: Enable HIRQ from R4 */
    q: boolean;
    /** Bit 1: Enable PIRQ from R1 */
    i: boolean;
    /** Bit 2: Enable PIRQ from R4 */
    j: boolean;
    /** Bit 3: Enable PNMI from R3 */
    m: boolean;
    /** Bit 4: Two-byte mode for R3 */
    v: boolean;
    /** Bit 5: Parasite reset */
    p: boolean;
}

export interface TubeLatchState {
    /** Current data byte in the latch */
    value: number;
    /** Latch contains unread data */
    dataAvailable: boolean;
}

export interface TubeFifo24State {
    /** Number of bytes currently in the FIFO (0-24) */
    count: number;
    /** FIFO contents in read order (head to tail) */
    data: Uint8Array;
    /** count > 0 */
    dataAvailable: boolean;
}

export interface TubeReg3State {
    /** Number of bytes in the register (0-2) */
    count: number;
    /** Register contents in read order (up to 2 bytes) */
    data: Uint8Array;
    /** Complete transfer awaiting consumption */
    pending: boolean;
    /** Current threshold (1 or 2, from V flag) */
    threshold: number;
}

export interface TubeHostStatus {
    /** Offset 0: control flags + R1 status bits */
    r1Status: number;
    /** Offset 2: R2 status bits */
    r2Status: number;
    /** Offset 4: R3 status bits */
    r3Status: number;
    /** Offset 6: R4 status bits */
    r4Status: number;
}

export interface TubeParasiteStatus {
    /** Offset 0: control flags + R1 status bits */
    r1Status: number;
    /** Offset 2: R2 status bits */
    r2Status: number;
    /** Offset 4: R3 status bits (includes N flag at bit 5) */
    r3Status: number;
    /** Offset 6: R4 status bits */
    r4Status: number;
}

export interface TubeInterrupts {
    /** Host IRQ: Q=1 AND R4 P-to-H has data */
    hirq: boolean;
    /** Parasite IRQ: (I=1 AND R1 H-to-P has data) OR (J=1 AND R4 H-to-P has data) */
    pirq: boolean;
    /** PNMI level (combinational, before edge detection) */
    pnmiLevel: boolean;
    /** PNMI latched rising edge */
    pnmiEdge: boolean;
}

export interface TubeTransferCounters {
    /** Host-to-parasite direction */
    r1H2pWrites: number;
    r1H2pReads: number;
    r2H2pWrites: number;
    r2H2pReads: number;
    r3H2pWrites: number;
    r3H2pReads: number;
    r4H2pWrites: number;
    r4H2pReads: number;
    /** Parasite-to-host direction */
    r1P2hWrites: number;
    r1P2hReads: number;
    r2P2hWrites: number;
    r2P2hReads: number;
    r3P2hWrites: number;
    r3P2hReads: number;
    r4P2hWrites: number;
    r4P2hReads: number;
}

export interface TubeUlaState {
    /** Whether the Tube socket is populated */
    enabled: boolean;
    /** Control flags (6 bits, written by host) */
    controlFlags: TubeControlFlags;
    /** Register 1: H-to-P is a 1-byte latch */
    r1H2p: TubeLatchState;
    /** Register 1: P-to-H is a 24-byte FIFO */
    r1P2h: TubeFifo24State;
    /** Register 2: 1-byte latch in each direction */
    r2H2p: TubeLatchState;
    r2P2h: TubeLatchState;
    /** Register 3: 2-byte shift register in each direction */
    r3H2p: TubeReg3State;
    r3P2h: TubeReg3State;
    /** Register 4: 1-byte latch in each direction */
    r4H2p: TubeLatchState;
    r4P2h: TubeLatchState;
    /** Status registers (as read by host) */
    hostStatus: TubeHostStatus;
    /** Status registers (as read by parasite) */
    parasiteStatus: TubeParasiteStatus;
    /** Interrupt outputs */
    interrupts: TubeInterrupts;
    /** Bus stretching state */
    hostStretched: boolean;
    /** Per-register transfer counters */
    counters: TubeTransferCounters;
}

// -- Formatting helpers -------------------------------------------------------

function formatControlFlags(f: TubeControlFlags): string {
    const raw =
        (f.q ? 1 : 0) |
        (f.i ? 2 : 0) |
        (f.j ? 4 : 0) |
        (f.m ? 8 : 0) |
        (f.v ? 16 : 0) |
        (f.p ? 32 : 0);
    const flags: string[] = [];
    if (f.q) flags.push("Q");
    if (f.i) flags.push("I");
    if (f.j) flags.push("J");
    if (f.m) flags.push("M");
    if (f.v) flags.push("V");
    if (f.p) flags.push("P");
    return `Flags=${raw.toString(16).toUpperCase().padStart(2, "0")} [${flags.join(" ") || "none"}]`;
}

function formatLatch(l: TubeLatchState): string {
    const avail = l.dataAvailable ? "AVAIL" : "empty";
    return `$${l.value.toString(16).toUpperCase().padStart(2, "0")} (${avail})`;
}

function formatFifo24(f: TubeFifo24State): string {
    const avail = f.dataAvailable ? "AVAIL" : "empty";
    const bytes = Array.from(f.data.slice(0, 8));
    let hexData = bytes.map(b => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
    if (f.data.length > 8) {
        hexData += " ...";
    }
    return `count=${f.count}/24 (${avail}) [${hexData}]`;
}

function formatReg3(r: TubeReg3State): string {
    const mode = r.threshold === 2 ? "2-byte" : "1-byte";
    const pend = r.pending ? "PENDING" : "idle";
    const hexData = Array.from(r.data).map(b => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
    return `count=${r.count}/${r.threshold} (${pend}, ${mode}) [${hexData}]`;
}

function formatStatus(label: string, s: TubeHostStatus | TubeParasiteStatus): string {
    const hex2 = (n: number) => n.toString(16).toUpperCase().padStart(2, "0");
    return `${label}: R1=$${hex2(s.r1Status)} R2=$${hex2(s.r2Status)} R3=$${hex2(s.r3Status)} R4=$${hex2(s.r4Status)}`;
}

function formatInterrupts(intr: TubeInterrupts): string {
    const active: string[] = [];
    if (intr.hirq) active.push("HIRQ");
    if (intr.pirq) active.push("PIRQ");
    if (intr.pnmiLevel) active.push("PNMI-level");
    if (intr.pnmiEdge) active.push("PNMI-edge");
    return active.join(" ") || "none";
}

function formatCounters(c: TubeTransferCounters): string {
    const lines = ["Transfer counters:"];
    const regs = [
        { reg: 1, h2pW: c.r1H2pWrites, h2pR: c.r1H2pReads, p2hW: c.r1P2hWrites, p2hR: c.r1P2hReads },
        { reg: 2, h2pW: c.r2H2pWrites, h2pR: c.r2H2pReads, p2hW: c.r2P2hWrites, p2hR: c.r2P2hReads },
        { reg: 3, h2pW: c.r3H2pWrites, h2pR: c.r3H2pReads, p2hW: c.r3P2hWrites, p2hR: c.r3P2hReads },
        { reg: 4, h2pW: c.r4H2pWrites, h2pR: c.r4H2pReads, p2hW: c.r4P2hWrites, p2hR: c.r4P2hReads },
    ];
    for (const r of regs) {
        const h2pDelta = r.h2pW - r.h2pR;
        const p2hDelta = r.p2hW - r.p2hR;
        lines.push(
            `  R${r.reg}: H->P w=${r.h2pW} r=${r.h2pR} (delta=${h2pDelta})` +
            `  P->H w=${r.p2hW} r=${r.p2hR} (delta=${p2hDelta})`
        );
    }
    return lines.join("\n");
}

/** Format a complete Tube ULA state snapshot for display. */
export function formatTubeUlaState(s: TubeUlaState): string {
    if (!s.enabled) {
        return "Tube: not enabled";
    }
    const lines = [
        `Tube ULA: ${formatControlFlags(s.controlFlags)}`,
        `  R1 H->P: ${formatLatch(s.r1H2p)}`,
        `  R1 P->H: ${formatFifo24(s.r1P2h)}`,
        `  R2 H->P: ${formatLatch(s.r2H2p)}`,
        `  R2 P->H: ${formatLatch(s.r2P2h)}`,
        `  R3 H->P: ${formatReg3(s.r3H2p)}`,
        `  R3 P->H: ${formatReg3(s.r3P2h)}`,
        `  R4 H->P: ${formatLatch(s.r4H2p)}`,
        `  R4 P->H: ${formatLatch(s.r4P2h)}`,
        `  ${formatStatus("Host status    ", s.hostStatus)}`,
        `  ${formatStatus("Parasite status", s.parasiteStatus)}`,
        `  Interrupts: ${formatInterrupts(s.interrupts)}`,
    ];
    if (s.hostStretched) {
        lines.push("  Host: STRETCHED");
    }
    lines.push(`  ${formatCounters(s.counters)}`);
    return lines.join("\n");
}

// -- Conversion ---------------------------------------------------------------

const DEFAULT_CONTROL_FLAGS: TubeControlFlags = {
    q: false, i: false, j: false, m: false, v: false, p: false,
};

const DEFAULT_LATCH: TubeLatchState = { value: 0, dataAvailable: false };

const DEFAULT_FIFO24: TubeFifo24State = { count: 0, data: new Uint8Array(0), dataAvailable: false };

const DEFAULT_REG3: TubeReg3State = { count: 0, data: new Uint8Array(0), pending: false, threshold: 1 };

const DEFAULT_HOST_STATUS: TubeHostStatus = { r1Status: 0, r2Status: 0, r3Status: 0, r4Status: 0 };

const DEFAULT_PARASITE_STATUS: TubeParasiteStatus = { r1Status: 0, r2Status: 0, r3Status: 0, r4Status: 0 };

const DEFAULT_INTERRUPTS: TubeInterrupts = { hirq: false, pirq: false, pnmiLevel: false, pnmiEdge: false };

const DEFAULT_COUNTERS: TubeTransferCounters = {
    r1H2pWrites: 0, r1H2pReads: 0,
    r2H2pWrites: 0, r2H2pReads: 0,
    r3H2pWrites: 0, r3H2pReads: 0,
    r4H2pWrites: 0, r4H2pReads: 0,
    r1P2hWrites: 0, r1P2hReads: 0,
    r2P2hWrites: 0, r2P2hReads: 0,
    r3P2hWrites: 0, r3P2hReads: 0,
    r4P2hWrites: 0, r4P2hReads: 0,
};

function toControlFlags(proto: ProtoTubeControlFlags | undefined): TubeControlFlags {
    if (!proto) return { ...DEFAULT_CONTROL_FLAGS };
    return { q: proto.q, i: proto.i, j: proto.j, m: proto.m, v: proto.v, p: proto.p };
}

function toLatchState(proto: ProtoTubeLatchState | undefined): TubeLatchState {
    if (!proto) return { ...DEFAULT_LATCH };
    return { value: proto.value, dataAvailable: proto.dataAvailable };
}

function toFifo24State(proto: ProtoTubeFifo24State | undefined): TubeFifo24State {
    if (!proto) return { ...DEFAULT_FIFO24 };
    return {
        count: proto.count,
        data: proto.data instanceof Uint8Array ? proto.data : new Uint8Array(proto.data),
        dataAvailable: proto.dataAvailable,
    };
}

function toReg3State(proto: ProtoTubeReg3State | undefined): TubeReg3State {
    if (!proto) return { ...DEFAULT_REG3 };
    return {
        count: proto.count,
        data: proto.data instanceof Uint8Array ? proto.data : new Uint8Array(proto.data),
        pending: proto.pending,
        threshold: proto.threshold,
    };
}

function toHostStatus(proto: ProtoTubeHostStatus | undefined): TubeHostStatus {
    if (!proto) return { ...DEFAULT_HOST_STATUS };
    return {
        r1Status: proto.r1Status,
        r2Status: proto.r2Status,
        r3Status: proto.r3Status,
        r4Status: proto.r4Status,
    };
}

function toParasiteStatus(proto: ProtoTubeParasiteStatus | undefined): TubeParasiteStatus {
    if (!proto) return { ...DEFAULT_PARASITE_STATUS };
    return {
        r1Status: proto.r1Status,
        r2Status: proto.r2Status,
        r3Status: proto.r3Status,
        r4Status: proto.r4Status,
    };
}

function toInterrupts(proto: ProtoTubeInterrupts | undefined): TubeInterrupts {
    if (!proto) return { ...DEFAULT_INTERRUPTS };
    return {
        hirq: proto.hirq,
        pirq: proto.pirq,
        pnmiLevel: proto.pnmiLevel,
        pnmiEdge: proto.pnmiEdge,
    };
}

function toTransferCounters(proto: ProtoTubeTransferCounters | undefined): TubeTransferCounters {
    if (!proto) return { ...DEFAULT_COUNTERS };
    return {
        r1H2pWrites: proto.r1H2pWrites,
        r1H2pReads: proto.r1H2pReads,
        r2H2pWrites: proto.r2H2pWrites,
        r2H2pReads: proto.r2H2pReads,
        r3H2pWrites: proto.r3H2pWrites,
        r3H2pReads: proto.r3H2pReads,
        r4H2pWrites: proto.r4H2pWrites,
        r4H2pReads: proto.r4H2pReads,
        r1P2hWrites: proto.r1P2hWrites,
        r1P2hReads: proto.r1P2hReads,
        r2P2hWrites: proto.r2P2hWrites,
        r2P2hReads: proto.r2P2hReads,
        r3P2hWrites: proto.r3P2hWrites,
        r3P2hReads: proto.r3P2hReads,
        r4P2hWrites: proto.r4P2hWrites,
        r4P2hReads: proto.r4P2hReads,
    };
}

function toTubeUlaState(proto: ProtoTubeState): TubeUlaState {
    return {
        enabled: proto.enabled,
        controlFlags: toControlFlags(proto.controlFlags),
        r1H2p: toLatchState(proto.r1H2p),
        r1P2h: toFifo24State(proto.r1P2h),
        r2H2p: toLatchState(proto.r2H2p),
        r2P2h: toLatchState(proto.r2P2h),
        r3H2p: toReg3State(proto.r3H2p),
        r3P2h: toReg3State(proto.r3P2h),
        r4H2p: toLatchState(proto.r4H2p),
        r4P2h: toLatchState(proto.r4P2h),
        hostStatus: toHostStatus(proto.hostStatus),
        parasiteStatus: toParasiteStatus(proto.parasiteStatus),
        interrupts: toInterrupts(proto.interrupts),
        hostStretched: proto.hostStretched,
        counters: toTransferCounters(proto.counters),
    };
}

// -- Class --------------------------------------------------------------------

/**
 * Tube ULA device inspection.
 *
 * Provides side-effect-free access to Tube ULA register state for debugging.
 */
export class TubeUlaInspection {
    private readonly stub: DeviceInspectionClient;

    constructor(stub: DeviceInspectionClient) {
        this.stub = stub;
    }

    /** Get the current Tube ULA state. */
    async getState(): Promise<TubeUlaState> {
        const response = await promisify<GetTubeStateRequest, ProtoTubeState>(
            this.stub as unknown as Record<string, Function>,
            "getTubeState",
            {},
        );
        return toTubeUlaState(response);
    }
}
