/**
 * Client for the tcp-printer extension's typed API.
 *
 * The tcp-printer extension sends everything the BBC prints to a raw TCP
 * printer port on the network, such as a JetDirect printer on port 9100 or a
 * printer emulator. This client reads the endpoint, whether it is connected
 * and how much has been sent -- the scripting-friendly equivalent of the
 * sidebar panel. Requires the server to be launched with --tcp-printer.
 *
 * There is no way to switch the printer online or offline: it is online
 * exactly while it is connected.
 *
 * The TcpPrinter messages are tunnelled over the core's ExtensionRpc channel.
 */

import {
    TcpPrinterGetStatusRequest,
    TcpPrinterStatus as ProtoTcpPrinterStatus,
} from "./generated/tcp_printer.js";
import type { ExtensionChannel } from "./extension_rpc.js";

/** The logical service name the tcp-printer extension's dispatcher registers. */
const SERVICE = "TcpPrinter";

export interface TcpPrinterStatus {
    /** host:port, an IPv6 host in brackets. */
    endpoint: string;
    /** Connected, and so online. */
    connected: boolean;
    /** Bytes accepted from the port since launch. */
    bytesReceived: number;
    /** Bytes written to the socket. */
    bytesSent: number;
}

/** Read the network printer's status. */
export class TcpPrinter {
    private readonly channel: ExtensionChannel;

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /** The endpoint, whether it is connected, and the byte counts. */
    async getStatus(): Promise<TcpPrinterStatus> {
        const payload = TcpPrinterGetStatusRequest.encode(
            TcpPrinterGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetStatus", payload);
        const response = ProtoTcpPrinterStatus.decode(reply);
        return {
            endpoint: response.endpoint,
            connected: response.connected,
            bytesReceived: response.bytesReceived,
            bytesSent: response.bytesSent,
        };
    }
}
