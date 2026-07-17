/**
 * IC32 Addressable Latch access for the Beebium TypeScript client.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    AddressableLatchState as ProtoAddressableLatchState,
    GetAddressableLatchStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

export interface AddressableLatchState {
    /** Raw 8-bit latch value */
    value: number;
    /** Bits 4-5: Screen memory base address select */
    screenBase: number;
    /** Bit 0: Active low */
    soundWriteEnable: boolean;
    /** Bit 1: Active low */
    speechRead: boolean;
    /** Bit 2: Active low */
    speechWrite: boolean;
    /** Bit 3: Active low */
    keyboardWrite: boolean;
    /** Bit 6: Active low (True = LED on) */
    capsLockLed: boolean;
    /** Bit 7: Active low (True = LED on) */
    shiftLockLed: boolean;
}

// -- Bit definitions (matching hardware active-low conventions) ----------------

export const BIT_SOUND_WRITE = 0x01;
export const BIT_SPEECH_READ = 0x02;
export const BIT_SPEECH_WRITE = 0x04;
export const BIT_KB_WRITE = 0x08;
export const BIT_SCREEN_BASE_LO = 0x10;
export const BIT_SCREEN_BASE_HI = 0x20;
export const BIT_CAPS_LOCK = 0x40;
export const BIT_SHIFT_LOCK = 0x80;

// -- Formatting ---------------------------------------------------------------

/** Format an addressable latch state snapshot for display. */
export function formatLatchState(s: AddressableLatchState): string {
    const hex2 = (n: number) => n.toString(16).toUpperCase().padStart(2, "0");
    const leds: string[] = [];
    if (s.capsLockLed) {
        leds.push("CAPS");
    }
    if (s.shiftLockLed) {
        leds.push("SHIFT");
    }
    const ledStr = leds.length > 0 ? leds.join(", ") : "none";
    return (
        `Latch=${hex2(s.value)} ScreenBase=${s.screenBase}\n` +
        `Sound=${s.soundWriteEnable} KB=${s.keyboardWrite}\n` +
        `LEDs: ${ledStr}`
    );
}

// -- Conversion ---------------------------------------------------------------

function toAddressableLatchState(proto: ProtoAddressableLatchState): AddressableLatchState {
    return {
        value: proto.value,
        screenBase: proto.screenBase,
        soundWriteEnable: proto.soundWriteEnable,
        speechRead: proto.speechRead,
        speechWrite: proto.speechWrite,
        keyboardWrite: proto.keyboardWrite,
        capsLockLed: proto.capsLockLed,
        shiftLockLed: proto.shiftLockLed,
    };
}

// -- Class --------------------------------------------------------------------

/**
 * Addressable Latch access.
 *
 * Provides read access to the addressable latch state, which is
 * write-only on actual hardware.
 */
export class AddressableLatch {
    private readonly stub: DeviceInspectionClient;

    constructor(stub: DeviceInspectionClient) {
        this.stub = stub;
    }

    /** Get the current latch state. */
    async getState(): Promise<AddressableLatchState> {
        const response = await promisify<GetAddressableLatchStateRequest, ProtoAddressableLatchState>(
            this.stub as unknown as Record<string, Function>,
            "getAddressableLatchState",
            {},
        );
        return toAddressableLatchState(response);
    }
}
