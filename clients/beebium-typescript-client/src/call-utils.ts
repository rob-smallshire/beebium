/**
 * Promisification helpers for callback-based gRPC client methods.
 */

import type { Metadata, ServiceError } from "@grpc/grpc-js";
import { ConnectionError } from "./exceptions.js";

/**
 * Converts a callback-based gRPC unary call to a Promise.
 *
 * @param client - The gRPC client instance.
 * @param method - The method name on the client to call.
 * @param request - The protobuf request message.
 * @returns A promise that resolves with the response or rejects with a ConnectionError.
 */
export function promisify<Req, Res>(
    client: Record<string, Function>,
    method: string,
    request: Req,
): Promise<Res> {
    return new Promise<Res>((resolve, reject) => {
        const fn = client[method];
        if (typeof fn !== "function") {
            reject(new ConnectionError(`Method "${method}" not found on client`));
            return;
        }
        fn.call(client, request, (error: ServiceError | null, response: Res) => {
            if (error) {
                reject(new ConnectionError(`gRPC call ${method} failed: ${error.message}`));
            } else {
                resolve(response);
            }
        });
    });
}

/**
 * Converts a callback-based gRPC unary call to a Promise, passing metadata.
 *
 * @param client - The gRPC client instance.
 * @param method - The method name on the client to call.
 * @param request - The protobuf request message.
 * @param metadata - gRPC metadata to send with the call.
 * @returns A promise that resolves with the response or rejects with a ConnectionError.
 */
export function promisifyWithMetadata<Req, Res>(
    client: Record<string, Function>,
    method: string,
    request: Req,
    metadata: Metadata,
): Promise<Res> {
    return new Promise<Res>((resolve, reject) => {
        const fn = client[method];
        if (typeof fn !== "function") {
            reject(new ConnectionError(`Method "${method}" not found on client`));
            return;
        }
        fn.call(client, request, metadata, (error: ServiceError | null, response: Res) => {
            if (error) {
                reject(new ConnectionError(`gRPC call ${method} failed: ${error.message}`));
            } else {
                resolve(response);
            }
        });
    });
}
