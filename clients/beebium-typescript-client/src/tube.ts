/**
 * Tube coprocessor management interface for the Beebium TypeScript client.
 *
 * Provides Tube status queries for the host machine.
 */

import type {
    TubeServiceClient,
    GetTubeStatusResponse as ProtoGetTubeStatusResponse,
} from "./generated/tube.js";
import { promisify } from "./call-utils.js";

/**
 * The Tube and the coprocessor attached to it. A coprocessor is an in-process
 * plugin attached at launch (`--tube-65c02`, `--tube-65c102`, or a preset's
 * coprocessor). When none is attached, `coprocessorConnected` is false and the
 * coprocessor fields are empty or zero.
 */
export interface TubeStatus {
    hasTubeSocket: boolean;
    enabled: boolean;
    coprocessorConnected: boolean;
    /** The CPU as people name it ("65C02", "65C102"), as `{coprocessor-cpu}` shows it. */
    coprocessorCpu: string;
    /** The nominal clock in hertz (3000000 for the 6502 Second Processor). */
    coprocessorClockHz: number;
    /** The coprocessor extension's instance id, as the peripheral extension listing gives it. */
    coprocessorExtensionId: string;
}

function toTubeStatus(proto: ProtoGetTubeStatusResponse): TubeStatus {
    return {
        hasTubeSocket: proto.hasTubeSocket,
        enabled: proto.enabled,
        coprocessorConnected: proto.coprocessorConnected,
        coprocessorCpu: proto.coprocessorCpu,
        coprocessorClockHz: proto.coprocessorClockHz,
        coprocessorExtensionId: proto.coprocessorExtensionId,
    };
}

/**
 * The Tube and its coprocessor. The coprocessor itself is driven and debugged
 * through `Beebium.connectCoprocessor()`.
 */
export class Tube {
    private readonly stub: TubeServiceClient;

    constructor(stub: TubeServiceClient) {
        this.stub = stub;
    }

    /** Get the full Tube status. */
    async getStatus(): Promise<TubeStatus> {
        const response = await promisify<{}, ProtoGetTubeStatusResponse>(
            this.stub as unknown as Record<string, Function>,
            "getStatus",
            {},
        );
        return toTubeStatus(response);
    }

    /** Whether the Tube hardware is currently enabled. */
    async isEnabled(): Promise<boolean> {
        return (await this.getStatus()).enabled;
    }

    /** Whether a coprocessor is attached to the Tube. */
    async isCoprocessorConnected(): Promise<boolean> {
        return (await this.getStatus()).coprocessorConnected;
    }
}
