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
    parasiteConnected: boolean;
    parasiteType: string;
    parasiteClockHz: number;
    sharedMemoryName: string;
    parasiteGrpcAddress: string;
}

function toTubeStatus(proto: ProtoGetTubeStatusResponse): TubeStatus {
    return {
        hasTubeSocket: proto.hasTubeSocket,
        enabled: proto.enabled,
        parasiteConnected: proto.parasiteConnected,
        parasiteType: proto.parasiteType,
        parasiteClockHz: proto.parasiteClockHz,
        sharedMemoryName: proto.sharedMemoryName,
        parasiteGrpcAddress: proto.parasiteGrpcAddress,
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

    /** Whether a parasite process is connected. */
    async isParasiteConnected(): Promise<boolean> {
        return (await this.getStatus()).parasiteConnected;
    }

    /** Get the parasite's gRPC address (empty if not registered). */
    async getParasiteGrpcAddress(): Promise<string> {
        return (await this.getStatus()).parasiteGrpcAddress;
    }
}
