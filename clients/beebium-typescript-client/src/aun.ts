/**
 * AUN (Acorn Universal Networking) transport-specific operations.
 *
 * These RPCs are served by the AUN transport's AunService dispatcher,
 * tunnelled over the core's ExtensionRpc channel. They are reachable whenever
 * the server has the AUN transport loaded (--aun or a preset's
 * econet.transport), whether or not its socket is up yet. With no AUN
 * transport loaded the call fails with gRPC NOT_FOUND; use
 * bbc.transport.getActive() to see which transport, if any, is loaded.
 */

import {
    AunGetStatusRequest,
    AunGetStatusResponse,
    AunListPeersRequest,
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
    AunRemoveMapPeerRequest,
    AunRemoveMapPeerResponse,
    AunAddMapSubnetRequest,
    AunAddMapSubnetResponse,
    AunRemoveMapSubnetRequest,
    AunRemoveMapSubnetResponse,
    AunListMapRequest,
    AunListMapResponse,
    AunPeerSource as ProtoAunPeerSource,
} from "./generated/aun.js";
import { ExtensionIdResolver } from "./extension_rpc.js";
import type { ExtensionChannel, ExtensionIdSource } from "./extension_rpc.js";
import { EconetError } from "./exceptions.js";

/** The logical service name the AUN extension's dispatcher registers. */
const SERVICE = "AunService";

export interface AunStatus {
    /** True if the AUN socket is bound and the cable is plugged in. */
    connected: boolean;
    /** The bound UDP port, 0 while there is no socket. */
    localPort: number;
    /** Entries in the resolved routing table (what listPeers() returns). */
    peerCount: number;
    /**
     * The map file's path on the server's host (--aun map-file=, else
     * BEEBIUM_AUN_MAP_FILEPATH, else the per-user aun-map.json; empty when
     * disabled with map-file=none).
     */
    mapFilePath: string;
    /** Entry count (peers + subnets) from the last map-file load. */
    mapFileEntryCount: number;
    /** The last map-file load error, empty on success or an absent file. */
    mapFileError: string;
    /** The mDNS discovery mode set by --aun discovery=: "on", "announce", "browse" or "off". */
    discoveryMode: string;
}

/**
 * Where an AUN peer entry came from.
 *
 * Each (net, stn) resolves to one winner by precedence, highest first: `Api`,
 * `Launch`, `MapFile`, `Discovered`, `Subnet`. The operator sources
 * (`addPeer()`, CLI `--aun map=` / the preset's
 * `econet.transport.parameters`, and the map file) all beat discovered
 * peers. Removing the winner falls back to the next source still present.
 */
export enum PeerSource {
    Launch = "launch",
    Api = "api",
    MapFile = "map-file",
    Discovered = "discovered",
    Subnet = "subnet",
}

export interface PeerInfo {
    net: number;
    stn: number;
    ipAddress: string;
    port: number;
    /**
     * The source this resolved entry won from: `Launch` (`--aun map=` /
     * preset), `Api` (a runtime `addPeer`), `MapFile` (the per-user map
     * file), `Discovered` (the AUN extension's mDNS subscriber), or `Subnet`
     * (materialised from a subnet rule: the map file's `subnets` or
     * `--aun subnet=`). Precedence, highest
     * first: `Api`, `Launch`, `MapFile`, `Discovered`, `Subnet`.
     */
    source: PeerSource;
}

/** A peers[] entry in the map file, with its host-resolution state. */
export interface MapPeer {
    net: number;
    stn: number;
    host: string;        // as written (IPv4 literal or DNS name)
    port: number;
    label: string;
    resolved: boolean;
    resolvedIp: string;  // dotted-quad when resolved, else empty
}

/** A subnets[] entry in the map file. */
export interface MapSubnet {
    net: number;
    subnet: string;
    label: string;
}

/** The map file's entries (distinct from the live routing table). */
export interface MapListing {
    peers: MapPeer[];
    subnets: MapSubnet[];
}

function peerSourceFromProto(source: ProtoAunPeerSource): PeerSource {
    switch (source) {
        case ProtoAunPeerSource.AUN_PEER_SOURCE_API:
            return PeerSource.Api;
        case ProtoAunPeerSource.AUN_PEER_SOURCE_MAP_FILE:
            return PeerSource.MapFile;
        case ProtoAunPeerSource.AUN_PEER_SOURCE_DISCOVERED:
            return PeerSource.Discovered;
        case ProtoAunPeerSource.AUN_PEER_SOURCE_SUBNET:
            return PeerSource.Subnet;
        default:
            // LAUNCH and the unspecified default both read as launch config.
            return PeerSource.Launch;
    }
}

/**
 * AUN-specific RPCs (peer table, map file, cable plug, status).
 *
 * Available whenever the server has the AUN transport loaded; reach it as
 * bbc.aun (or bbc.aunInstance(id)). Check bbc.transport.getActive() first if
 * your code might run against a server configured for Piconet or no
 * transport.
 */
export class Aun {
    private readonly channel: ExtensionChannel;
    private readonly extensionId: ExtensionIdResolver;

    /**
     * @param channel The ExtensionRpc channel that carries the messages.
     * @param extensionId The transport instance to address: the id that
     *     EconetTransportService reports for it, or a function that discovers
     *     it on first use. Empty routes by service name, which the server
     *     accepts only while one loaded instance offers the service.
     */
    constructor(channel: ExtensionChannel, extensionId: ExtensionIdSource = "") {
        this.channel = channel;
        this.extensionId = new ExtensionIdResolver(extensionId);
    }

    /** Unary call to this transport instance's dispatcher. */
    private async invoke(method: string, payload: Uint8Array): Promise<Uint8Array> {
        return this.channel.invoke(SERVICE, method, payload, await this.extensionId.get());
    }

    /** Read the AUN transport status (link, port, peers, map file, discovery mode). */
    async getStatus(): Promise<AunStatus> {
        const payload = AunGetStatusRequest.encode(
            AunGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.invoke("GetStatus", payload);
        const response = AunGetStatusResponse.decode(reply);
        return {
            connected: response.connected,
            localPort: response.localPort,
            peerCount: response.peerCount,
            mapFilePath: response.mapFilePath,
            mapFileEntryCount: response.mapFileEntryCount,
            mapFileError: response.mapFileError,
            discoveryMode: response.discoveryMode || "on",
        };
    }

    /**
     * The resolved routing table: one entry per (net, stn), carrying the
     * winning source. Map-file peers whose host did not resolve are not
     * here; see listMap().
     */
    async listPeers(): Promise<PeerInfo[]> {
        const payload = AunListPeersRequest.encode(
            AunListPeersRequest.fromPartial({}),
        ).finish();
        const reply = await this.invoke("ListPeers", payload);
        const response = AunListPeersResponse.decode(reply);
        return response.peers.map((p) => ({
            net: p.net,
            stn: p.stn,
            ipAddress: p.ipAddress,
            port: p.port,
            source: peerSourceFromProto(p.source),
        }));
    }

    /**
     * Plug or unplug the simulated network cable.
     *
     * While disconnected the ADLC sees DCD high (no carrier). Before the AUN
     * socket is up the state is remembered and applied when it comes up.
     *
     * @throws EconetError if the server reports the call failed.
     */
    async setConnected(connected: boolean): Promise<void> {
        const payload = AunSetConnectedRequest.encode(
            AunSetConnectedRequest.fromPartial({ connected }),
        ).finish();
        const reply = await this.invoke("SetConnected", payload);
        const response = AunSetConnectedResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Add or replace this client's (`Api`) peer mapping for an Econet address.
     *
     * `Api` entries take precedence over every other source for that
     * (net, stn). They work before the AUN socket is up and are not written to
     * the map file; use addMapPeer() for a peer every instance should share.
     *
     * @param net - Econet network number (0-255).
     * @param stn - Econet station number (1-254).
     * @param ipAddress - Dotted-quad IPv4 address (a DNS name is rejected).
     * @param port - UDP port (0 = use AUN default 32768).
     * @throws EconetError on a validation error (net, stn or ipAddress).
     */
    async addPeer(
        net: number,
        stn: number,
        ipAddress: string,
        port: number = 0,
    ): Promise<void> {
        const payload = AunAddPeerRequest.encode(
            AunAddPeerRequest.fromPartial({ net, stn, ipAddress, port }),
        ).finish();
        const reply = await this.invoke("AddPeer", payload);
        const response = AunAddPeerResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Remove the `Api` entry addPeer() made for an Econet address. Entries
     * from other sources are untouched, so a station also named by the launch
     * config, the map file or mDNS falls back to that entry. Removing an
     * address with no `Api` entry is not an error.
     *
     * @throws EconetError if the server reports the call failed.
     */
    async removePeer(net: number, stn: number): Promise<void> {
        const payload = AunRemovePeerRequest.encode(
            AunRemovePeerRequest.fromPartial({ net, stn }),
        ).finish();
        const reply = await this.invoke("RemovePeer", payload);
        const response = AunRemovePeerResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Re-read the map file on the server now, replacing only its
     * contributions (`MapFile` peers, the map file's subnet rules, and the
     * `Subnet` peers materialised from subnet rules); `Api`, `Launch` and
     * `Discovered` entries and `--aun subnet=` rules are untouched. A
     * modification is picked up automatically by the mtime poll while
     * discovery browses (discovery=on or browse); this forces it. With the
     * map file disabled (map-file=none) it does nothing.
     *
     * @throws {EconetError} If the file was present but could not be parsed.
     */
    async reloadMap(): Promise<void> {
        const payload = AunReloadMapRequest.encode(
            AunReloadMapRequest.fromPartial({}),
        ).finish();
        const reply = await this.invoke("ReloadMap", payload);
        const response = AunReloadMapResponse.decode(reply);
        if (response.error) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Add or replace a peer in the server's aun-map.json. The server writes its
     * own file atomically (preserving order and unknown keys) and applies the
     * change to its peer set at once; other instances pick it up from the poll.
     *
     * @throws {EconetError} On a validation or write error (names the field),
     *     or when the map file is disabled (map-file=none).
     */
    async addMapPeer(
        net: number,
        stn: number,
        host: string,
        port = 32768,
        label = "",
    ): Promise<void> {
        const payload = AunAddMapPeerRequest.encode(
            AunAddMapPeerRequest.fromPartial({ net, stn, host, port, label }),
        ).finish();
        const reply = await this.invoke("AddMapPeer", payload);
        const response = AunAddMapPeerResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Remove a peer from the map file. Resolves to whether an entry was removed.
     *
     * @throws {EconetError} On a read or write error, or when the map file is
     *     disabled.
     */
    async removeMapPeer(net: number, stn: number): Promise<boolean> {
        const payload = AunRemoveMapPeerRequest.encode(
            AunRemoveMapPeerRequest.fromPartial({ net, stn }),
        ).finish();
        const reply = await this.invoke("RemoveMapPeer", payload);
        const response = AunRemoveMapPeerResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
        return response.removed;
    }

    /**
     * Add or replace the subnet rule for `net` (`a.b.c.0/24`) in the map file:
     * every station on the net maps to `a.b.c.<station>` port 32768.
     *
     * @throws {EconetError} On a validation or write error, or when the map
     *     file is disabled.
     */
    async addMapSubnet(net: number, subnet: string, label = ""): Promise<void> {
        const payload = AunAddMapSubnetRequest.encode(
            AunAddMapSubnetRequest.fromPartial({ net, subnet, label }),
        ).finish();
        const reply = await this.invoke("AddMapSubnet", payload);
        const response = AunAddMapSubnetResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Remove a subnet rule from the map file. Resolves to whether one was removed.
     *
     * @throws {EconetError} On a read or write error, or when the map file is
     *     disabled.
     */
    async removeMapSubnet(net: number): Promise<boolean> {
        const payload = AunRemoveMapSubnetRequest.encode(
            AunRemoveMapSubnetRequest.fromPartial({ net }),
        ).finish();
        const reply = await this.invoke("RemoveMapSubnet", payload);
        const response = AunRemoveMapSubnetResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
        return response.removed;
    }

    /**
     * List the map file's entries with labels and host resolution, distinct
     * from listPeers() which lists the live resolved routing table. Empty
     * when the map file is disabled.
     *
     * @throws {EconetError} If the file was present but could not be parsed.
     */
    async listMap(): Promise<MapListing> {
        const payload = AunListMapRequest.encode(
            AunListMapRequest.fromPartial({}),
        ).finish();
        const reply = await this.invoke("ListMap", payload);
        const response = AunListMapResponse.decode(reply);
        if (response.error) {
            throw new EconetError(response.error);
        }
        return {
            peers: response.peers.map((p) => ({
                net: p.net,
                stn: p.stn,
                host: p.host,
                port: p.port,
                label: p.label,
                resolved: p.resolved,
                resolvedIp: p.resolvedIp,
            })),
            subnets: response.subnets.map((s) => ({
                net: s.net,
                subnet: s.subnet,
                label: s.label,
            })),
        };
    }
}
