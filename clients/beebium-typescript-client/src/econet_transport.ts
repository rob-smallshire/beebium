/**
 * Econet transport discovery (which transport is loaded on the server).
 *
 * Wraps EconetTransportService. Use this to decide whether to drive
 * bbc.aun or bbc.piconet (or any future transport-specific service):
 * each transport extension has a canonical name ("aun", "piconet",
 * ...) which maps one-to-one with the corresponding service.
 */

import type {
    EconetTransportServiceClient,
    ListTransportsResponse as ProtoListTransportsResponse,
    GetActiveTransportResponse as ProtoGetActiveTransportResponse,
} from "./generated/econet_transport.js";
import { promisify } from "./call-utils.js";
import { EconetError } from "./exceptions.js";

export interface TransportInfo {
    /**
     * Canonical extension name -- "aun", "piconet", etc. Maps to the
     * transport-specific service (AunService, PiconetService) the client
     * reaches over ExtensionRpc.
     */
    name: string;

    /** Human-readable description from the extension manifest. */
    description: string;

    /**
     * True if this transport is the one the server uses for Econet. On a BBC
     * machine every loaded transport is active, even before Econet hardware
     * is fitted or while its backend is down; Econet.getStatus() gives the
     * link state.
     */
    active: boolean;

    /**
     * Opaque, server-assigned instance id. This is the key to pass to
     * ExtensionUiService (SubscribeView / Dispatch) to drive this
     * transport's control panel, and the extensionId that routes the
     * transport's typed RPCs over ExtensionRpc. Discover it here rather
     * than hardcoding names.
     */
    id: string;

    /**
     * True if this transport implements an Extension UI, so a frontend
     * can decide whether to render an ExtensionUiService panel for it.
     */
    hasUi: boolean;
}

/**
 * Discover which Econet transport extension is active.
 *
 * Usage:
 *     const active = await bbc.transport.getActive();
 *     if (active === undefined) {
 *         console.log("No Econet transport configured");
 *     } else if (active.name === "aun") {
 *         await bbc.aun.addPeer(0, 254, "192.168.1.10");
 *     } else if (active.name === "piconet") {
 *         console.log((await bbc.piconet.getStatus()).devicePath);
 *     }
 */
export class EconetTransport {
    private readonly stub: EconetTransportServiceClient;

    constructor(stub: EconetTransportServiceClient) {
        this.stub = stub;
    }

    /**
     * List all econet transports the server knows about.
     *
     * Returns one entry per loaded transport extension. BBC machine
     * variants load at most one, and report it active.
     */
    async list(): Promise<TransportInfo[]> {
        const response = await promisify<{}, ProtoListTransportsResponse>(
            this.stub as unknown as Record<string, Function>,
            "listTransports",
            {},
        );
        return response.transports.map((t) => ({
            name: t.name,
            description: t.description,
            active: t.active,
            id: t.id,
            hasUi: t.hasUi,
        }));
    }

    /** The single active transport, or undefined if none configured. */
    async getActive(): Promise<TransportInfo | undefined> {
        const response = await promisify<{}, ProtoGetActiveTransportResponse>(
            this.stub as unknown as Record<string, Function>,
            "getActiveTransport",
            {},
        );
        if (!response.active) {
            return undefined;
        }
        return {
            name: response.active.name,
            description: response.active.description,
            active: response.active.active,
            id: response.active.id,
            hasUi: response.active.hasUi,
        };
    }

    /**
     * The ExtensionRpc routing id for the transport called `name`.
     *
     * Returns the id of the single loaded transport with that name. When
     * none is loaded the result is empty, which leaves the server to route
     * by service name and report the missing transport itself. When several
     * are loaded the name cannot choose between them: this throws an
     * EconetError naming their ids, any of which addresses one instance.
     */
    async routingId(name: string): Promise<string> {
        const matches = (await this.list()).filter((t) => t.name === name);
        if (matches.length > 1) {
            const ids = matches.map((t) => t.id).join(", ");
            throw new EconetError(
                `${matches.length} instances of Econet transport '${name}' are loaded; ` +
                    `address one by its id: ${ids}`,
            );
        }
        return matches.length === 1 ? matches[0]!.id : "";
    }
}
