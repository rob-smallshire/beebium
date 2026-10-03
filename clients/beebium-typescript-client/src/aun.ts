/**
 * AUN (Acorn Universal Networking) transport-specific operations.
 *
 * These RPCs are surfaced by the AunService dispatcher when AUN is the active
 * Econet transport on the server. Use bbc.transport.getActive() to confirm AUN
 * is the active transport before calling these methods; otherwise the call
 * returns an error indicating "AUN backend is not active".
 *
 * The AUN messages are tunnelled over the core's ExtensionRpc channel; the AUN
 * extension no longer hosts its own gRPC service. The public API here is
 * unchanged.
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
    connected: boolean;
    localPort: number;
    peerCount: number;
    /** The per-user aun-map.json path on the server's host (empty if disabled). */
    mapFilePath: string;
    /** Entry count (peers + subnets) from the last map-file load. */
    mapFileEntryCount: number;
    /** The last map-file load error, empty on success or an absent file. */
    mapFileError: string;
    /** The mDNS discovery mode: "on", "announce", "browse" or "off" (#158). */
    discoveryMode: string;
}

/**
 * Where an AUN peer entry came from.
 *
 * Operator-configured peers (CLI `--aun map=`, the preset's
 * `econet.transport.parameters`, or `addPeer()`) always take precedence
 * over discovered peers in the routing table.
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
     * (derived from the map file's subnet convention). Precedence, highest
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
 * AUN-specific RPCs (peer table, cable plug, port status).
 *
 * Available on the server's gRPC surface only when AUN is the active
 * Econet transport. Check bbc.transport.getActive() first if your
 * code might run against a server configured for Piconet or no
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

    /** Read the AUN backend status. */
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

    /** Enumerate all configured AUN peers. */
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
     * While disconnected the ADLC sees DCD high (no carrier).
     *
     * @throws EconetError if the AUN backend is not active or the call fails.
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
     * Add an Econet address to UDP endpoint peer mapping.
     *
     * @param net - Econet network number (0-255).
     * @param stn - Econet station number (1-254).
     * @param ipAddress - Dotted-quad IP address.
     * @param port - UDP port (0 = use AUN default 32768).
     * @throws EconetError if the call fails.
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
     * Remove a peer mapping by Econet address.
     *
     * @throws EconetError if the call fails.
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
     * Re-read the per-user aun-map.json on the server now, replacing only the
     * map file's contributions (`MapFile` peers and the subnet rules); `Api`,
     * `Launch` and `Discovered` entries are untouched. A modification is
     * normally picked up automatically on the poll; this forces it.
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
     * @throws {EconetError} On a validation or write error (names the field).
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
     * @throws {EconetError} On a write error.
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
     * Add or replace a subnet rule (`a.b.c.0/24`) in the map file.
     *
     * @throws {EconetError} On a validation or write error.
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
     * @throws {EconetError} On a write error.
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
     * from listPeers() which lists the live resolved routing table.
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
