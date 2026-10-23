import { describe, it, expect, vi } from "vitest";
import { TcpPrinter } from "../src/tcp_printer.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import { TcpPrinterStatus } from "../src/generated/tcp_printer.js";

describe("TcpPrinter", () => {
    it("getStatus decodes the endpoint, connection and counts", async () => {
        const invoke = vi.fn(async () =>
            TcpPrinterStatus.encode(
                TcpPrinterStatus.fromPartial({
                    endpoint: "127.0.0.1:9100",
                    connected: true,
                    bytesReceived: 6,
                    bytesSent: 6,
                }),
            ).finish(),
        );
        const printer = new TcpPrinter({ invoke } as unknown as ExtensionChannel);

        const status = await printer.getStatus();
        expect(invoke).toHaveBeenCalledWith("TcpPrinter", "GetStatus", expect.anything());
        expect(status).toEqual({
            endpoint: "127.0.0.1:9100",
            connected: true,
            bytesReceived: 6,
            bytesSent: 6,
        });
    });
});
