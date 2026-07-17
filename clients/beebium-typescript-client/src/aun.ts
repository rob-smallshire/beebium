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
    AunPeerSource as ProtoAunPeerSource,
} from "./generated/aun.js";
import type { ExtensionChannel } from "./extension_rpc.js";
import { EconetError } from "./exceptions.js";

/** The logical service name the AUN extension's dispatcher registers. */
const SERVICE = "AunService";

export interface AunStatus {
    connected: boolean;
    localPort: number;
    peerCount: number;
}

/**
 * Where an AUN peer entry came from.
 *
 * Operator-configured peers (CLI `--aun map=`, the preset's
 * `econet.transport.parameters`, or `addPeer()`) always take precedence
 * over discovered peers in the routing table.
 */
export enum PeerSource {
    OperatorConfigured = "operator-configured",
    Discovered = "discovered",
}

export interface PeerInfo {
    net: number;
    stn: number;
    ipAddress: string;
    port: number;
    /**
     * Provenance of this entry. `OperatorConfigured` for entries
     * added via `--aun map=` / preset / `addPeer`; `Discovered` for
     * entries auto-populated by the AUN extension's mDNS subscriber.
     * Older servers that don't carry the proto field default to
     * `OperatorConfigured` (the only kind they had).
     */
    source: PeerSource;
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

    constructor(channel: ExtensionChannel) {
        this.channel = channel;
    }

    /** Read the AUN backend status. */
    async getStatus(): Promise<AunStatus> {
        const payload = AunGetStatusRequest.encode(
            AunGetStatusRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "GetStatus", payload);
        const response = AunGetStatusResponse.decode(reply);
        return {
            connected: response.connected,
            localPort: response.localPort,
            peerCount: response.peerCount,
        };
    }

    /** Enumerate all configured AUN peers. */
    async listPeers(): Promise<PeerInfo[]> {
        const payload = AunListPeersRequest.encode(
            AunListPeersRequest.fromPartial({}),
        ).finish();
        const reply = await this.channel.invoke(SERVICE, "ListPeers", payload);
        const response = AunListPeersResponse.decode(reply);
        return response.peers.map((p) => ({
            net: p.net,
            stn: p.stn,
            ipAddress: p.ipAddress,
            port: p.port,
            // UNSPECIFIED collapses to OperatorConfigured so a newer
            // client reading an older server's response behaves the
            // same way it always has -- pre-discovery servers only
            // ever published operator-configured peers.
            source: p.source === ProtoAunPeerSource.AUN_PEER_SOURCE_DISCOVERED
                ? PeerSource.Discovered
                : PeerSource.OperatorConfigured,
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
        const reply = await this.channel.invoke(SERVICE, "SetConnected", payload);
        const response = AunSetConnectedResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }

    /**
     * Add an Econet address to UDP endpoint peer mapping.
     *
     * @param net - Econet network number (0-127).
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
        const reply = await this.channel.invoke(SERVICE, "AddPeer", payload);
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
        const reply = await this.channel.invoke(SERVICE, "RemovePeer", payload);
        const response = AunRemovePeerResponse.decode(reply);
        if (!response.success) {
            throw new EconetError(response.error);
        }
    }
}
