/**
 * Econet management interface for the Beebium TypeScript client.
 *
 * Provides Econet hardware configuration and status queries.
 *
 * Note: the AUN-specific operations (setConnected / addPeer /
 * removePeer / listPeers) moved out of EconetService into AunService
 * during the prior branch, and the full typed TS wrappers for the
 * new per-transport services (AunService, PiconetService,
 * EconetTransportService, ExtensionUiService) have not yet been
 * ported from the Python client. See
 * project_typescript_client_cutover.md in project memory for the
 * outstanding work.
 */

import type {
    EconetServiceClient,
    GetEconetStatusResponse as ProtoGetEconetStatusResponse,
    AdlcStatus as ProtoAdlcStatus,
    HandshakeStatus as ProtoHandshakeStatus,
    EnableEconetResponse as ProtoEnableEconetResponse,
    DisableEconetResponse as ProtoDisableEconetResponse,
    SetStationIdResponse as ProtoSetStationIdResponse,
} from "./generated/econet.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";
import { EconetError } from "./exceptions.js";

export interface AdlcStatus {
    cr1: number;
    cr2: number;
    cr3: number;
    cr4: number;
    sr1: number;
    sr2: number;
    irqOutput: boolean;
    txFifoEmpty: boolean;
    txFifoFull: boolean;
    rxFifoEmpty: boolean;
    rxFifoFull: boolean;
    txFrameField: string;
    rxFrameField: string;
    pseLevel: number;
    ctsInput: boolean;
}

export interface HandshakeStatus {
    stage: string;
    flagFillActive: boolean;
}

export interface EconetStatus {
    hasEconetSocket: boolean;
    enabled: boolean;
    stationId: number;
    aunMode: boolean;
    connected: boolean;
    adlc: AdlcStatus | undefined;
    handshake: HandshakeStatus | undefined;
}

function toAdlcStatus(proto: ProtoAdlcStatus | undefined): AdlcStatus | undefined {
    if (!proto) return undefined;
    return {
        cr1: proto.cr1,
        cr2: proto.cr2,
        cr3: proto.cr3,
        cr4: proto.cr4,
        sr1: proto.sr1,
        sr2: proto.sr2,
        irqOutput: proto.irqOutput,
        txFifoEmpty: proto.txFifoEmpty,
        txFifoFull: proto.txFifoFull,
        rxFifoEmpty: proto.rxFifoEmpty,
        rxFifoFull: proto.rxFifoFull,
        txFrameField: proto.txFrameField,
        rxFrameField: proto.rxFrameField,
        pseLevel: proto.pseLevel,
        ctsInput: proto.ctsInput,
    };
}

function toHandshakeStatus(proto: ProtoHandshakeStatus | undefined): HandshakeStatus | undefined {
    if (!proto) return undefined;
    return {
        stage: proto.stage,
        flagFillActive: proto.flagFillActive,
    };
}

function toEconetStatus(proto: ProtoGetEconetStatusResponse): EconetStatus {
    return {
        hasEconetSocket: proto.hasEconetSocket,
        enabled: proto.enabled,
        stationId: proto.stationId,
        aunMode: proto.aunMode,
        connected: proto.connected,
        adlc: toAdlcStatus(proto.adlc),
        handshake: toHandshakeStatus(proto.handshake),
    };
}

/**
 * Econet management interface.
 *
 * Provides Econet hardware configuration and status queries. For
 * AUN-specific peer management, Piconet device status, or transport
 * discovery, use AunService / PiconetService / EconetTransportService
 * directly via gRPC -- the TS wrappers for those are not yet
 * implemented.
 */
export class Econet {
    private readonly stub: EconetServiceClient;

    constructor(stub: EconetServiceClient) {
        this.stub = stub;
    }

    /** Get the full Econet hardware status. */
    async getStatus(): Promise<EconetStatus> {
        const response = await promisify<{}, ProtoGetEconetStatusResponse>(
            this.stub as unknown as Record<string, Function>,
            "getEconetStatus",
            {},
        );
        return toEconetStatus(response);
    }

    /**
     * Stream Econet status changes.
     *
     * The server pushes an initial snapshot on subscription, then a new
     * snapshot whenever status visible on EconetService changes
     * (enable/disable, station ID, or transport backend connection
     * toggle). Iteration ends when the client cancels or the server
     * shuts down.
     *
     * @param options.minIntervalMs Minimum interval between pushes
     *     (0 = server default, typically 50ms). The server only pushes
     *     on change, so this caps update rate rather than forcing
     *     periodic traffic.
     */
    async *watchStatus(
        options?: { minIntervalMs?: number },
    ): AsyncIterable<EconetStatus> {
        const stream = this.stub.watchEconetStatus({
            minIntervalMs: options?.minIntervalMs ?? 0,
        });
        for await (const proto of toAsyncIterable(stream)) {
            yield toEconetStatus(proto);
        }
    }

    /** Whether Econet hardware is currently enabled. */
    async isEnabled(): Promise<boolean> {
        return (await this.getStatus()).enabled;
    }

    /** Get the current station ID. */
    async getStationId(): Promise<number> {
        return (await this.getStatus()).stationId;
    }

    /**
     * Enable Econet hardware.
     *
     * @param stationId - Station number (1-254).
     * @param options.aunPort - UDP port to bind (0 = default 32768).
     * @param options.noNetwork - If true, fit hardware with no network connection.
     * @returns The actual AUN port that was bound.
     */
    async enable(
        stationId: number,
        options?: { aunPort?: number; noNetwork?: boolean },
    ): Promise<number> {
        const response = await promisify<
            { stationId: number; aunPort: number; noNetwork: boolean },
            ProtoEnableEconetResponse
        >(
            this.stub as unknown as Record<string, Function>,
            "enableEconet",
            {
                stationId,
                aunPort: options?.aunPort ?? 0,
                noNetwork: options?.noNetwork ?? false,
            },
        );
        if (!response.success) {
            throw new EconetError(`Enable Econet failed: ${response.error}`);
        }
        return response.actualAunPort;
    }

    /** Set the station ID (takes effect on next machine reset). */
    async setStationId(stationId: number): Promise<void> {
        const response = await promisify<{ stationId: number }, ProtoSetStationIdResponse>(
            this.stub as unknown as Record<string, Function>,
            "setStationId",
            { stationId },
        );
        if (!response.success) {
            throw new EconetError(`Set station ID failed: ${response.error}`);
        }
    }

    /** Disable Econet hardware. */
    async disable(): Promise<void> {
        const response = await promisify<{}, ProtoDisableEconetResponse>(
            this.stub as unknown as Record<string, Function>,
            "disableEconet",
            {},
        );
        if (!response.success) {
            throw new EconetError(`Disable Econet failed: ${response.error}`);
        }
    }
}
