/**
 * Utilities for working with gRPC server-streaming calls.
 */

import type { ClientReadableStream } from "@grpc/grpc-js";
import { ConnectionError } from "./exceptions.js";

/**
 * Wraps a gRPC server-streaming call as an async iterable.
 *
 * The returned iterable yields each message from the stream. If the stream
 * encounters an error, it is thrown as a ConnectionError. The stream is
 * properly cleaned up when iteration ends (whether by completion, error,
 * or early break).
 */
export async function* toAsyncIterable<T>(
    stream: ClientReadableStream<T>,
): AsyncGenerator<T, void, undefined> {
    // Buffer incoming messages and errors so the consumer can pull at its own pace.
    const pending: Array<{ value: T } | { error: Error } | { done: true }> = [];
    let resolve: (() => void) | null = null;

    function notify(): void {
        if (resolve) {
            const r = resolve;
            resolve = null;
            r();
        }
    }

    stream.on("data", (message: T) => {
        pending.push({ value: message });
        notify();
    });

    stream.on("error", (err: Error) => {
        pending.push({ error: err });
        notify();
    });

    stream.on("end", () => {
        pending.push({ done: true });
        notify();
    });

    try {
        while (true) {
            if (pending.length === 0) {
                await new Promise<void>((r) => {
                    resolve = r;
                });
            }

            const item = pending.shift();
            if (!item) {
                continue;
            }

            if ("done" in item) {
                return;
            }

            if ("error" in item) {
                throw new ConnectionError(
                    `gRPC stream error: ${item.error.message}`,
                );
            }

            yield item.value;
        }
    } finally {
        stream.cancel();
    }
}

/**
 * A handle to a background stream consumer. Messages are delivered via
 * callbacks rather than pulled by the caller.
 */
export class BackgroundStreamHandle {
    private _cancelled = false;
    private _running = true;

    /**
     * @param stream - The gRPC readable stream to consume.
     * @param onMessage - Called for each message received.
     * @param onError - Called if the stream encounters an error. Optional.
     * @param onEnd - Called when the stream ends normally. Optional.
     */
    constructor(
        private readonly stream: ClientReadableStream<unknown>,
        onMessage: (message: unknown) => void,
        onError?: (error: Error) => void,
        onEnd?: () => void,
    ) {
        stream.on("data", (message: unknown) => {
            if (!this._cancelled) {
                onMessage(message);
            }
        });

        stream.on("error", (err: Error) => {
            this._running = false;
            if (!this._cancelled && onError) {
                onError(err);
            }
        });

        stream.on("end", () => {
            this._running = false;
            if (!this._cancelled && onEnd) {
                onEnd();
            }
        });
    }

    /** Whether the stream is still active (not ended, errored, or cancelled). */
    get isRunning(): boolean {
        return this._running && !this._cancelled;
    }

    /** Cancel the stream. No further callbacks will be invoked. */
    cancel(): void {
        if (!this._cancelled) {
            this._cancelled = true;
            this._running = false;
            this.stream.cancel();
        }
    }
}
