/**
 * Serial port (MC6850 ACIA + Serial ULA) status interface for the Beebium
 * TypeScript client.
 *
 * Wraps SerialService, which reports the live state of the on-board serial
 * hardware registers. What is attached to the far end of the wire is owned by a
 * serial PeripheralExtension (host-serial, rpc-serial, loopback-serial), not by
 * this service -- drive the rpc-serial peer via the RpcSerial client.
 */

import type {
    SerialServiceClient,
    SerialStatus as ProtoSerialStatus,
} from "./generated/serial.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";

export interface SerialStatus {
    hasSerialSocket: boolean;
    /** Physical connector standard ("RS423" / "RS232"); empty when no socket. */
    connector: string;
    // MC6850 ACIA
    aciaControl: number;
    aciaStatus: number;
    tdre: boolean;
    rdrf: boolean;
    notDcd: boolean;
    notCts: boolean;
    irqPending: boolean;
    // Serial ULA (SERPROC)
    ulaControl: number;
    txBaud: number;
    rxBaud: number;
    rs423Selected: boolean;
    motorOn: boolean;
    txBitPeriod: number;
    rxBitPeriod: number;
}

function toSerialStatus(proto: ProtoSerialStatus): SerialStatus {
    return {
        hasSerialSocket: proto.hasSerialSocket,
        connector: proto.connector,
        aciaControl: proto.aciaControl,
        aciaStatus: proto.aciaStatus,
        tdre: proto.tdre,
        rdrf: proto.rdrf,
        notDcd: proto.notDcd,
        notCts: proto.notCts,
        irqPending: proto.irqPending,
        ulaControl: proto.ulaControl,
        txBaud: proto.txBaud,
        rxBaud: proto.rxBaud,
        rs423Selected: proto.rs423Selected,
        motorOn: proto.motorOn,
        txBitPeriod: proto.txBitPeriod,
        rxBitPeriod: proto.rxBitPeriod,
    };
}

/**
 * Serial port (MC6850 ACIA + Serial ULA) status.
 */
export class Serial {
    private readonly stub: SerialServiceClient;

    constructor(stub: SerialServiceClient) {
        this.stub = stub;
    }

    /** Get serial hardware status (ACIA + Serial ULA registers). */
    async getStatus(): Promise<SerialStatus> {
        const response = await promisify<{}, ProtoSerialStatus>(
            this.stub as unknown as Record<string, Function>,
            "getSerialStatus",
            {},
        );
        return toSerialStatus(response);
    }

    /**
     * Stream serial status changes, pushed by the server.
     *
     * The server sends an initial snapshot on subscription, then a fresh one
     * whenever the chip state changes (sampled at minIntervalMs, so per-byte
     * TDRE/RDRF toggling is coalesced). Iteration ends when the client cancels
     * or the server shuts down; prefer this over polling getStatus.
     *
     * @param options.minIntervalMs Minimum interval between change checks
     *     (0 = server default of 50ms).
     */
    async *watchStatus(
        options?: { minIntervalMs?: number },
    ): AsyncIterable<SerialStatus> {
        const stream = this.stub.watchSerialStatus({
            minIntervalMs: options?.minIntervalMs ?? 0,
        });
        for await (const proto of toAsyncIterable(stream)) {
            yield toSerialStatus(proto);
        }
    }
}
