/**
 * Client for the host-serial extension's typed config API.
 *
 * The host-serial extension bridges the BBC serial port to a host PTY or serial
 * device. This client queries and re-points that bridge (mode / path / baud)
 * programmatically -- the scripting-friendly equivalent of the GUI panel,
 * without the declarative ExtensionUi control tree. Requires the server to be
 * launched with --host-serial.
 *
 * The HostSerial messages are tunnelled over the core's ExtensionRpc channel;
 * the host-serial extension no longer hosts its own gRPC service. The public
 * API here is unchanged.
 */

import {
    HostSerialGetConfigRequest,
    HostSerialSetConfigRequest,
    HostSerialConfig as ProtoHostSerialConfig,
} from "./generated/host_serial.js";
import type { ExtensionChannel } from "./extension_rpc.js";

/** The logical service name the host-serial extension's dispatcher registers. */
const SERVICE = "HostSerial";

export interface HostSerialConfig {
    /** "pty" or "device". */
    mode: string;
    /** pty slave path, or the opened device path. */
    path: string;
    /** Host line speed (device mode; informational for pty). */
    baud: number;
    /** Is the host port currently open? */
    serialOpen: boolean;
    /** OS error text when serialOpen is false. */
    openError: string;
}

/** Fields to change in setConfig(); omitted fields are kept (a partial update). */
export interface HostSerialSetConfigOptions {
    /** Only "device" is accepted at runtime; "pty" is rejected. */
    mode?: string;
    path?: string;
    baud?: number;
}

/** Query and re-point the host-serial bridge. */
export class HostSerial {
    private readonly channel: ExtensionChannel;

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /** Read the current bridge configuration and open state. */
    async getConfig(): Promise<HostSerialConfig> {
        const payload = HostSerialGetConfigRequest.encode(
            HostSerialGetConfigRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetConfig", payload);
        return fromProto(ProtoHostSerialConfig.decode(reply));
    }

    /**
     * Re-point the bridge. A partial update: only the fields you pass change;
     * the rest are kept. mode may only be "device" at runtime (a pty is created
     * only at startup). The re-point applies on the next emulation tick, so the
     * returned config may still show the previous device until it lands -- call
     * getConfig() to confirm. Never blocks.
     */
    async setConfig(options: HostSerialSetConfigOptions = {}): Promise<HostSerialConfig> {
        // Only the present fields are serialized (proto3 optional), so the server
        // sees exactly the diff the caller asked for.
        const payload = HostSerialSetConfigRequest.encode(
            HostSerialSetConfigRequest.fromPartial({
                mode: options.mode,
                path: options.path,
                baud: options.baud,
            }),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "SetConfig", payload);
        return fromProto(ProtoHostSerialConfig.decode(reply));
    }
}

function fromProto(response: ProtoHostSerialConfig): HostSerialConfig {
    return {
        mode: response.mode,
        path: response.path,
        baud: response.baud,
        serialOpen: response.serialOpen,
        openError: response.openError,
    };
}
