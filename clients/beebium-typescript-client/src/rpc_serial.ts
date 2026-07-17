/**
 * Client for the rpc-serial extension.
 *
 * The rpc-serial extension makes the RPC client the device on the far end of
 * the BBC's serial wire: send() injects bytes for the BBC to receive, receive()
 * collects bytes the BBC has transmitted. Requires the server to be launched
 * with --rpc-serial (so the extension owns the serial port).
 *
 * The RpcSerial messages are tunnelled over the core's ExtensionRpc channel;
 * the rpc-serial extension no longer hosts its own gRPC service. The public API
 * here is unchanged.
 */

import {
    RpcSerialSendRequest,
    RpcSerialSendResponse,
    RpcSerialReceiveRequest,
    RpcSerialReceiveResponse,
    RpcSerialStatusRequest,
    RpcSerialStatus as ProtoRpcSerialStatus,
} from "./generated/rpc_serial.js";
import type { ExtensionChannel } from "./extension_rpc.js";

/** The logical service name the rpc-serial extension's dispatcher registers. */
const SERVICE = "RpcSerial";

export interface RpcSerialStatus {
    /** Bytes the BBC sent, awaiting receive(). */
    txPending: number;
    /** Bytes queued (via send()) to deliver to the BBC. */
    rxPending: number;
}

/**
 * Drive the client-driven serial peer provided by the rpc-serial extension.
 */
export class RpcSerial {
    private readonly channel: ExtensionChannel;

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /**
     * Inject bytes for the BBC to receive.
     *
     * Returns the number of bytes accepted, which is fewer than data.length when
     * the receive queue is full; resend data.slice(accepted) after the BBC has
     * read some. Never blocks.
     */
    async send(data: Uint8Array): Promise<number> {
        const payload = RpcSerialSendRequest.encode(
            RpcSerialSendRequest.fromPartial({ data: Buffer.from(data) }),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "Send", payload);
        return RpcSerialSendResponse.decode(reply).accepted;
    }

    /** Collect bytes the BBC has transmitted (0 = all currently available). */
    async receive(maxBytes = 0): Promise<Uint8Array> {
        const payload = RpcSerialReceiveRequest.encode(
            RpcSerialReceiveRequest.fromPartial({ maxBytes }),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "Receive", payload);
        return new Uint8Array(RpcSerialReceiveResponse.decode(reply).data);
    }

    /** Pending byte counts in each direction. */
    async getStatus(): Promise<RpcSerialStatus> {
        const payload = RpcSerialStatusRequest.encode(
            RpcSerialStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetStatus", payload);
        const response = ProtoRpcSerialStatus.decode(reply);
        return { txPending: response.txPending, rxPending: response.rxPending };
    }
}
