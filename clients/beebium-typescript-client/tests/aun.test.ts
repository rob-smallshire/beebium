import { describe, it, expect, vi } from "vitest";
import { Aun, PeerSource } from "../src/aun.js";
import type { ExtensionChannel } from "../src/extension_rpc.js";
import { EconetError } from "../src/exceptions.js";
import {
    AunGetStatusResponse,
    AunListPeersResponse,
    AunSetConnectedRequest,
    AunSetConnectedResponse,
    AunAddPeerRequest,
    AunAddPeerResponse,
    AunRemovePeerRequest,
    AunRemovePeerResponse,
    AunPeerSource,
} from "../src/generated/aun.js";

type Responder = () => Uint8Array;

// A fake ExtensionChannel: invoke() returns the method's encoded response bytes
// and records the (decodable) request bytes, exactly as the real channel carries
// them to/from the AunService dispatcher.
function fakeChannel(responders: Record<string, Responder>) {
    const calls: Array<{ service: string; method: string; payload: Uint8Array }> = [];
    const invoke = vi.fn(
        async (service: string, method: string, payload: Uint8Array) => {
            calls.push({ service, method, payload });
            return responders[method]();
        },
    );
    function request<T>(method: string, type: { decode: (b: Uint8Array) => T }): T {
        const call = calls.find((c) => c.method === method);
        if (!call) throw new Error(`${method} was not invoked`);
        expect(call.service).toBe("AunService");
        return type.decode(call.payload);
    }
    return { channel: { invoke } as unknown as ExtensionChannel, request };
}

const ok = (
    msg: { encode: (m: any) => { finish: () => Uint8Array }; fromPartial: (f: any) => any },
    fields: any,
) => () => msg.encode(msg.fromPartial(fields)).finish();

describe("Aun", () => {
    describe("getStatus", () => {
        it("maps all fields from the response", async () => {
            const { channel } = fakeChannel({
                GetStatus: ok(AunGetStatusResponse, {
                    connected: true,
                    localPort: 32768,
                    peerCount: 3,
                }),
            });
            const status = await new Aun(channel).getStatus();
            expect(status.connected).toBe(true);
            expect(status.localPort).toBe(32768);
            expect(status.peerCount).toBe(3);
        });

        it("reports disconnected status", async () => {
            const { channel } = fakeChannel({
                GetStatus: ok(AunGetStatusResponse, {
                    connected: false,
                    localPort: 0,
                    peerCount: 0,
                }),
            });
            const status = await new Aun(channel).getStatus();
            expect(status.connected).toBe(false);
            expect(status.localPort).toBe(0);
            expect(status.peerCount).toBe(0);
        });
    });

    describe("listPeers", () => {
        it("returns empty list when no peers configured", async () => {
            const { channel } = fakeChannel({
                ListPeers: ok(AunListPeersResponse, { peers: [] }),
            });
            expect(await new Aun(channel).listPeers()).toEqual([]);
        });

        it("maps each peer entry to PeerInfo", async () => {
            const { channel } = fakeChannel({
                ListPeers: ok(AunListPeersResponse, {
                    peers: [
                        {
                            net: 0,
                            stn: 254,
                            ipAddress: "192.168.1.10",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_OPERATOR_CONFIGURED,
                        },
                        {
                            net: 0,
                            stn: 100,
                            ipAddress: "10.0.0.1",
                            port: 33000,
                            source: AunPeerSource.AUN_PEER_SOURCE_DISCOVERED,
                        },
                    ],
                }),
            });
            const peers = await new Aun(channel).listPeers();
            expect(peers).toHaveLength(2);
            expect(peers[0]).toEqual({
                net: 0,
                stn: 254,
                ipAddress: "192.168.1.10",
                port: 32768,
                source: PeerSource.OperatorConfigured,
            });
            expect(peers[1]!.stn).toBe(100);
            expect(peers[1]!.ipAddress).toBe("10.0.0.1");
            expect(peers[1]!.source).toBe(PeerSource.Discovered);
        });

        it("falls back to OperatorConfigured for UNSPECIFIED source", async () => {
            const { channel } = fakeChannel({
                ListPeers: ok(AunListPeersResponse, {
                    peers: [
                        {
                            net: 0,
                            stn: 254,
                            ipAddress: "192.168.1.10",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_UNSPECIFIED,
                        },
                    ],
                }),
            });
            const peers = await new Aun(channel).listPeers();
            expect(peers[0]!.source).toBe(PeerSource.OperatorConfigured);
        });
    });

    describe("setConnected", () => {
        it("tunnels connected=true and resolves on success", async () => {
            const { channel, request } = fakeChannel({
                SetConnected: ok(AunSetConnectedResponse, { success: true }),
            });
            await new Aun(channel).setConnected(true);
            expect(request("SetConnected", AunSetConnectedRequest).connected).toBe(true);
        });

        it("tunnels connected=false", async () => {
            const { channel, request } = fakeChannel({
                SetConnected: ok(AunSetConnectedResponse, { success: true }),
            });
            await new Aun(channel).setConnected(false);
            expect(request("SetConnected", AunSetConnectedRequest).connected).toBe(false);
        });

        it("throws EconetError on failure", async () => {
            const { channel } = fakeChannel({
                SetConnected: ok(AunSetConnectedResponse, {
                    success: false,
                    error: "AUN backend is not active",
                }),
            });
            await expect(new Aun(channel).setConnected(true)).rejects.toThrow(
                "AUN backend is not active",
            );
        });
    });

    describe("addPeer", () => {
        it("tunnels all fields and defaults port to 0", async () => {
            const { channel, request } = fakeChannel({
                AddPeer: ok(AunAddPeerResponse, { success: true }),
            });
            await new Aun(channel).addPeer(0, 254, "192.168.1.10");
            const req = request("AddPeer", AunAddPeerRequest);
            expect(req.net).toBe(0);
            expect(req.stn).toBe(254);
            expect(req.ipAddress).toBe("192.168.1.10");
            expect(req.port).toBe(0);
        });

        it("passes explicit port when given", async () => {
            const { channel, request } = fakeChannel({
                AddPeer: ok(AunAddPeerResponse, { success: true }),
            });
            await new Aun(channel).addPeer(1, 100, "10.0.0.1", 33000);
            expect(request("AddPeer", AunAddPeerRequest).port).toBe(33000);
        });

        it("throws EconetError on failure", async () => {
            const { channel } = fakeChannel({
                AddPeer: ok(AunAddPeerResponse, {
                    success: false,
                    error: "peer already exists",
                }),
            });
            await expect(new Aun(channel).addPeer(0, 254, "1.2.3.4")).rejects.toThrow(
                "peer already exists",
            );
        });
    });

    describe("removePeer", () => {
        it("tunnels net and stn", async () => {
            const { channel, request } = fakeChannel({
                RemovePeer: ok(AunRemovePeerResponse, { success: true }),
            });
            await new Aun(channel).removePeer(0, 254);
            const req = request("RemovePeer", AunRemovePeerRequest);
            expect(req.net).toBe(0);
            expect(req.stn).toBe(254);
        });

        it("throws EconetError on failure", async () => {
            const { channel } = fakeChannel({
                RemovePeer: ok(AunRemovePeerResponse, {
                    success: false,
                    error: "peer not found",
                }),
            });
            await expect(new Aun(channel).removePeer(0, 1)).rejects.toThrow(
                "peer not found",
            );
        });
    });
});
