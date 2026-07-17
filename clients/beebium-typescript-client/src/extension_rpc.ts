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
 * Routing: when extensionId is empty the core routes by service name, which is
 * unambiguous while an extension type is a singleton (the common case today). A
 * future discovery method will let a client target a specific instance.
 */
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
