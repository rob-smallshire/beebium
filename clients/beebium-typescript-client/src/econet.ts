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
    EconetEvent as ProtoEconetEvent,
} from "./generated/econet.js";
import { EconetEventType } from "./generated/econet.js";
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

/** A frame observed crossing the transport. */
export interface EconetFrameInfo {
    frameType: string;
    destNet: number;
    destStn: number;
    srcNet: number;
    srcStn: number;
    port: number;
    controlByte: number;

    /** The payload's true size, which may exceed `data.length`. */
    dataLength: number;

    /**
     * The AUN transaction handle. A peer retransmitting an unacknowledged
     * frame reuses its handle, so two frames sharing one are the same
     * transmission rather than two transactions carrying like bytes.
     */
    handle: number;

    /**
     * The leading bytes of the payload. Compare with `dataLength` to tell
     * whether the server truncated it.
     */
    data: Uint8Array;
}

/** Something that happened on the Econet transport. */
export interface EconetEvent {
    /** "frameSent", "frameReceived", "connectionChange", or "unknown". */
    type: string;

    /** Monotonic. A gap means events were lost. */
    sequence: number;

    /** Present for frameSent and frameReceived. */
    frame?: EconetFrameInfo;

    /** Meaningful for connectionChange. */
    connected: boolean;
}

export interface HandshakeStatus {
    stage: string;
    flagFillActive: boolean;

    /**
     * Inbound holding queue. Packets arriving while the handshake is in a
     * stage that cannot use them are held and re-offered rather than dropped.
     * A non-zero `framesDropped` means a peer is offering traffic faster than
     * the handshake can consume it.
     */
    framesHeld: number;
    framesRedelivered: number;
    framesExpired: number;
    framesDropped: number;
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
        framesHeld: proto.framesHeld,
        framesRedelivered: Number(proto.framesRedelivered),
        framesExpired: Number(proto.framesExpired),
        framesDropped: Number(proto.framesDropped),
    };
}

const EVENT_TYPE_NAMES: Record<number, string> = {
    [EconetEventType.ECONET_EVENT_FRAME_SENT]: "frameSent",
    [EconetEventType.ECONET_EVENT_FRAME_RECEIVED]: "frameReceived",
    [EconetEventType.ECONET_EVENT_HANDSHAKE_CHANGE]: "handshakeChange",
    [EconetEventType.ECONET_EVENT_CONNECTION_CHANGE]: "connectionChange",
};

function toEconetEvent(proto: ProtoEconetEvent): EconetEvent {
    return {
        type: EVENT_TYPE_NAMES[proto.type] ?? "unknown",
        sequence: Number(proto.sequence),
        frame: proto.frame
            ? {
                  frameType: proto.frame.frameType,
                  destNet: proto.frame.destNet,
                  destStn: proto.frame.destStn,
                  srcNet: proto.frame.srcNet,
                  srcStn: proto.frame.srcStn,
                  port: proto.frame.port,
                  controlByte: proto.frame.controlByte,
                  dataLength: proto.frame.dataLength,
                  handle: proto.frame.handle,
                  data: proto.frame.data,
              }
            : undefined,
        connected: proto.connected,
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

    /**
     * Stream frames as they cross the transport.
     *
     * The view the emulated screen cannot give: what actually went out on the
     * network and what came back. Frames are observed just above the wire, so
     * they are typed frames after the four-way handshake has translated them.
     *
     * Starts from the moment of subscription rather than replaying history,
     * and a quiet network produces nothing. `sequence` is monotonic; a gap
     * means events were lost because this subscriber fell further behind than
     * the server's ring buffer reaches.
     *
     * Rejects with FAILED_PRECONDITION if no Econet hardware is fitted, since
     * there would be nothing to observe.
     */
    async *events(
        options?: { maxBatch?: number; minIntervalMs?: number },
    ): AsyncIterable<EconetEvent> {
        const stream = this.stub.subscribeEconetEvents({
            maxBatch: options?.maxBatch ?? 0,
            minIntervalMs: options?.minIntervalMs ?? 0,
        });
        for await (const proto of toAsyncIterable(stream)) {
            yield toEconetEvent(proto);
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
