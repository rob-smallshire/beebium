/**
 * 6522 VIA (Versatile Interface Adapter) access for the Beebium TypeScript client.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    ViaState as ProtoViaState,
    GetSystemViaStateRequest,
    GetUserViaStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

export enum ViaId {
    SYSTEM = "system",
    USER = "user",
}

export interface ViaState {
    /** Output Register A */
    ora: number;
    /** Output Register B */
    orb: number;
    /** Data Direction Register A */
    ddra: number;
    /** Data Direction Register B */
    ddrb: number;
    /** Input Register A (computed from port pins) */
    ira: number;
    /** Input Register B (computed from port pins) */
    irb: number;

    /** Timer 1 counter (16-bit effective value) */
    t1c: number;
    /** Timer 1 latch (16-bit) */
    t1l: number;
    /** Timer 2 counter (16-bit effective value) */
    t2c: number;
    /** Timer 2 latch (8-bit, low byte only) */
    t2l: number;

    /** Auxiliary Control Register */
    acr: number;
    /** Peripheral Control Register */
    pcr: number;
    /** Shift Register */
    sr: number;

    /** Interrupt Flag Register */
    ifr: number;
    /** Interrupt Enable Register */
    ier: number;

    /** Timer 1 active and can generate IRQ */
    t1Pending: boolean;
    /** Timer 2 active and can generate IRQ */
    t2Pending: boolean;
    /** PB7 output state from Timer 1 */
    t1Pb7: number;

    /** Control lines */
    ca1: boolean;
    ca2: boolean;
    cb1: boolean;
    cb2: boolean;
}

// -- ACR flag helpers ---------------------------------------------------------

/** Port A input latching enabled on CA1 edge. */
export function paLatching(acr: number): boolean { return !!(acr & 0x01); }

/** Port B input latching enabled on CB1 edge. */
export function pbLatching(acr: number): boolean { return !!(acr & 0x02); }

/** Timer 2 counts PB6 pulses (else counts phi2). */
export function t2CountPb6(acr: number): boolean { return !!(acr & 0x20); }

/** Timer 1 free-running mode (else one-shot). */
export function t1Continuous(acr: number): boolean { return !!(acr & 0x40); }

/** Timer 1 toggles PB7 output on timeout. */
export function t1OutputPb7(acr: number): boolean { return !!(acr & 0x80); }

// -- IFR flag helpers ---------------------------------------------------------

/** CA2 interrupt flag. */
export function ifrCa2(ifr: number): boolean { return !!(ifr & 0x01); }

/** CA1 interrupt flag. */
export function ifrCa1(ifr: number): boolean { return !!(ifr & 0x02); }

/** Shift register interrupt flag. */
export function ifrSr(ifr: number): boolean { return !!(ifr & 0x04); }

/** CB2 interrupt flag. */
export function ifrCb2(ifr: number): boolean { return !!(ifr & 0x08); }

/** CB1 interrupt flag. */
export function ifrCb1(ifr: number): boolean { return !!(ifr & 0x10); }

/** Timer 2 interrupt flag. */
export function ifrT2(ifr: number): boolean { return !!(ifr & 0x20); }

/** Timer 1 interrupt flag. */
export function ifrT1(ifr: number): boolean { return !!(ifr & 0x40); }

/** Master IRQ flag (set when any enabled interrupt is pending). */
export function ifrIrq(ifr: number): boolean { return !!(ifr & 0x80); }

// -- Formatting ---------------------------------------------------------------

/** Format a VIA state snapshot for display. */
export function formatViaState(s: ViaState): string {
    const hex2 = (n: number) => n.toString(16).toUpperCase().padStart(2, "0");
    const hex4 = (n: number) => n.toString(16).toUpperCase().padStart(4, "0");
    return (
        `ORA=${hex2(s.ora)} ORB=${hex2(s.orb)} ` +
        `DDRA=${hex2(s.ddra)} DDRB=${hex2(s.ddrb)}\n` +
        `T1C=${hex4(s.t1c)} T1L=${hex4(s.t1l)} ` +
        `T2C=${hex4(s.t2c)} T2L=${hex2(s.t2l)}\n` +
        `ACR=${hex2(s.acr)} PCR=${hex2(s.pcr)} SR=${hex2(s.sr)}\n` +
        `IFR=${hex2(s.ifr)} IER=${hex2(s.ier)}`
    );
}

// -- Conversion ---------------------------------------------------------------

function toViaState(proto: ProtoViaState): ViaState {
    return {
        ora: proto.ora,
        orb: proto.orb,
        ddra: proto.ddra,
        ddrb: proto.ddrb,
        ira: proto.ira,
        irb: proto.irb,
        t1c: proto.t1c,
        t1l: proto.t1l,
        t2c: proto.t2c,
        t2l: proto.t2l,
        acr: proto.acr,
        pcr: proto.pcr,
        sr: proto.sr,
        ifr: proto.ifr,
        ier: proto.ier,
        t1Pending: proto.t1Pending,
        t2Pending: proto.t2Pending,
        t1Pb7: proto.t1Pb7,
        ca1: proto.ca1,
        ca2: proto.ca2,
        cb1: proto.cb1,
        cb2: proto.cb2,
    };
}

// -- Class --------------------------------------------------------------------

/**
 * 6522 VIA access for System VIA or User VIA.
 *
 * Provides read access to VIA state including registers and internal state.
 */
export class Via {
    private readonly stub: DeviceInspectionClient;
    private readonly viaId: ViaId;

    constructor(stub: DeviceInspectionClient, viaId: ViaId) {
        this.stub = stub;
        this.viaId = viaId;
    }

    /** Get the current VIA state. */
    async getState(): Promise<ViaState> {
        if (this.viaId === ViaId.SYSTEM) {
            const response = await promisify<GetSystemViaStateRequest, ProtoViaState>(
                this.stub as unknown as Record<string, Function>,
                "getSystemViaState",
                {},
            );
            return toViaState(response);
        } else {
            const response = await promisify<GetUserViaStateRequest, ProtoViaState>(
                this.stub as unknown as Record<string, Function>,
                "getUserViaState",
                {},
            );
            return toViaState(response);
        }
    }
}
