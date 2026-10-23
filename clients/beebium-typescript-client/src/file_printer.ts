/**
 * Client for the file-printer extension's typed API.
 *
 * The file-printer extension spools everything the BBC sends to its printer
 * port into a file on the host. This client reads what it has written so far
 * and switches the printer online or offline -- the scripting-friendly
 * equivalent of the sidebar panel. Requires the server to be launched with
 * --file-printer path=<file>.
 *
 * The FilePrinter messages are tunnelled over the core's ExtensionRpc channel.
 */

import {
    FilePrinterGetStatusRequest,
    FilePrinterSetOnlineRequest,
    FilePrinterStatus as ProtoFilePrinterStatus,
} from "./generated/file_printer.js";
import type { ExtensionChannel } from "./extension_rpc.js";

/** The logical service name the file-printer extension's dispatcher registers. */
const SERVICE = "FilePrinter";

export interface FilePrinterStatus {
    /** The file being written. */
    path: string;
    /** Bytes accepted from the port since launch. */
    bytesReceived: number;
    /** Bytes written to the file, after line-ending translation. */
    bytesWritten: number;
    /** Is the printer accepting bytes? */
    online: boolean;
}

/** Read the file printer's status and switch it online or offline. */
export class FilePrinter {
    private readonly channel: ExtensionChannel;

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /** The file being written, the byte counts and the online state. */
    async getStatus(): Promise<FilePrinterStatus> {
        const payload = FilePrinterGetStatusRequest.encode(
            FilePrinterGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetStatus", payload);
        return fromProto(ProtoFilePrinterStatus.decode(reply));
    }

    /**
     * Switch the printer online or offline. Offline, the receiver holds the
     * byte the BBC strobed and withholds /ACK, so the guest waits as it would
     * for a switched-off printer; online again, it accepts the byte and the
     * print resumes. Returns the status after the change. Never blocks.
     */
    async setOnline(online: boolean): Promise<FilePrinterStatus> {
        const payload = FilePrinterSetOnlineRequest.encode(
            FilePrinterSetOnlineRequest.fromPartial({ online }),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "SetOnline", payload);
        return fromProto(ProtoFilePrinterStatus.decode(reply));
    }
}

function fromProto(response: ProtoFilePrinterStatus): FilePrinterStatus {
    return {
        path: response.path,
        bytesReceived: response.bytesReceived,
        bytesWritten: response.bytesWritten,
        online: response.online,
    };
}
