import { describe, it, expect, vi } from "vitest";
import { Piconet } from "../src/piconet.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import {
    PiconetGetStatusRequest,
    PiconetGetStatusResponse,
} from "../src/generated/piconet_service.js";

type InvokeHandler = (service: string, method: string, payload: Uint8Array) => Uint8Array;

// A mock ExtensionChannel whose invoke() returns encoded status bytes, exactly
// as the real channel carries the PiconetService dispatcher's reply.
function mockChannel(handler: InvokeHandler) {
    const invoke = vi.fn(
        async (service: string, method: string, payload: Uint8Array) =>
            handler(service, method, payload),
    );
    return { channel: { invoke } as unknown as ExtensionChannel, invoke };
}

function encodeStatus(devicePath: string, serialOpen: boolean): Uint8Array {
    return PiconetGetStatusResponse.encode(
        PiconetGetStatusResponse.fromPartial({ devicePath, serialOpen }),
    ).finish();
}

describe("Piconet", () => {
    describe("getStatus", () => {
        it("reports a connected adapter", async () => {
            const { channel, invoke } = mockChannel(() =>
                encodeStatus("/dev/tty.usbmodem101", true),
            );
            const piconet = new Piconet(channel);

            const status = await piconet.getStatus();
            expect(invoke).toHaveBeenCalledWith(
                "PiconetService",
                "GetStatus",
                expect.anything(),
            );
            expect(status.devicePath).toBe("/dev/tty.usbmodem101");
            expect(status.serialOpen).toBe(true);
        });

        it("reports a disconnected adapter", async () => {
            const { channel } = mockChannel(() => encodeStatus("/dev/nonexistent", false));
            const piconet = new Piconet(channel);

            const status = await piconet.getStatus();
            expect(status.devicePath).toBe("/dev/nonexistent");
            expect(status.serialOpen).toBe(false);
        });

        it("tunnels an empty GetStatus request", async () => {
            let seenLength = -1;
            const { channel } = mockChannel((_service, _method, payload) => {
                // The request decodes cleanly and carries no fields.
                PiconetGetStatusRequest.decode(payload);
                seenLength = payload.length;
                return encodeStatus("", false);
            });
            const piconet = new Piconet(channel);

            await piconet.getStatus();
            expect(seenLength).toBe(0);
        });
    });
});
