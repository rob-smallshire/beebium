/**
 * Client side of the generic ExtensionRpc channel.
 *
 * Extensions never host their own gRPC service (that would put two gRPC
 * runtimes in one process and crash -- see
 * docs/discussion/extension-rpc-channel.md). Instead the core hosts one
 * ExtensionRpc service that carries opaque serialized bytes; this helper wraps
 * that single stub so a per-extension client (e.g. RpcSerial) can tunnel its
 * typed messages through it.
 *
 * This is the hand-written equivalent of what a stub generator will emit later;
 * until then, extension clients encode their own request message, call
 * invoke()/serverStream(), and decode the reply.
 */

import type { ExtensionRpcClient, InvokeResponse } from "./generated/extension_rpc.js";
import type { InvokeRequest } from "./generated/extension_rpc.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";

/**
 * Thin wrapper over the core's ExtensionRpc stub.
 *
 * Routing: extensionId selects an instance -- a peripheral extension's id (from
 * PeripheralExtensionService) or an Econet transport's id (from
 * EconetTransportService). When it is empty the core routes by service name,
 * which succeeds only while exactly one loaded instance offers the service;
 * with more than one the call fails with FAILED_PRECONDITION naming the
 * candidate ids.
 */
/**
 * The instance an adapter routes its ExtensionRpc calls to: an id known up
 * front, or a function that discovers it on first use (for example by asking
 * EconetTransportService which transport instance is loaded).
 */
export type ExtensionIdSource = string | (() => Promise<string>);

/**
 * Resolves an ExtensionIdSource once and remembers the answer. A failed
 * discovery is not remembered, so the next call tries again.
 */
export class ExtensionIdResolver {
    private readonly source: ExtensionIdSource;
    private resolved?: Promise<string>;

    constructor(source: ExtensionIdSource = "") {
        this.source = source;
    }

    get(): Promise<string> {
        if (this.resolved === undefined) {
            const source = this.source;
            const pending =
                typeof source === "string" ? Promise.resolve(source) : source();
            this.resolved = pending;
            pending.catch(() => {
                if (this.resolved === pending) {
                    this.resolved = undefined;
                }
            });
        }
        return this.resolved;
    }
}

export class ExtensionChannel {
    private readonly stub: ExtensionRpcClient;

    constructor(stub: ExtensionRpcClient) {
        this.stub = stub;
    }

    /**
     * Unary call. Returns the response payload bytes.
     *
     * A non-OK extension status surfaces as the gRPC error it maps to (the same
     * ConnectionError a native service call would raise via promisify()).
     */
    async invoke(
        service: string,
        method: string,
        payload: Uint8Array,
        extensionId = "",
    ): Promise<Uint8Array> {
        const response = await promisify<InvokeRequest, InvokeResponse>(
            this.stub as unknown as Record<string, Function>,
            "invoke",
            { extensionId, service, method, payload: Buffer.from(payload), metadata: {} },
        );
        return new Uint8Array(response.payload);
    }

    /** Server-streaming call. Yields each response payload's bytes. */
    async *serverStream(
        service: string,
        method: string,
        payload: Uint8Array,
        extensionId = "",
    ): AsyncGenerator<Uint8Array, void, undefined> {
        const stream = this.stub.serverStream({
            extensionId,
            service,
            method,
            payload: Buffer.from(payload),
            metadata: {},
        });
        for await (const response of toAsyncIterable<InvokeResponse>(stream)) {
            yield new Uint8Array(response.payload);
        }
    }
}
