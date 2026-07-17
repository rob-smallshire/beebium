/**
 * Video frame access interface for the Beebium TypeScript client.
 *
 * Provides video configuration queries, single-frame capture, and
 * continuous frame streaming.
 */

import type {
    VideoServiceClient,
    Frame as ProtoFrame,
    VideoConfig as ProtoVideoConfig,
} from "./generated/video.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable, BackgroundStreamHandle } from "./stream-utils.js";
import { TimeoutError } from "./exceptions.js";

export interface VideoConfig {
    width: number;
    height: number;
    framerateHz: number;
}

export interface Frame {
    frameNumber: number;
    cycleCount: number;
    width: number;
    height: number;
    pixels: Buffer;
}

function toVideoConfig(proto: ProtoVideoConfig): VideoConfig {
    return {
        width: proto.width,
        height: proto.height,
        framerateHz: proto.framerateHz,
    };
}

function toFrame(proto: ProtoFrame): Frame {
    return {
        frameNumber: proto.frameNumber,
        cycleCount: proto.cycleCount,
        width: proto.width,
        height: proto.height,
        pixels: proto.pixels,
    };
}

/**
 * Video frame access interface.
 *
 * Provides video configuration queries, single-frame capture, and
 * continuous frame streaming.
 */
export class Video {
    private readonly stub: VideoServiceClient;

    constructor(stub: VideoServiceClient) {
        this.stub = stub;
    }

    /** Get the current video configuration (resolution, framerate). */
    async getConfig(): Promise<VideoConfig> {
        const response = await promisify<{}, ProtoVideoConfig>(
            this.stub as unknown as Record<string, Function>,
            "getConfig",
            {},
        );
        return toVideoConfig(response);
    }

    /**
     * Capture a single frame.
     *
     * Starts a frame stream, waits for the first frame, then cancels.
     * Throws TimeoutError if no frame arrives within the timeout.
     */
    async captureFrame(timeoutMs: number = 1000): Promise<Frame> {
        const stream = this.stub.subscribeFrames({});
        try {
            return await new Promise<Frame>((resolve, reject) => {
                const timer = setTimeout(() => {
                    stream.cancel();
                    reject(new TimeoutError(`No frame received within ${timeoutMs}ms`));
                }, timeoutMs);

                stream.on("data", (message: ProtoFrame) => {
                    clearTimeout(timer);
                    stream.cancel();
                    resolve(toFrame(message));
                });

                stream.on("error", (err: Error) => {
                    clearTimeout(timer);
                    // CANCELLED is expected after we got a frame and called cancel
                    if (!err.message.includes("CANCELLED")) {
                        reject(err);
                    }
                });
            });
        } catch (err) {
            stream.cancel();
            throw err;
        }
    }

    /** Stream frames as an async iterable. Yields each frame as it arrives. */
    async *streamFrames(): AsyncIterable<Frame> {
        const stream = this.stub.subscribeFrames({});
        for await (const proto of toAsyncIterable(stream)) {
            yield toFrame(proto);
        }
    }

    /**
     * Start a background frame stream with a callback.
     *
     * Returns a handle that can be used to cancel the stream.
     */
    startBackgroundStream(callback: (frame: Frame) => void): BackgroundStreamHandle {
        const stream = this.stub.subscribeFrames({});
        return new BackgroundStreamHandle(
            stream,
            (message: unknown) => callback(toFrame(message as ProtoFrame)),
        );
    }
}
