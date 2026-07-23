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
    ScreenText as ProtoScreenText,
    ScreenGeometry as ProtoScreenGeometry,
    ScreenHold as ProtoScreenHold,
} from "./generated/video.js";
import {
    TeletextTextLayout,
    ScreenTextLayout,
    ScreenTextSearch,
    ScreenTextCharacters,
} from "./generated/video.js";
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

/**
 * A rectangle in frame pixel coordinates.
 *
 * The origin is the top-left of the active area rather than of the bordered
 * display, matching the frames the video service streams.
 */
export interface PixelRegion {
    x: number;
    y: number;
    width: number;
    height: number;
}

/** One cell of a run: where it sits, and whether a glyph was recognised. */
export interface ScreenTextCell {
    bounds: PixelRegion;

    /**
     * True when a glyph was recognised here; false when the cell had ink the
     * font could not identify. An unmatched cell still copies as a space to
     * keep columns aligned, but a client can leave it out of a highlight rather
     * than dress a failed read up as a success. A genuine space is matched.
     */
    matched: boolean;
}

/** A contiguous piece of text and where it was found. */
export interface ScreenTextRun {
    text: string;

    /**
     * Where the run was found, so a client can highlight exactly what it
     * captured.
     */
    bounds: PixelRegion;

    /**
     * The character cell geometry the run was read with, so a selection can
     * snap to it. Zero when the run is not cell-aligned, as text written at
     * the graphics cursor is.
     */
    cellWidth: number;
    cellHeight: number;

    /**
     * The run's cells in reading order, from the glyph-recognising strategy.
     * Empty from the teletext strategy, whose cells are exact characters and
     * all matched: a client then highlights the whole run's bounds.
     */
    cells: ScreenTextCell[];
}

/** Text read from the display, whatever mode is producing it. */
export interface ScreenText {
    /**
     * True when at least one band of the requested region had a strategy that
     * could read it.
     *
     * Distinct from readable-but-empty: a graphics screen that was read and
     * found to contain no text is supported with no runs, whereas a display
     * this build has no strategy for is unsupported. Which displays fall in
     * the second group narrows as strategies are added, so a caller should
     * treat it as "not this time" rather than "not ever".
     */
    supported: boolean;

    /**
     * In reading order: bands top to bottom, and within a band by baseline
     * then x.
     */
    runs: ScreenTextRun[];

    /**
     * The runs joined by the requested layout, for a caller that wants a
     * string and not structure. Lines are joined with LF.
     */
    text: string;

    /**
     * Cells a strategy tried to read and could not identify at all. Zero for a
     * MODE 7 display, whose cells are exact character codes.
     */
    unreadableCells: number;

    /**
     * Cells a strategy read but could not pin to a single character, because
     * the font in use draws two characters identically. Also zero for MODE 7.
     */
    ambiguousCells: number;

    frameNumber: number;
}

/** The character grid for one band of scanlines, in frame pixels. */
export interface ScreenBandGeometry {
    /** First scanline, inclusive. */
    top: number;
    /** One past the last. */
    bottom: number;

    cellWidth: number;
    cellHeight: number;

    /**
     * Cell-to-cell step. Equal to the cell size except where a mode leaves
     * blank scanlines between rows, as MODE 3 and MODE 6 do: an eight-scanline
     * glyph on a ten-scanline pitch.
     */
    columnPitch: number;
    rowPitch: number;

    /** Where the grid starts within the band. */
    originX: number;
    originY: number;
}

/** The character grid the display currently implies, per band. */
export interface ScreenGeometry {
    bands: ScreenBandGeometry[];
    frameNumber: number;
}

/** Which search a glyph-recognising strategy should run. */
export type ScreenTextSearchMode = "anywhere" | "aligned";

/**
 * Which meaning a MODE 7 byte is read with: the byte at face value, or the
 * glyph the SAA5050 drew for it. They differ for eleven codes.
 */
export type ScreenTextCharactersMode = "codes" | "displayed";

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

function toScreenText(proto: ProtoScreenText): ScreenText {
    return {
        supported: proto.supported,
        runs: proto.runs.map((run) => ({
            text: run.text,
            bounds: {
                x: run.bounds?.x ?? 0,
                y: run.bounds?.y ?? 0,
                width: run.bounds?.width ?? 0,
                height: run.bounds?.height ?? 0,
            },
            cellWidth: run.cellWidth,
            cellHeight: run.cellHeight,
            cells: run.cells.map((cell) => ({
                bounds: {
                    x: cell.bounds?.x ?? 0,
                    y: cell.bounds?.y ?? 0,
                    width: cell.bounds?.width ?? 0,
                    height: cell.bounds?.height ?? 0,
                },
                matched: cell.matched,
            })),
        })),
        text: proto.text,
        unreadableCells: proto.unreadableCells,
        ambiguousCells: proto.ambiguousCells,
        frameNumber: Number(proto.frameNumber),
    };
}

/**
 * A screen held on the server for the life of a selection.
 *
 * A reading depends on the pixels, the band geometry, the teletext grid and the
 * font in RAM, all of which move independently. Read at four different instants
 * they describe a screen that never existed, so holding captures them together
 * and later reads name the capture. The emulator keeps running: a hold is a
 * copy, not a pause.
 */
export interface ScreenHold {
    /** Names the hold in later `screenText` and `screenGeometry` calls. */
    holdId: number;

    /**
     * The grid the held screen implies, returned with the hold so it cannot
     * drift from the pixels it describes.
     */
    geometry: ScreenGeometry;

    /**
     * The held still, when `includeFrame` was asked for, so a caller can show
     * exactly the picture its reads are made against.
     */
    frame?: Frame;
}

function toScreenGeometry(proto: ProtoScreenGeometry): ScreenGeometry {
    return {
        bands: proto.bands.map((band) => ({
            top: band.top,
            bottom: band.bottom,
            cellWidth: band.cellWidth,
            cellHeight: band.cellHeight,
            columnPitch: band.columnPitch,
            rowPitch: band.rowPitch,
            originX: band.originX,
            originY: band.originY,
        })),
        frameNumber: Number(proto.frameNumber),
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
     * Read text from the display, whatever mode is producing it.
     *
     * Prefer this to `teletextScreen()` and to the screen-memory scraper in
     * `screen.ts`. The caller selects in pixels -- the one coordinate system
     * every mode shares -- and the server picks a reading strategy per band of
     * scanlines, so a split screen is read a band at a time and the caller
     * never learns which mode produced what.
     *
     * A display this build has no strategy for comes back with `supported`
     * false and no runs, rather than something stale with a flag attached.
     *
     * @param options.region `[x, y, width, height]` in frame pixels; the whole
     *     display when omitted. Clipped to the display rather than rejected.
     * @param options.search `"anywhere"`, the default, reads all the text --
     *     on the grid and placed freely with VDU 5 -- and is a strict superset
     *     of `"aligned"`, which reads only the grid and is exact and cheaper,
     *     as a snapped drag wants. Pick one up front; there is no reason to ask
     *     for both. Independent of `region`. Honoured by strategies that
     *     recognise glyphs in pixels; a MODE 7 display is always its grid.
     * @param options.flowed Join a run that reached the right edge to the next
     *     without a line break, rejoining a line that wrapped. By default each
     *     grid row is its own line, preserving the shape of the selection.
     * @param options.characters Which meaning a MODE 7 byte carries.
     *     `"codes"`, the default, takes the byte at face value, so `[`, `]`
     *     and `^` come back as themselves and a copied BASIC listing keeps its
     *     assembler blocks and exponentiation. `"displayed"` reports the glyphs
     *     the SAA5050 actually drew for those eleven codes -- a left arrow, a
     *     right arrow, an up arrow and the rest -- for capturing a teletext
     *     screen as it looked. Only the caller knows which was meant. Ignored
     *     outside MODE 7, whose font is the MOS's and already ASCII.
     */
    async screenText(options?: {
        region?: PixelRegion;
        search?: ScreenTextSearchMode;
        flowed?: boolean;
        characters?: ScreenTextCharactersMode;
        holdId?: number;
    }): Promise<ScreenText> {
        const searches: Record<ScreenTextSearchMode, ScreenTextSearch> = {
            anywhere: ScreenTextSearch.SCREEN_TEXT_SEARCH_ANYWHERE,
            aligned: ScreenTextSearch.SCREEN_TEXT_SEARCH_ALIGNED,
        };
        const search = options?.search ?? "anywhere";
        if (!(search in searches)) {
            throw new RangeError(
                `Unknown search ${search}; expected one of ` +
                `${Object.keys(searches).join(", ")}`,
            );
        }

        const repertoires: Record<ScreenTextCharactersMode, ScreenTextCharacters> = {
            codes: ScreenTextCharacters.SCREEN_TEXT_CHARACTERS_CODES,
            displayed: ScreenTextCharacters.SCREEN_TEXT_CHARACTERS_DISPLAYED,
        };
        const characters = options?.characters ?? "codes";
        if (!(characters in repertoires)) {
            throw new RangeError(
                `Unknown characters ${characters}; expected one of ` +
                `${Object.keys(repertoires).join(", ")}`,
            );
        }

        const request: Record<string, unknown> = {
            search: searches[search],
            layout: options?.flowed
                ? ScreenTextLayout.SCREEN_TEXT_LAYOUT_FLOWED
                : ScreenTextLayout.SCREEN_TEXT_LAYOUT_ROWS,
            characters: repertoires[characters],
        };
        if (options?.region !== undefined) {
            request["region"] = options.region;
        }
        if (options?.holdId !== undefined) {
            request["holdId"] = options.holdId;
        }

        const response = await promisify<Record<string, unknown>, ProtoScreenText>(
            this.stub as unknown as Record<string, Function>,
            "getScreenText",
            request,
        );
        return toScreenText(response);
    }

    /**
     * Report the character grid the display currently implies, per band.
     *
     * Separate from `screenText()` because snapping a drag has to happen while
     * the drag is in progress, when there is nothing to send yet. One call on
     * mouse-down is ample.
     *
     * Every band reports a grid, including one no strategy can read text from:
     * where the cells are and what is in them are separate questions.
     */
    async screenGeometry(options?: { holdId?: number }): Promise<ScreenGeometry> {
        const request: Record<string, unknown> = {};
        if (options?.holdId !== undefined) {
            request["holdId"] = options.holdId;
        }
        const response = await promisify<Record<string, unknown>, ProtoScreenGeometry>(
            this.stub as unknown as Record<string, Function>,
            "getScreenGeometry",
            request,
        );
        return toScreenGeometry(response);
    }

    /**
     * Hold the screen as it stands, so reads describe one still.
     *
     * Everything a reading depends on is captured together, so a selection made
     * against a moving display reads the picture it was drawn on rather than
     * whatever has been drawn since. The emulator keeps running.
     *
     * Release the hold when finished; holds also expire on their own.
     */
    async holdScreen(options?: { includeFrame?: boolean }): Promise<ScreenHold> {
        const response = await promisify<Record<string, unknown>, ProtoScreenHold>(
            this.stub as unknown as Record<string, Function>,
            "holdScreen",
            { includeFrame: options?.includeFrame ?? false },
        );
        return {
            holdId: response.holdId,
            geometry: toScreenGeometry(response.geometry!),
            frame: response.frame ? toFrame(response.frame) : undefined,
        };
    }

    /** Let a held screen go. */
    async releaseScreen(holdId: number): Promise<void> {
        await promisify<Record<string, unknown>, unknown>(
            this.stub as unknown as Record<string, Function>,
            "releaseScreen",
            { holdId },
        );
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
