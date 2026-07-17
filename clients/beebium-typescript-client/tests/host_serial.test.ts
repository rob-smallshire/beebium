import { describe, it, expect, vi } from "vitest";
import { HostSerial } from "../src/host_serial.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import {
    HostSerialSetConfigRequest,
    HostSerialConfig,
} from "../src/generated/host_serial.js";

type InvokeHandler = (service: string, method: string, payload: Uint8Array) => Uint8Array;

function mockChannel(handler: InvokeHandler) {
    const invoke = vi.fn(
        async (service: string, method: string, payload: Uint8Array) =>
            handler(service, method, payload),
    );
    return { channel: { invoke } as unknown as ExtensionChannel, invoke };
}

function encodeConfig(fields: Partial<HostSerialConfig>): Uint8Array {
    return HostSerialConfig.encode(HostSerialConfig.fromPartial(fields)).finish();
}

describe("HostSerial", () => {
    it("getConfig decodes the bridge configuration", async () => {
        const { channel, invoke } = mockChannel(() =>
            encodeConfig({
                mode: "device",
                path: "/dev/ttys003",
                baud: 9600,
                serialOpen: true,
                openError: "",
            }),
        );
        const host = new HostSerial(channel);

        const config = await host.getConfig();
        expect(invoke).toHaveBeenCalledWith("HostSerial", "GetConfig", expect.anything());
        expect(config).toEqual({
            mode: "device",
            path: "/dev/ttys003",
            baud: 9600,
            serialOpen: true,
            openError: "",
        });
    });

    it("setConfig tunnels only the fields provided (a partial update)", async () => {
        let seen: HostSerialSetConfigRequest | undefined;
        const { channel, invoke } = mockChannel((_service, _method, payload) => {
            seen = HostSerialSetConfigRequest.decode(payload);
            return encodeConfig({ mode: "device", path: "/dev/keep", baud: 2400 });
        });
        const host = new HostSerial(channel);

        await host.setConfig({ baud: 2400 });
        expect(invoke).toHaveBeenCalledWith("HostSerial", "SetConfig", expect.anything());
        // proto3 optional: an unset field stays absent so the server keeps it.
        expect(seen!.baud).toBe(2400);
        expect(seen!.mode).toBeUndefined();
        expect(seen!.path).toBeUndefined();
    });

    it("setConfig forwards every provided field", async () => {
        let seen: HostSerialSetConfigRequest | undefined;
        const { channel } = mockChannel((_service, _method, payload) => {
            seen = HostSerialSetConfigRequest.decode(payload);
            return encodeConfig({ mode: "device", path: "/dev/new", baud: 19200 });
        });
        const host = new HostSerial(channel);

        await host.setConfig({ mode: "device", path: "/dev/new", baud: 19200 });
        expect(seen!.mode).toBe("device");
        expect(seen!.path).toBe("/dev/new");
        expect(seen!.baud).toBe(19200);
    });
});
