/**
 * Piconet (USB-CDC Econet bridge) transport-specific operations.
 *
 * These RPCs are surfaced by the PiconetService dispatcher when Piconet is the
 * active Econet transport on the server. The current scope is intentionally
 * minimal -- just enough for a user to confirm "yes, my Piconet is plugged in,
 * and it's on /dev/X".
 *
 * The PiconetService messages are tunnelled over the core's ExtensionRpc
 * channel; the piconet extension no longer hosts its own gRPC service. The
 * public API here is unchanged.
 */

import {
    PiconetGetStatusRequest,
    PiconetGetStatusResponse,
} from "./generated/piconet_service.js";
import { ExtensionIdResolver } from "./extension_rpc.js";
import type { ExtensionChannel, ExtensionIdSource } from "./extension_rpc.js";

/** The logical service name the piconet extension's dispatcher registers. */
const SERVICE = "PiconetService";

export interface PiconetStatus {
    /**
     * Serial device path the Piconet is configured to use, e.g.
     * "/dev/tty.usbmodem101". Set at startup from the
     * --piconet device_path=... CLI argument or preset value.
     */
    devicePath: string;

    /**
     * True if the SerialPort is currently open. On POSIX this is the
     * file descriptor's validity; if the user yanks the USB cable
     * mid-run this transitions to false. False at startup means the
     * device couldn't be opened.
     *
     * This says nothing about whether a real Econet wire is connected
     * to the Piconet adapter; only whether the host-side USB CDC
     * endpoint is responsive.
     */
    serialOpen: boolean;
}

/**
 * Piconet-specific RPCs.
 *
 * Available on the server's gRPC surface only when Piconet is the
 * active Econet transport. Check bbc.transport.getActive() first if
 * your code might run against a server configured for AUN or no
 * transport.
 */
export class Piconet {
    private readonly channel: ExtensionChannel;
    private readonly extensionId: ExtensionIdResolver;

    /**
     * @param channel The ExtensionRpc channel that carries the messages.
     * @param extensionId The transport instance to address: the id that
     *     EconetTransportService reports for it, or a function that discovers
     *     it on first use. Empty routes by service name, which the server
     *     accepts only while one loaded instance offers the service.
     */
    constructor(channel: ExtensionChannel, extensionId: ExtensionIdSource = "") {
        this.channel = channel;
        this.extensionId = new ExtensionIdResolver(extensionId);
    }

    /** Unary call to this transport instance's dispatcher. */
    private async invoke(method: string, payload: Uint8Array): Promise<Uint8Array> {
        return this.channel.invoke(SERVICE, method, payload, await this.extensionId.get());
    }

    /** Read the Piconet adapter status (device path + serial open). */
    async getStatus(): Promise<PiconetStatus> {
        const payload = PiconetGetStatusRequest.encode(
            PiconetGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.invoke("GetStatus", payload);
        const response = PiconetGetStatusResponse.decode(reply);
        return {
            devicePath: response.devicePath,
            serialOpen: response.serialOpen,
        };
    }
}
