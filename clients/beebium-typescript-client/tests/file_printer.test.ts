import { describe, it, expect, vi } from "vitest";
import { FilePrinter } from "../src/file_printer.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import {
    FilePrinterSetOnlineRequest,
    FilePrinterStatus,
} from "../src/generated/file_printer.js";

type InvokeHandler = (service: string, method: string, payload: Uint8Array) => Uint8Array;

function mockChannel(handler: InvokeHandler) {
    const invoke = vi.fn(
        async (service: string, method: string, payload: Uint8Array) =>
            handler(service, method, payload),
    );
    return { channel: { invoke } as unknown as ExtensionChannel, invoke };
}

function encodeStatus(fields: Partial<FilePrinterStatus>): Uint8Array {
    return FilePrinterStatus.encode(FilePrinterStatus.fromPartial(fields)).finish();
}

describe("FilePrinter", () => {
    it("getStatus decodes the path, counts and online state", async () => {
        const { channel, invoke } = mockChannel(() =>
            encodeStatus({
                path: "/tmp/printout.txt",
                bytesReceived: 6,
                bytesWritten: 6,
                online: true,
            }),
        );
        const printer = new FilePrinter(channel);

        const status = await printer.getStatus();
        expect(invoke).toHaveBeenCalledWith("FilePrinter", "GetStatus", expect.anything());
        expect(status).toEqual({
            path: "/tmp/printout.txt",
            bytesReceived: 6,
            bytesWritten: 6,
            online: true,
        });
    });

    it("setOnline tunnels the requested state and returns the new status", async () => {
        let seen: FilePrinterSetOnlineRequest | undefined;
        const { channel, invoke } = mockChannel((_service, _method, payload) => {
            seen = FilePrinterSetOnlineRequest.decode(payload);
            return encodeStatus({ path: "/tmp/printout.txt", online: false });
        });
        const printer = new FilePrinter(channel);

        const status = await printer.setOnline(false);
        expect(invoke).toHaveBeenCalledWith("FilePrinter", "SetOnline", expect.anything());
        expect(seen!.online).toBe(false);
        expect(status.online).toBe(false);
    });
});
