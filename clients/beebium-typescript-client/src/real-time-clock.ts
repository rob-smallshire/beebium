/**
 * The machine's own real-time clock chip, read without side effects.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    RealTimeClockState as ProtoRealTimeClockState,
    GetRealTimeClockStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

/** The first register of the chip's battery-backed CMOS RAM. */
export const FIRST_CMOS_REGISTER = 0x0e;

/**
 * The clock chip's registers, as a read of each would return them. `present`
 * is false on a machine with no clock of its own (only the Integra-B has one,
 * a CDP6818); the other fields are then empty.
 */
export interface RealTimeClockState {
    present: boolean;
    chip: string;
    /**
     * All 64 registers: 0-9 the time and alarm, 10-13 control registers A-D,
     * 14-63 the battery-backed CMOS RAM.
     */
    registers: Uint8Array;
    /**
     * The battery-backed CMOS RAM, registers 14-63. On the Integra-B, IBOS's
     * user register N is `cmosRam[N]`.
     */
    cmosRam: Uint8Array;
    /** The register the chip's address latch selects. */
    addressRegister: number;
}

/**
 * The machine's own real-time clock chip. Reading through the guest's RTC
 * ports has side effects -- it moves the chip's address latch, and a read of
 * register C clears its interrupt flags -- so a test that wants the raw
 * registers reads them here instead. A plug-in clock (acorn-rtc on the user
 * port) is an extension and is not reported.
 */
export class RealTimeClock {
    private readonly stub: DeviceInspectionClient;

    constructor(stub: DeviceInspectionClient) {
        this.stub = stub;
    }

    /** The clock's registers, read without side effects. */
    async getState(): Promise<RealTimeClockState> {
        const response = await promisify<GetRealTimeClockStateRequest, ProtoRealTimeClockState>(
            this.stub as unknown as Record<string, Function>,
            "getRealTimeClockState",
            {},
        );
        const registers = new Uint8Array(response.registers);
        return {
            present: response.present,
            chip: response.chip,
            registers,
            cmosRam: registers.subarray(FIRST_CMOS_REGISTER),
            addressRegister: response.addressRegister,
        };
    }
}
