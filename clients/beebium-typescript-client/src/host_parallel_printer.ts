/**
 * Client for the host-parallel-printer extension's typed API.
 *
 * The host-parallel-printer extension sends everything the BBC prints to a
 * printer device on the host: /dev/usb/lp0 or /dev/lp0 on Linux, or on
 * Windows a USB printer's interface path or LPTn. This client reads the
 * device, whether it is open and how much has been written -- the
 * scripting-friendly equivalent of the sidebar panel -- and lists the printer
 * devices present, to help choose the device parameter. Requires the server
 * to be launched with --host-parallel-printer device=<path>.
 *
 * There is no way to switch the printer online or offline: it is online
 * exactly while its device is open.
 *
 * The HostParallelPrinter messages are tunnelled over the core's
 * ExtensionRpc channel.
 */

import {
    HostParallelPrinterDeviceList,
    HostParallelPrinterGetStatusRequest,
    HostParallelPrinterListDevicesRequest,
    HostParallelPrinterStatus as ProtoHostParallelPrinterStatus,
} from "./generated/host_parallel_printer.js";
import type { ExtensionChannel } from "./extension_rpc.js";

/** The logical service name the host-parallel-printer extension's dispatcher registers. */
const SERVICE = "HostParallelPrinter";

export interface HostParallelPrinterStatus {
    /** The host device path, as configured. */
    device: string;
    /** Open, and so online. */
    open: boolean;
    /** Bytes accepted from the port since launch. */
    bytesReceived: number;
    /** Bytes written to the device. */
    bytesSent: number;
}

/** A printer device present on the host. */
export interface HostPrinterDevice {
    /** What to give as device=. */
    path: string;
    /** A friendly name. */
    name: string;
}

/** Read the host printer's status and list the host's printer devices. */
export class HostParallelPrinter {
    private readonly channel: ExtensionChannel;

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /** The device, whether it is open, and the byte counts. */
    async getStatus(): Promise<HostParallelPrinterStatus> {
        const payload = HostParallelPrinterGetStatusRequest.encode(
            HostParallelPrinterGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetStatus", payload);
        const response = ProtoHostParallelPrinterStatus.decode(reply);
        return {
            device: response.device,
            open: response.open,
            bytesReceived: response.bytesReceived,
            bytesSent: response.bytesSent,
        };
    }

    /**
     * The printer devices present on the host; empty when there are none. On
     * Linux the /dev/usb/lp* and /dev/lp* nodes, on Windows the USB printer
     * interface paths. Nothing is opened or chosen.
     */
    async listDevices(): Promise<HostPrinterDevice[]> {
        const payload = HostParallelPrinterListDevicesRequest.encode(
            HostParallelPrinterListDevicesRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "ListDevices", payload);
        return HostParallelPrinterDeviceList.decode(reply).devices.map((device) => ({
            path: device.path,
            name: device.name,
        }));
    }
}
