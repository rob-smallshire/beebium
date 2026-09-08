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

export interface TubeStatus {
    hasTubeSocket: boolean;
    enabled: boolean;
    coprocessorConnected: boolean;
    coprocessorType: string;
    coprocessorClockHz: number;
    sharedMemoryName: string;
    coprocessorGrpcAddress: string;
}

function toTubeStatus(proto: ProtoGetTubeStatusResponse): TubeStatus {
    return {
        hasTubeSocket: proto.hasTubeSocket,
        enabled: proto.enabled,
        coprocessorConnected: proto.coprocessorConnected,
        coprocessorType: proto.coprocessorType,
        coprocessorClockHz: proto.coprocessorClockHz,
        sharedMemoryName: proto.sharedMemoryName,
        coprocessorGrpcAddress: proto.coprocessorGrpcAddress,
    };
}

/**
 * Tube coprocessor management interface.
 *
 * Provides Tube status queries for the host machine.
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

    /** Whether a coprocessor process is connected. */
    async isCoprocessorConnected(): Promise<boolean> {
        return (await this.getStatus()).coprocessorConnected;
    }

    /** Get the coprocessor's gRPC address (empty if not registered). */
    async getCoprocessorGrpcAddress(): Promise<string> {
        return (await this.getStatus()).coprocessorGrpcAddress;
    }
}
