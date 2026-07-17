import { describe, it, expect, vi } from "vitest";
import { RpcSerial } from "../src/rpc_serial.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import {
    RpcSerialSendRequest,
    RpcSerialSendResponse,
    RpcSerialReceiveRequest,
    RpcSerialReceiveResponse,
    RpcSerialStatus,
} from "../src/generated/rpc_serial.js";

type InvokeHandler = (service: string, method: string, payload: Uint8Array) => Uint8Array;

// A mock ExtensionChannel whose invoke() decodes the tunnelled request bytes
// and returns encoded response bytes, exactly as the real channel carries them.
function mockChannel(handler: InvokeHandler) {
    const invoke = vi.fn(
        async (service: string, method: string, payload: Uint8Array) =>
            handler(service, method, payload),
    );
    return { channel: { invoke } as unknown as ExtensionChannel, invoke };
}

describe("RpcSerial", () => {
    it("send tunnels the bytes and returns the accepted count", async () => {
        let seen: Uint8Array | undefined;
        const { channel, invoke } = mockChannel((_service, _method, payload) => {
            seen = RpcSerialSendRequest.decode(payload).data;
            return RpcSerialSendResponse.encode(
                RpcSerialSendResponse.fromPartial({ accepted: 3 }),
            ).finish();
        });
        const rpc = new RpcSerial(channel);

        const accepted = await rpc.send(new Uint8Array([1, 2, 3, 4]));
        expect(accepted).toBe(3);
        expect(invoke).toHaveBeenCalledWith("RpcSerial", "Send", expect.anything());
        expect(Buffer.from(seen!).equals(Buffer.from([1, 2, 3, 4]))).toBe(true);
    });

    it("receive tunnels maxBytes and returns the bytes", async () => {
        let seen: number | undefined;
        const { channel, invoke } = mockChannel((_service, _method, payload) => {
            seen = RpcSerialReceiveRequest.decode(payload).maxBytes;
            return RpcSerialReceiveResponse.encode(
                RpcSerialReceiveResponse.fromPartial({ data: Buffer.from([5, 6, 7]) }),
            ).finish();
        });
        const rpc = new RpcSerial(channel);

        const data = await rpc.receive(16);
        expect(seen).toBe(16);
        expect(invoke).toHaveBeenCalledWith("RpcSerial", "Receive", expect.anything());
        expect(Array.from(data)).toEqual([5, 6, 7]);
    });

    it("receive defaults maxBytes to 0", async () => {
        let seen: number | undefined;
        const { channel } = mockChannel((_service, _method, payload) => {
            seen = RpcSerialReceiveRequest.decode(payload).maxBytes;
            return RpcSerialReceiveResponse.encode(
                RpcSerialReceiveResponse.fromPartial({ data: Buffer.from([]) }),
            ).finish();
        });
        const rpc = new RpcSerial(channel);

        await rpc.receive();
        expect(seen).toBe(0);
    });

    it("getStatus returns the pending counts", async () => {
        const { channel, invoke } = mockChannel(() =>
            RpcSerialStatus.encode(
                RpcSerialStatus.fromPartial({ txPending: 12, rxPending: 5 }),
            ).finish(),
        );
        const rpc = new RpcSerial(channel);

        const status = await rpc.getStatus();
        expect(invoke).toHaveBeenCalledWith("RpcSerial", "GetStatus", expect.anything());
        expect(status.txPending).toBe(12);
        expect(status.rxPending).toBe(5);
    });
});
