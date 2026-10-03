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
    AunReloadMapRequest,
    AunReloadMapResponse,
    AunAddMapPeerRequest,
    AunAddMapPeerResponse,
    AunRemoveMapPeerResponse,
    AunAddMapSubnetRequest,
    AunAddMapSubnetResponse,
    AunListMapResponse,
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

        it("maps the map-file fields", async () => {
            const { channel } = fakeChannel({
                GetStatus: ok(AunGetStatusResponse, {
                    connected: true,
                    localPort: 32768,
                    peerCount: 2,
                    mapFilePath: "/home/u/.config/beebium/aun-map.json",
                    mapFileEntryCount: 2,
                    mapFileError: "",
                }),
            });
            const status = await new Aun(channel).getStatus();
            expect(status.mapFilePath).toBe("/home/u/.config/beebium/aun-map.json");
            expect(status.mapFileEntryCount).toBe(2);
            expect(status.mapFileError).toBe("");
        });
    });

    describe("reloadMap", () => {
        it("invokes ReloadMap and resolves on success", async () => {
            const { channel, request } = fakeChannel({
                ReloadMap: ok(AunReloadMapResponse, { reloaded: true, error: "" }),
            });
            await new Aun(channel).reloadMap();
            request("ReloadMap", AunReloadMapRequest);  // throws if not invoked
        });

        it("throws when the map file could not be parsed", async () => {
            const { channel } = fakeChannel({
                ReloadMap: ok(AunReloadMapResponse, {
                    reloaded: true,
                    error: "aun-map.json: invalid JSON at byte 11",
                }),
            });
            await expect(new Aun(channel).reloadMap()).rejects.toThrow(EconetError);
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
                            source: AunPeerSource.AUN_PEER_SOURCE_API,
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
                source: PeerSource.Api,
            });
            expect(peers[1]!.stn).toBe(100);
            expect(peers[1]!.ipAddress).toBe("10.0.0.1");
            expect(peers[1]!.source).toBe(PeerSource.Discovered);
        });

        it("maps launch and map-file sources to their provenance", async () => {
            const { channel } = fakeChannel({
                ListPeers: ok(AunListPeersResponse, {
                    peers: [
                        {
                            net: 0,
                            stn: 254,
                            ipAddress: "192.168.1.10",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_LAUNCH,
                        },
                        {
                            net: 0,
                            stn: 253,
                            ipAddress: "192.168.1.11",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_MAP_FILE,
                        },
                        {
                            net: 128,
                            stn: 44,
                            ipAddress: "192.168.1.44",
                            port: 32768,
                            source: AunPeerSource.AUN_PEER_SOURCE_SUBNET,
                        },
                    ],
                }),
            });
            const peers = await new Aun(channel).listPeers();
            expect(peers[0]!.source).toBe(PeerSource.Launch);
            expect(peers[1]!.source).toBe(PeerSource.MapFile);
            expect(peers[2]!.source).toBe(PeerSource.Subnet);
        });

        it("maps UNSPECIFIED source to Launch", async () => {
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
            expect(peers[0]!.source).toBe(PeerSource.Launch);
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

    describe("map file edits", () => {
        it("addMapPeer sends the fields and resolves on success", async () => {
            const { channel, request } = fakeChannel({
                AddMapPeer: ok(AunAddMapPeerResponse, { success: true, error: "" }),
            });
            await new Aun(channel).addMapPeer(0, 254, "192.168.1.10", 32768, "fs");
            const req = request("AddMapPeer", AunAddMapPeerRequest);
            expect(req.stn).toBe(254);
            expect(req.host).toBe("192.168.1.10");
            expect(req.label).toBe("fs");
        });

        it("addMapPeer throws on a validation error", async () => {
            const { channel } = fakeChannel({
                AddMapPeer: ok(AunAddMapPeerResponse, {
                    success: false,
                    error: "station must be 1-254",
                }),
            });
            await expect(
                new Aun(channel).addMapPeer(0, 0, "192.168.1.10"),
            ).rejects.toThrow(EconetError);
        });

        it("removeMapPeer returns whether an entry was removed", async () => {
            const { channel } = fakeChannel({
                RemoveMapPeer: ok(AunRemoveMapPeerResponse, {
                    success: true,
                    error: "",
                    removed: true,
                }),
            });
            expect(await new Aun(channel).removeMapPeer(0, 254)).toBe(true);
        });

        it("addMapSubnet sends the fields", async () => {
            const { channel, request } = fakeChannel({
                AddMapSubnet: ok(AunAddMapSubnetResponse, {
                    success: true,
                    error: "",
                }),
            });
            await new Aun(channel).addMapSubnet(128, "192.168.5.0/24", "risc os");
            const req = request("AddMapSubnet", AunAddMapSubnetRequest);
            expect(req.net).toBe(128);
            expect(req.subnet).toBe("192.168.5.0/24");
        });

        it("listMap maps entries and resolution state", async () => {
            const { channel } = fakeChannel({
                ListMap: ok(AunListMapResponse, {
                    peers: [
                        {
                            net: 0,
                            stn: 254,
                            host: "risc.local",
                            port: 32768,
                            label: "fs",
                            resolved: false,
                            resolvedIp: "",
                        },
                    ],
                    subnets: [{ net: 128, subnet: "192.168.5.0/24", label: "" }],
                    error: "",
                }),
            });
            const listing = await new Aun(channel).listMap();
            expect(listing.peers).toHaveLength(1);
            expect(listing.peers[0]!.host).toBe("risc.local");
            expect(listing.peers[0]!.resolved).toBe(false);
            expect(listing.subnets[0]!.subnet).toBe("192.168.5.0/24");
        });

        it("listMap throws when the file is malformed", async () => {
            const { channel } = fakeChannel({
                ListMap: ok(AunListMapResponse, {
                    peers: [],
                    subnets: [],
                    error: "aun-map.json: invalid JSON at byte 11",
                }),
            });
            await expect(new Aun(channel).listMap()).rejects.toThrow(EconetError);
        });
    });

    describe("ExtensionRpc routing", () => {
        const status = { GetStatus: ok(AunGetStatusResponse, { connected: true }) };

        it("routes by service name when no instance id is given", async () => {
            const { channel } = fakeChannel(status);
            await new Aun(channel).getStatus();
            const invoke = (channel as any).invoke;
            expect(invoke.mock.calls[0][3]).toBe("");
        });

        it("passes a fixed transport instance id on every call", async () => {
            const { channel } = fakeChannel({
                ...status,
                ListPeers: ok(AunListPeersResponse, { peers: [] }),
            });
            const aun = new Aun(channel, "aun-1");
            await aun.getStatus();
            await aun.listPeers();
            const invoke = (channel as any).invoke;
            expect(invoke.mock.calls[0][3]).toBe("aun-1");
            expect(invoke.mock.calls[1][3]).toBe("aun-1");
        });

        it("resolves a deferred instance id once and reuses it", async () => {
            const { channel } = fakeChannel(status);
            const resolve = vi.fn(async () => "aun-resolved");
            const aun = new Aun(channel, resolve);
            await aun.getStatus();
            await aun.getStatus();
            const invoke = (channel as any).invoke;
            expect(invoke.mock.calls[0][3]).toBe("aun-resolved");
            expect(invoke.mock.calls[1][3]).toBe("aun-resolved");
            expect(resolve).toHaveBeenCalledTimes(1);
        });

        it("retries the deferred id after a failed resolution", async () => {
            const { channel } = fakeChannel(status);
            const resolve = vi
                .fn<() => Promise<string>>()
                .mockRejectedValueOnce(new Error("unreachable"))
                .mockResolvedValue("aun-late");
            const aun = new Aun(channel, resolve);
            await expect(aun.getStatus()).rejects.toThrow("unreachable");
            await aun.getStatus();
            const invoke = (channel as any).invoke;
            expect(invoke.mock.calls[0][3]).toBe("aun-late");
        });
    });
});
