import { describe, it, expect, vi } from "vitest";
import { HostParallelPrinter } from "../src/host_parallel_printer.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import {
    HostParallelPrinterDeviceList,
    HostParallelPrinterStatus,
} from "../src/generated/host_parallel_printer.js";

describe("HostParallelPrinter", () => {
    it("getStatus decodes the device, open state and counts", async () => {
        const invoke = vi.fn(async () =>
            HostParallelPrinterStatus.encode(
                HostParallelPrinterStatus.fromPartial({
                    device: "/dev/usb/lp0",
                    open: true,
                    bytesReceived: 6,
                    bytesSent: 6,
                }),
            ).finish(),
        );
        const printer = new HostParallelPrinter({ invoke } as unknown as ExtensionChannel);

        const status = await printer.getStatus();
        expect(invoke).toHaveBeenCalledWith("HostParallelPrinter", "GetStatus", expect.anything());
        expect(status).toEqual({
            device: "/dev/usb/lp0",
            open: true,
            bytesReceived: 6,
            bytesSent: 6,
        });
    });

    it("listDevices decodes the paths and names", async () => {
        const invoke = vi.fn(async () =>
            HostParallelPrinterDeviceList.encode(
                HostParallelPrinterDeviceList.fromPartial({
                    devices: [{ path: "/dev/usb/lp0", name: "USB printer lp0" }],
                }),
            ).finish(),
        );
        const printer = new HostParallelPrinter({ invoke } as unknown as ExtensionChannel);

        const devices = await printer.listDevices();
        expect(invoke).toHaveBeenCalledWith(
            "HostParallelPrinter",
            "ListDevices",
            expect.anything(),
        );
        expect(devices).toEqual([{ path: "/dev/usb/lp0", name: "USB printer lp0" }]);
    });
});
