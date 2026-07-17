/**
 * MC6845 CRTC (Cathode Ray Tube Controller) access for the Beebium TypeScript client.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    CrtcState as ProtoCrtcState,
    GetCrtcStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

export interface CrtcState {
    /** All 18 registers (R0-R17) */
    registers: number[];
    /** Currently selected register (address register) */
    addressRegister: number;
    /** Horizontal character position (0 to R0) */
    column: number;
    /** Vertical character row (0 to R4) */
    row: number;
    /** Scanline within character row (0 to R9) */
    raster: number;
    /** Current character address (MA0-MA13) */
    charAddr: number;
    /** R12/R13: Display start address */
    screenStart: number;
    /** R14/R15: Cursor address */
    cursorPosition: number;
    /** Horizontal sync active */
    inHsync: boolean;
    /** Vertical sync active */
    inVsync: boolean;
    /** Display output enabled */
    displayEnabled: boolean;
}

// -- Register index constants -------------------------------------------------

export const R0_HTOTAL = 0;
export const R1_HDISPLAYED = 1;
export const R2_HSYNC_POS = 2;
export const R3_SYNC_WIDTH = 3;
export const R4_VTOTAL = 4;
export const R5_VTOTAL_ADJ = 5;
export const R6_VDISPLAYED = 6;
export const R7_VSYNC_POS = 7;
export const R8_INTERLACE = 8;
export const R9_MAX_SCANLINE = 9;
export const R10_CURSOR_START = 10;
export const R11_CURSOR_END = 11;
export const R12_START_ADDR_HI = 12;
export const R13_START_ADDR_LO = 13;
export const R14_CURSOR_HI = 14;
export const R15_CURSOR_LO = 15;
export const R16_LIGHTPEN_HI = 16;
export const R17_LIGHTPEN_LO = 17;

// -- Convenience accessor functions -------------------------------------------

/** R0: Horizontal total (characters - 1). */
export function htotal(s: CrtcState): number { return s.registers[0]!; }

/** R1: Horizontal displayed (characters). */
export function hdisplayed(s: CrtcState): number { return s.registers[1]!; }

/** R2: Horizontal sync position. */
export function hsyncPos(s: CrtcState): number { return s.registers[2]!; }

/** R3: Sync widths (H in low nibble, V in high nibble). */
export function syncWidth(s: CrtcState): number { return s.registers[3]!; }

/** R4: Vertical total (rows - 1). */
export function vtotal(s: CrtcState): number { return s.registers[4]!; }

/** R5: Vertical total adjust (scanlines). */
export function vtotalAdj(s: CrtcState): number { return s.registers[5]!; }

/** R6: Vertical displayed (rows). */
export function vdisplayed(s: CrtcState): number { return s.registers[6]!; }

/** R7: Vertical sync position. */
export function vsyncPos(s: CrtcState): number { return s.registers[7]!; }

/** R8: Interlace and skew mode. */
export function interlaceMode(s: CrtcState): number { return s.registers[8]!; }

/** R9: Maximum scanline address (rows - 1). */
export function maxScanline(s: CrtcState): number { return s.registers[9]! & 0x1F; }

/** Horizontal sync width (from R3 low nibble). */
export function hsyncWidth(s: CrtcState): number { return s.registers[3]! & 0x0F; }

/** Vertical sync width (from R3 high nibble, 0 means 16). */
export function vsyncWidth(s: CrtcState): number {
    const w = (s.registers[3]! >> 4) & 0x0F;
    return w === 0 ? 16 : w;
}

/** True if interlace sync and video mode is active (Mode 7). */
export function isInterlaced(s: CrtcState): boolean {
    return (s.registers[8]! & 0x03) === 0x03;
}

// -- Formatting ---------------------------------------------------------------

/** Format a CRTC state snapshot for display. */
export function formatCrtcState(s: CrtcState): string {
    const hex2 = (n: number) => n.toString(16).toUpperCase().padStart(2, "0");
    const hex4 = (n: number) => n.toString(16).toUpperCase().padStart(4, "0");
    return (
        `R0-R3: H=${hex2(htotal(s))} D=${hex2(hdisplayed(s))} ` +
        `S=${hex2(hsyncPos(s))} W=${hex2(syncWidth(s))}\n` +
        `R4-R7: V=${hex2(vtotal(s))} A=${hex2(vtotalAdj(s))} ` +
        `D=${hex2(vdisplayed(s))} S=${hex2(vsyncPos(s))}\n` +
        `R8-R9: I=${hex2(interlaceMode(s))} M=${hex2(maxScanline(s))}\n` +
        `Screen=${hex4(s.screenStart)} Cursor=${hex4(s.cursorPosition)}\n` +
        `Col=${s.column} Row=${s.row} Raster=${s.raster} Addr=${hex4(s.charAddr)}\n` +
        `HSync=${s.inHsync} VSync=${s.inVsync} Display=${s.displayEnabled}`
    );
}

// -- Conversion ---------------------------------------------------------------

function toCrtcState(proto: ProtoCrtcState): CrtcState {
    return {
        registers: [...proto.registers],
        addressRegister: proto.addressRegister,
        column: proto.column,
        row: proto.row,
        raster: proto.raster,
        charAddr: proto.charAddr,
        screenStart: proto.screenStart,
        cursorPosition: proto.cursorPosition,
        inHsync: proto.inHsync,
        inVsync: proto.inVsync,
        displayEnabled: proto.displayEnabled,
    };
}

// -- Class --------------------------------------------------------------------

/**
 * MC6845 CRTC access.
 *
 * Provides read access to CRTC state including all 18 registers
 * and internal timing counters.
 */
export class Crtc {
    private readonly stub: DeviceInspectionClient;

    constructor(stub: DeviceInspectionClient) {
        this.stub = stub;
    }

    /** Get the current CRTC state. */
    async getState(): Promise<CrtcState> {
        const response = await promisify<GetCrtcStateRequest, ProtoCrtcState>(
            this.stub as unknown as Record<string, Function>,
            "getCrtcState",
            {},
        );
        return toCrtcState(response);
    }
}
