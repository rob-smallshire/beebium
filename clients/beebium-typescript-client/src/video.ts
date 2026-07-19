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
    TeletextScreen as ProtoTeletextScreen,
    TeletextScreenCell,
} from "./generated/video.js";
import { TeletextTextLayout } from "./generated/video.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable, BackgroundStreamHandle } from "./stream-utils.js";
import { TimeoutError } from "./exceptions.js";

export interface VideoConfig {
    width: number;
    height: number;
    framerateHz: number;
}

/** A MODE 7 display is always this size. */
export const TELETEXT_ROWS = 25;
export const TELETEXT_COLUMNS = 40;

/** A MODE 7 screen, or a region of one, read as characters. */
export interface TeletextScreen {
    /**
     * False when the display is not MODE 7, in which case the cells describe
     * whatever was last shown in MODE 7 rather than anything current.
     */
    active: boolean;

    rows: number;
    columns: number;

    /**
     * The region as text, converted server-side so every client agrees on what
     * graphics, control codes, concealed cells and double-height rows copy as.
     * Lines are joined with LF.
     */
    text: string;

    frameNumber: number;

    /**
     * Row-major, `rows * columns` entries, each with the attributes in effect
     * at that cell.
     */
    cells: TeletextScreenCell[];

    /** The cell at a position within the returned region. */
    cell(row: number, column: number): TeletextScreenCell;
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

function toTeletextScreen(proto: ProtoTeletextScreen): TeletextScreen {
    const cells = proto.cells;
    const columns = proto.columns;
    return {
        active: proto.active,
        rows: proto.rows,
        columns,
        text: proto.text,
        frameNumber: Number(proto.frameNumber),
        cells,
        cell(row: number, column: number): TeletextScreenCell {
            const cell = cells[row * columns + column];
            if (cell === undefined) {
                throw new RangeError(
                    `No cell at row ${row}, column ${column} in a ` +
                    `${proto.rows}x${columns} region`,
                );
            }
            return cell;
        },
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
     * Read the MODE 7 screen as characters rather than pixels.
     *
     * Prefer this to reading screen memory. The cells are captured after the
     * SAA5050 has resolved the control codes, so there is no hardware-scroll
     * offset to undo and no attribute state to re-derive -- the two failings
     * of the screen-memory scraper in `screen.ts`, which returns a rotated
     * grid once the display has scrolled.
     *
     * Only MODE 7 has characters to read. In a bitmap mode the returned screen
     * has `active` false and describes whatever was last shown in MODE 7, so
     * callers must check it.
     *
     * @param options.row First row of the region to read.
     * @param options.column First column of the region to read.
     * @param options.rows Number of rows; the rest of the screen when omitted.
     * @param options.columns Number of columns; the rest when omitted.
     * @param options.flowed Join a row that filled the region's width to the
     *     next without a line break, rejoining a line that wrapped. By default
     *     each row is its own line, preserving the shape of the selection.
     */
    async teletextScreen(options?: {
        row?: number;
        column?: number;
        rows?: number;
        columns?: number;
        flowed?: boolean;
    }): Promise<TeletextScreen> {
        const wantsRegion =
            options?.row !== undefined
            || options?.column !== undefined
            || options?.rows !== undefined
            || options?.columns !== undefined;

        const request: Record<string, unknown> = {
            layout: options?.flowed
                ? TeletextTextLayout.TELETEXT_LAYOUT_FLOWED
                : TeletextTextLayout.TELETEXT_LAYOUT_ROWS,
        };
        if (wantsRegion) {
            request["region"] = {
                row: options?.row ?? 0,
                column: options?.column ?? 0,
                rows: options?.rows ?? TELETEXT_ROWS,
                columns: options?.columns ?? TELETEXT_COLUMNS,
            };
        }

        const response = await promisify<Record<string, unknown>, ProtoTeletextScreen>(
            this.stub as unknown as Record<string, Function>,
            "getTeletextScreen",
            request,
        );
        return toTeletextScreen(response);
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
