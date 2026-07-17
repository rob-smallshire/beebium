/**
 * Video ULA access for the Beebium TypeScript client.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    VideoUlaState as ProtoVideoUlaState,
    GetVideoUlaStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

export interface VideoUlaState {
    /** Control register (0xFE20) */
    control: number;
    /** Palette (16 entries: logical colour -> physical colour) */
    palette: number[];
}

// -- Control register bit masks -----------------------------------------------

export const CTRL_FLASH = 0x01;
export const CTRL_TELETEXT = 0x02;
export const CTRL_LINE_WIDTH = 0x0C;
export const CTRL_FAST_CLOCK = 0x10;
export const CTRL_CURSOR_WIDTH = 0xE0;

// -- Control register helpers -------------------------------------------------

/** Flash colour select (swaps colours 8-15 with 0-7). */
export function flashSelect(control: number): boolean { return !!(control & CTRL_FLASH); }

/** True if Mode 7 teletext mode is active. */
export function teletextMode(control: number): boolean { return !!(control & CTRL_TELETEXT); }

/** Line width mode (0-3): 0=10 chars, 1=20 chars, 2=40 chars, 3=80 chars. */
export function lineWidthMode(control: number): number { return (control >> 2) & 0x03; }

/** True if 2MHz CRTC clock (high-res modes 0, 1, 2). */
export function fastClock(control: number): boolean { return !!(control & CTRL_FAST_CLOCK); }

/** Cursor width bits (0-7). */
export function cursorWidth(control: number): number { return (control >> 5) & 0x07; }

// -- Formatting ---------------------------------------------------------------

/** Format a Video ULA state snapshot for display. */
export function formatVideoUlaState(s: VideoUlaState): string {
    const hex2 = (n: number) => n.toString(16).toUpperCase().padStart(2, "0");
    const modeDesc = teletextMode(s.control)
        ? "Teletext"
        : `Bitmap (LW=${lineWidthMode(s.control)})`;
    const clock = fastClock(s.control) ? "2MHz" : "1MHz";
    const paletteStr = s.palette.map(p => p.toString(16).toUpperCase()).join(" ");
    return (
        `Control=${hex2(s.control)} (${modeDesc}, ${clock})\n` +
        `Palette: ${paletteStr}`
    );
}

// -- Conversion ---------------------------------------------------------------

function toVideoUlaState(proto: ProtoVideoUlaState): VideoUlaState {
    return {
        control: proto.control,
        palette: [...proto.palette],
    };
}

// -- Class --------------------------------------------------------------------

/**
 * Video ULA access.
 *
 * Provides read access to Video ULA state including the control register
 * and 16-entry palette (both write-only on actual hardware).
 */
export class VideoUla {
    private readonly stub: DeviceInspectionClient;

    constructor(stub: DeviceInspectionClient) {
        this.stub = stub;
    }

    /** Get the current Video ULA state. */
    async getState(): Promise<VideoUlaState> {
        const response = await promisify<GetVideoUlaStateRequest, ProtoVideoUlaState>(
            this.stub as unknown as Record<string, Function>,
            "getVideoUlaState",
            {},
        );
        return toVideoUlaState(response);
    }
}
