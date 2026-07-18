/**
 * System service interface for the Beebium TypeScript client.
 *
 * Provides machine identity, status monitoring, shutdown control,
 * and service advertisement management.
 */

import { Metadata } from "@grpc/grpc-js";
import type { SystemServiceClient } from "./generated/system.js";
import type {
    SystemInfo,
    ServerStatusEvent as ProtoServerStatusEvent,
    MachineIdentity as ProtoMachineIdentity,
    LaunchProvenance as ProtoLaunchProvenance,
    ShutdownConditionStatus as ProtoShutdownConditionStatus,
    AdvertisementState as ProtoAdvertisementState,
    PacingStats as ProtoPacingStats,
} from "./generated/system.js";
import {
    ServerStatusType,
    ShutdownMode as ProtoShutdownMode,
} from "./generated/system.js";
import { promisify, promisifyWithMetadata } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";

export enum ServerStatus {
    READY = "ready",
    SHUTTING_DOWN = "shutting_down",
    IDENTITY_CHANGED = "identity_changed",
    SHUTDOWN_PROGRESS = "shutdown_progress",
    /**
     * Periodic liveness tick, emitted on an otherwise-idle stream every few
     * seconds. Its absence (no events within a timeout) signals a
     * silently-unreachable server -- a network partition or a frozen process.
     */
    HEARTBEAT = "heartbeat",
}

export enum ShutdownMode {
    GRACEFUL = "graceful",
    IMMEDIATE = "immediate",
}

export interface Provenance {
    type: string;
    instanceUuid: string;
    version: string;
    timestamp: number;
}

export interface MachineIdentity {
    uuid: string;
    name: string;
    modelType: string;
    modelName: string;
}

export interface ShutdownResponse {
    accepted: boolean;
    message: string;
}

export interface ShutdownConditionStatus {
    name: string;
    ready: boolean;
    elapsedMs: number;
    timeoutMs: number;
}

export interface AdvertisementState {
    enabled: boolean;
    available: boolean;
    advertisedName: string;
}

export interface ServerStatusEvent {
    status: ServerStatus;
    message: string;
    shutdownGraceMs: number;
    identity?: MachineIdentity;
    shutdownConditions: ShutdownConditionStatus[];
}

/**
 * Snapshot of emulation pacing and speed headroom.
 *
 * The speed fields are sampled by the server over its pacing window (~5s),
 * so they update slowly and steadily -- intended for a polled readout
 * (e.g. a UI speed slider once per second), not a high-frequency series.
 */
export interface PacingStats {
    /** Total pacing ticks since start. */
    ticksExecuted: number;
    /** Ticks where sleep was cut short for I/O. */
    ticksIoSkipped: number;
    /** Current drift in cycles (+ = ahead of real time). */
    controllerDrift: number;
    /** Accumulated drift (time debt). */
    controllerIntegral: number;
    /** Configured multiplier (0.0 = unlimited). */
    speedMultiplier: number;
    /** Actual emulated rate / base clock over the window. */
    achievedSpeedMultiplier: number;
    /** Estimated ceiling at current host load; 0.0 = no estimate yet. */
    estimatedMaxSpeedMultiplier: number;
}

function mapServerStatus(protoStatus: ServerStatusType): ServerStatus {
    switch (protoStatus) {
        case ServerStatusType.SERVER_STATUS_READY:
            return ServerStatus.READY;
        case ServerStatusType.SERVER_STATUS_SHUTTING_DOWN:
            return ServerStatus.SHUTTING_DOWN;
        case ServerStatusType.SERVER_STATUS_IDENTITY_CHANGED:
            return ServerStatus.IDENTITY_CHANGED;
        case ServerStatusType.SERVER_STATUS_HEARTBEAT:
            return ServerStatus.HEARTBEAT;
        case ServerStatusType.SERVER_STATUS_SHUTDOWN_PROGRESS:
            return ServerStatus.SHUTDOWN_PROGRESS;
        default:
            return ServerStatus.READY;
    }
}

function mapShutdownMode(mode: ShutdownMode): ProtoShutdownMode {
    switch (mode) {
        case ShutdownMode.GRACEFUL:
            return ProtoShutdownMode.SHUTDOWN_GRACEFUL;
        case ShutdownMode.IMMEDIATE:
            return ProtoShutdownMode.SHUTDOWN_IMMEDIATE;
    }
}

function toMachineIdentity(proto: ProtoMachineIdentity): MachineIdentity {
    return {
        uuid: proto.uuid,
        name: proto.name,
        modelType: proto.modelType,
        modelName: proto.modelName,
    };
}

function toProvenance(proto: ProtoLaunchProvenance): Provenance {
    return {
        type: proto.type,
        instanceUuid: proto.instanceUuid,
        version: proto.version,
        timestamp: proto.timestamp,
    };
}

function toShutdownConditionStatus(proto: ProtoShutdownConditionStatus): ShutdownConditionStatus {
    return {
        name: proto.name,
        ready: proto.ready,
        elapsedMs: proto.elapsedMs,
        timeoutMs: proto.timeoutMs,
    };
}

function toAdvertisementState(proto: ProtoAdvertisementState): AdvertisementState {
    return {
        enabled: proto.enabled,
        available: proto.available,
        advertisedName: proto.advertisedName,
    };
}

function toServerStatusEvent(proto: ProtoServerStatusEvent): ServerStatusEvent {
    return {
        status: mapServerStatus(proto.status),
        message: proto.message,
        shutdownGraceMs: proto.shutdownGraceMs,
        identity: proto.identity ? toMachineIdentity(proto.identity) : undefined,
        shutdownConditions: proto.shutdownConditions.map(toShutdownConditionStatus),
    };
}

/**
 * System service interface.
 *
 * Provides machine identity, status monitoring, shutdown control,
 * and service advertisement management.
 */
export class System {
    private readonly stub: SystemServiceClient;
    private readonly instanceUuid: string | undefined;

    constructor(stub: SystemServiceClient, instanceUuid?: string) {
        this.stub = stub;
        this.instanceUuid = instanceUuid;
    }

    /** Get the system info from the server. */
    private async getSystemInfo(): Promise<SystemInfo> {
        return promisify<{}, SystemInfo>(
            this.stub as unknown as Record<string, Function>,
            "getSystemInfo",
            {},
        );
    }

    /** Get the machine identity. */
    async getIdentity(): Promise<MachineIdentity> {
        const info = await this.getSystemInfo();
        if (!info.identity) {
            throw new Error("Server returned no identity");
        }
        return toMachineIdentity(info.identity);
    }

    /** Get the launch provenance. */
    async getProvenance(): Promise<Provenance> {
        const info = await this.getSystemInfo();
        if (!info.provenance) {
            throw new Error("Server returned no provenance");
        }
        return toProvenance(info.provenance);
    }

    /** Set the machine name. Returns the updated identity. */
    async setMachineName(name: string): Promise<MachineIdentity> {
        const response = await promisify<{ name: string }, { identity?: ProtoMachineIdentity }>(
            this.stub as unknown as Record<string, Function>,
            "setMachineName",
            { name },
        );
        if (!response.identity) {
            throw new Error("Server returned no identity after setMachineName");
        }
        return toMachineIdentity(response.identity);
    }

    /** Subscribe to server status events as an async iterable. */
    async *watchStatus(): AsyncIterable<ServerStatusEvent> {
        const stream = this.stub.watchServerStatus({});
        for await (const event of toAsyncIterable(stream)) {
            yield toServerStatusEvent(event);
        }
    }

    /**
     * Wait for the server to report READY status.
     *
     * Returns true if a READY event was received within the timeout,
     * false if the timeout elapsed.
     */
    async waitForReady(timeoutMs: number = 5000): Promise<boolean> {
        return new Promise<boolean>(async (resolve) => {
            const timer = setTimeout(() => {
                resolve(false);
            }, timeoutMs);

            try {
                for await (const event of this.watchStatus()) {
                    if (event.status === ServerStatus.READY) {
                        clearTimeout(timer);
                        resolve(true);
                        return;
                    }
                }
                // Stream ended without a READY event.
                clearTimeout(timer);
                resolve(false);
            } catch {
                clearTimeout(timer);
                resolve(false);
            }
        });
    }

    /** Get the nominal CPU clock speed in Hz. */
    async getClockSpeedHz(): Promise<number> {
        const info = await this.getSystemInfo();
        return info.clockSpeedHz;
    }

    /**
     * Get the wire-protocol fingerprint the server was built against.
     * Empty if the server predates protocol fingerprinting.
     */
    async getProtocolFingerprint(): Promise<string> {
        const info = await this.getSystemInfo();
        return info.protocolFingerprint;
    }

    /**
     * Filesystem path of the running server executable.
     *
     * Resolved by the server from the OS rather than argv[0], so a server
     * started through a PATH symlink reports its real target. Use this to
     * identify which binary a connection actually reached -- notably when
     * diagnosing a protocol fingerprint mismatch.
     *
     * Empty if the server cannot determine its own path.
     */
    async getExecutablePath(): Promise<string> {
        const info = await this.getSystemInfo();
        return info.executablePath;
    }

    /** Get the number of connected clients. */
    async getClientCount(): Promise<number> {
        const info = await this.getSystemInfo();
        return info.connections?.clientCount ?? 0;
    }

    /**
     * Request the server to shut down.
     *
     * Sends the instance UUID as metadata if it was provided at construction.
     */
    async requestShutdown(
        mode: ShutdownMode = ShutdownMode.GRACEFUL,
        gracePeriodMs: number = 5000,
    ): Promise<ShutdownResponse> {
        const request = {
            mode: mapShutdownMode(mode),
            gracePeriodMs,
        };

        let response: { accepted: boolean; message: string };

        if (this.instanceUuid) {
            const metadata = new Metadata();
            metadata.set("x-beebium-instance-uuid", this.instanceUuid);
            response = await promisifyWithMetadata<typeof request, { accepted: boolean; message: string }>(
                this.stub as unknown as Record<string, Function>,
                "requestShutdown",
                request,
                metadata,
            );
        } else {
            response = await promisify<typeof request, { accepted: boolean; message: string }>(
                this.stub as unknown as Record<string, Function>,
                "requestShutdown",
                request,
            );
        }

        return {
            accepted: response.accepted,
            message: response.message,
        };
    }

    /** Get the current mDNS service advertisement state. */
    async getAdvertisementState(): Promise<AdvertisementState> {
        const response = await promisify<{}, { state?: ProtoAdvertisementState }>(
            this.stub as unknown as Record<string, Function>,
            "getAdvertisementState",
            {},
        );
        if (!response.state) {
            throw new Error("Server returned no advertisement state");
        }
        return toAdvertisementState(response.state);
    }

    /** Enable or disable mDNS service advertisement. Returns the resulting state. */
    async setAdvertisement(enabled: boolean): Promise<AdvertisementState> {
        const response = await promisify<{ enabled: boolean }, { state?: ProtoAdvertisementState }>(
            this.stub as unknown as Record<string, Function>,
            "setAdvertisement",
            { enabled },
        );
        if (!response.state) {
            throw new Error("Server returned no advertisement state");
        }
        return toAdvertisementState(response.state);
    }

    /**
     * Set the runtime emulation speed multiplier.
     *
     * `0.0` means unlimited speed, `1.0` is real-time. Returns the resulting
     * multiplier echoed by the server.
     */
    async setSpeedMultiplier(speedMultiplier: number): Promise<number> {
        const response = await promisify<{ speedMultiplier: number }, { speedMultiplier: number }>(
            this.stub as unknown as Record<string, Function>,
            "setSpeedMultiplier",
            { speedMultiplier },
        );
        return response.speedMultiplier;
    }

    /**
     * Get a snapshot of emulation pacing and speed headroom.
     *
     * `estimatedMaxSpeedMultiplier` is an upper-bound estimate of the fastest
     * the host can currently run the emulation, derived from the proportion of
     * wall-clock time spent computing rather than sleeping. It is `0.0` when no
     * estimate is available yet (idle/paused, or the first pacing window has
     * not elapsed).
     *
     * The fields are sampled over the server's pacing window (~5s), so poll at
     * a low rate (e.g. once per second for a speed slider) rather than treating
     * it as a high-frequency stream.
     */
    async getPacingStats(): Promise<PacingStats> {
        const response = await promisify<{}, ProtoPacingStats>(
            this.stub as unknown as Record<string, Function>,
            "getPacingStats",
            {},
        );
        return {
            ticksExecuted: response.ticksExecuted,
            ticksIoSkipped: response.ticksIoSkipped,
            controllerDrift: response.controllerDrift,
            controllerIntegral: response.controllerIntegral,
            speedMultiplier: response.speedMultiplier,
            achievedSpeedMultiplier: response.achievedSpeedMultiplier,
            estimatedMaxSpeedMultiplier: response.estimatedMaxSpeedMultiplier,
        };
    }
}
