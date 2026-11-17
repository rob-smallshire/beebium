// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

/**
 * The cassette deck behind the Serial ULA's cassette interface.
 *
 * The deck is the user's transport: inserting a tape leaves it stopped at the
 * start, and play is a separate request. The tape moves only while the deck
 * plays and the machine's motor relay is closed, which the filing system does
 * when it wants the tape.
 */

import { open } from "node:fs/promises";
import { basename, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import type {
    CassetteServiceClient,
    CassetteResponse as ProtoCassetteResponse,
    CreateTapeResponse as ProtoCreateTapeResponse,
    ListBlankTapesResponse as ProtoListBlankTapesResponse,
    AudioInputChunk as ProtoAudioInputChunk,
    StreamAudioSummary as ProtoStreamAudioSummary,
    DeckStatus as ProtoDeckStatus,
    InsertTapeResponse as ProtoInsertTapeResponse,
    TapeMetadata as ProtoTapeMetadata,
    TapeSignalMap as ProtoTapeSignalMap,
} from "./generated/cassette.js";
import {
    CassetteErrorKind as ProtoCassetteErrorKind,
    CreateTapeErrorKind as ProtoCreateTapeErrorKind,
    AudioSampleFormat as ProtoAudioSampleFormat,
    TapeTransport as ProtoTapeTransport,
} from "./generated/cassette.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";
import { CassetteError, CreateTapeError } from "./exceptions.js";

/** What the deck is doing: stopped, playing, recording, or winding. */
export enum TapeTransport {
    STOPPED = "stopped",
    PLAYING = "playing",
    RECORDING = "recording",
    REWINDING = "rewinding",
    FAST_FORWARDING = "fast_forwarding",
}

/**
 * Why a cassette operation failed. Carried on {@link CassetteError.kind} with
 * the server's sentence.
 */
export enum CassetteErrorKind {
    UNSPECIFIED = "unspecified",
    NO_CASSETTE_INTERFACE = "no_cassette_interface",
    CANNOT_OPEN = "cannot_open",
    UNREADABLE = "unreadable",
    UNSUPPORTED_FORMAT = "unsupported_format",
    INVALID_IMAGE = "invalid_image",
    DECK_OCCUPIED = "deck_occupied",
    DECK_EMPTY = "deck_empty",
    INVALID_POSITION = "invalid_position",
    READ_ONLY = "read_only",
    LIVE_SOURCE = "live_source",
    INVALID_FORMAT = "invalid_format",
}

/** How a live stream went, when it ended. */
export interface StreamAudioSummary {
    samplesReceived: number;
    /** Overruns: the ring was full. */
    samplesDropped: number;
    /** Underruns: the ring was empty. */
    silenceSamplesInserted: number;
}

/** A live stream's format. */
export interface AudioStreamFormat {
    sampleRate: number;
    sampleFormat?: "s16" | "f32";
    channels?: 1 | 2;
    channel?: "left" | "right";
    deviceName?: string;
}

/** Why a blank tape could not be created; on {@link CreateTapeError.kind}. */
export enum CreateTapeErrorKind {
    UNSPECIFIED = "unspecified",
    UNKNOWN_LABEL = "unknown_label",
    DESTINATION_EXISTS = "destination_exists",
    WRITE_FAILED = "write_failed",
    WRONG_EXTENSION = "wrong_extension",
}

/** A blank tape template the server ships, from which {@link Cassette.createTape} makes a tape. */
export interface BlankTape {
    /** The file name without its extension, e.g. "Blank C15 (15 minutes)". */
    label: string;
    /** e.g. "Blank C15 (15 minutes).uef". */
    filename: string;
    /** "tape". */
    kind: string;
    sizeBytes: number;
    durationSeconds: number;
}

/** A tape in the deck. */
export interface TapeMetadata {
    name: string;
    url: string;
    /** "UEF", "CSW" or "WAV". */
    format: string;
    title: string;
    origin: string;
    targetMachine: string;
    durationSeconds: number;
    /** Whether {@link Cassette.record} can record on it: a UEF whose file the server can write. */
    recordable: boolean;
    /** When not recordable, why, as the sentence {@link Cassette.record} would reject with. */
    recordRefusal: string;
    /** A WAV's rate in Hz; 0 for UEF and CSW. */
    sampleRate: number;
    /** The WAV channel played, "left" or "right"; empty for UEF and CSW. */
    channel: string;
}

/** The deck as the server last published it. */
export interface DeckStatus {
    hasCassetteInterface: boolean;
    /** Undefined when the deck is empty. */
    tape?: TapeMetadata;
    transport: TapeTransport;
    positionSeconds: number;
    /** The machine's motor relay, whether or not a tape is in. */
    motorOn: boolean;
    /** "ferranti" or "vlsi". */
    ulaPart: string;
    /** A live tape's ring, 0 to 1. */
    liveRingFill: number;
    /** Samples a live tape dropped because its ring was full. */
    liveOverruns: number;
    /** Samples of silence a live tape made up because its ring was empty. */
    liveUnderruns: number;
    /** Whether the tape's signal map is ready (see Cassette.signalMap). */
    signalMapReady: boolean;
    /** How many signal maps the server has published; fetch again when it changes. */
    signalMapGeneration: number;
}

/**
 * Where the tape carries signal: one level a second, 0 (silent throughout) to
 * 255 (signal throughout), the fraction of that second within a run of edges,
 * carrier included.
 */
export interface SignalMap {
    /** False while the map is being computed, and for a live tape. */
    ready: boolean;
    /** The number of levels, the tape's duration rounded up. */
    seconds: number;
    /** One a second; empty until ready. */
    levels: Uint8Array;
}

function toErrorKind(proto: ProtoCassetteErrorKind): CassetteErrorKind {
    switch (proto) {
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_NO_CASSETTE_INTERFACE:
            return CassetteErrorKind.NO_CASSETTE_INTERFACE;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_CANNOT_OPEN:
            return CassetteErrorKind.CANNOT_OPEN;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_UNREADABLE:
            return CassetteErrorKind.UNREADABLE;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT:
            return CassetteErrorKind.UNSUPPORTED_FORMAT;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_INVALID_IMAGE:
            return CassetteErrorKind.INVALID_IMAGE;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_DECK_OCCUPIED:
            return CassetteErrorKind.DECK_OCCUPIED;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_DECK_EMPTY:
            return CassetteErrorKind.DECK_EMPTY;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_INVALID_POSITION:
            return CassetteErrorKind.INVALID_POSITION;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_READ_ONLY:
            return CassetteErrorKind.READ_ONLY;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_LIVE_SOURCE:
            return CassetteErrorKind.LIVE_SOURCE;
        case ProtoCassetteErrorKind.CASSETTE_ERROR_KIND_INVALID_FORMAT:
            return CassetteErrorKind.INVALID_FORMAT;
        default:
            return CassetteErrorKind.UNSPECIFIED;
    }
}

function toCreateTapeErrorKind(proto: ProtoCreateTapeErrorKind): CreateTapeErrorKind {
    switch (proto) {
        case ProtoCreateTapeErrorKind.CREATE_TAPE_ERROR_KIND_UNKNOWN_LABEL:
            return CreateTapeErrorKind.UNKNOWN_LABEL;
        case ProtoCreateTapeErrorKind.CREATE_TAPE_ERROR_KIND_DESTINATION_EXISTS:
            return CreateTapeErrorKind.DESTINATION_EXISTS;
        case ProtoCreateTapeErrorKind.CREATE_TAPE_ERROR_KIND_WRITE_FAILED:
            return CreateTapeErrorKind.WRITE_FAILED;
        case ProtoCreateTapeErrorKind.CREATE_TAPE_ERROR_KIND_WRONG_EXTENSION:
            return CreateTapeErrorKind.WRONG_EXTENSION;
        default:
            return CreateTapeErrorKind.UNSPECIFIED;
    }
}

function toTransport(proto: ProtoTapeTransport): TapeTransport {
    switch (proto) {
        case ProtoTapeTransport.TAPE_TRANSPORT_PLAYING:
            return TapeTransport.PLAYING;
        case ProtoTapeTransport.TAPE_TRANSPORT_RECORDING:
            return TapeTransport.RECORDING;
        case ProtoTapeTransport.TAPE_TRANSPORT_REWINDING:
            return TapeTransport.REWINDING;
        case ProtoTapeTransport.TAPE_TRANSPORT_FAST_FORWARDING:
            return TapeTransport.FAST_FORWARDING;
        default:
            return TapeTransport.STOPPED;
    }
}

function toTapeMetadata(proto: ProtoTapeMetadata): TapeMetadata {
    return {
        name: proto.name,
        url: proto.url,
        format: proto.format,
        title: proto.title,
        origin: proto.origin,
        targetMachine: proto.targetMachine,
        durationSeconds: proto.durationSeconds,
        recordable: proto.recordable,
        sampleRate: proto.sampleRate,
        channel: proto.channel,
        recordRefusal: proto.recordRefusal,
    };
}

function toDeckStatus(proto: ProtoDeckStatus): DeckStatus {
    return {
        hasCassetteInterface: proto.hasCassetteInterface,
        tape: proto.tapeInserted && proto.tape ? toTapeMetadata(proto.tape) : undefined,
        transport: toTransport(proto.transport),
        positionSeconds: proto.positionSeconds,
        motorOn: proto.motorOn,
        ulaPart: proto.ulaPart,
        liveRingFill: proto.liveRingFill,
        liveOverruns: Number(proto.liveOverruns),
        liveUnderruns: Number(proto.liveUnderruns),
        signalMapReady: proto.signalMapReady,
        signalMapGeneration: proto.signalMapGeneration,
    };
}

/** A path as a file:// URL; anything with "://" as it is. */
function toTapeUrl(pathOrUrl: string): string {
    return pathOrUrl.includes("://") ? pathOrUrl : pathToFileURL(resolve(pathOrUrl)).href;
}

function check(response: { success: boolean; error: string; kind: ProtoCassetteErrorKind }): void {
    if (!response.success) {
        throw new CassetteError(response.error, toErrorKind(response.kind));
    }
}

/**
 * The cassette deck: insert, eject, play, stop, seek, and its status.
 *
 * Every unsuccessful request throws {@link CassetteError}, whose message is the
 * server's sentence and whose kind is a {@link CassetteErrorKind}.
 */
export class Cassette {
    readonly stub: CassetteServiceClient;

    constructor(stub: CassetteServiceClient) {
        this.stub = stub;
    }

    private call<Req, Res>(method: string, request: Req): Promise<Res> {
        return promisify<Req, Res>(this.stub as unknown as Record<string, Function>, method, request);
    }

    /**
     * Insert a UEF, CSW or WAV tape image the server can reach; the deck is left
     * stopped. A WAV plays through a model of the machine's cassette input;
     * `channel` chooses a stereo WAV's ("left" or "right"), by default the louder.
     */
    async insert(pathOrUrl: string, channel?: "left" | "right"): Promise<TapeMetadata> {
        const response = await this.call<{ url: string; channel?: string }, ProtoInsertTapeResponse>(
            "insertTape", channel === undefined ? { url: toTapeUrl(pathOrUrl) } : { url: toTapeUrl(pathOrUrl), channel });
        check(response);
        return toTapeMetadata(response.tape!);
    }

    /** Stop and remove the tape. */
    async eject(): Promise<void> {
        check(await this.call<object, ProtoCassetteResponse>("ejectTape", {}));
    }

    /** Press play. The tape moves while the machine's motor relay is closed. */
    async play(): Promise<void> {
        check(await this.call<object, ProtoCassetteResponse>("play", {}));
    }

    /** Press stop. */
    async stop(): Promise<void> {
        check(await this.call<object, ProtoCassetteResponse>("stop", {}));
    }

    /** Move the tape to `seconds` from its start; beyond the end lands at the end. */
    async seek(seconds: number): Promise<void> {
        check(await this.call<{ positionSeconds: number }, ProtoCassetteResponse>(
            "seek", { positionSeconds: seconds }));
    }

    /**
     * Wind backwards at 30 times real speed, whether or not the motor relay is
     * closed, until stop, play, a seek, eject or the start of the tape, where
     * the deck stops. `seek(0)` returns to the start at once.
     */
    async rewind(): Promise<void> {
        check(await this.call<object, ProtoCassetteResponse>("rewind", {}));
    }

    /**
     * Wind forwards at 30 times real speed, whether or not the motor relay is
     * closed, until stop, play, a seek, eject or the end of the tape, where the
     * deck stops.
     */
    async fastForward(): Promise<void> {
        check(await this.call<object, ProtoCassetteResponse>("fastForward", {}));
    }

    /**
     * Press record. From here, while the motor relay is closed, the tape moves
     * and what passes under the head is replaced by what the machine sends, or
     * by silence. Stop, play, a seek, a wind, eject and the end of the tape end
     * it, and the tape is then written back to its file. Throws CassetteError
     * with kind READ_ONLY for a tape Beebium only reads (a CSW).
     */
    async record(): Promise<void> {
        check(await this.call<object, ProtoCassetteResponse>("record", {}));
    }

    /** The blank tape templates the server ships, in file name order. */
    async listBlankTapes(): Promise<BlankTape[]> {
        const response = await this.call<object, ProtoListBlankTapesResponse>("listBlankTapes", {});
        return response.tapes.map((tape) => ({
            label: tape.label,
            filename: tape.filename,
            kind: tape.kind,
            sizeBytes: tape.sizeBytes,
            durationSeconds: tape.durationSeconds,
        }));
    }

    /**
     * Create a blank tape: copy the template with this label to a path on the
     * server's host, and return the new tape's absolute path. A destination
     * without an extension gets ".uef"; one with another is refused. Throws
     * CreateTapeError with its kind.
     */
    async createTape(label: string, destinationFilepath: string, overwrite = false): Promise<string> {
        const response = await this.call<
            { label: string; destinationPath: string; overwrite: boolean },
            ProtoCreateTapeResponse
        >("createTape", { label, destinationPath: destinationFilepath, overwrite });
        if (!response.success) {
            throw new CreateTapeError(response.error,
                toCreateTapeErrorKind(response.kind));
        }
        return response.path;
    }

    /**
     * Play `chunks` (little-endian samples in the stated format) as the live
     * tape, as a client capturing a deck's line-out would. The deck gets the
     * live tape, stopped, once the stream begins; call {@link play} to hear
     * it; the relay does not gate it. Resolves with the summary when the
     * chunks run out and the tape has played out what its ring holds, or the
     * tape is ejected. The server does not resample: send at real time (see
     * {@link streamWav}). Rejects with CassetteError (DECK_OCCUPIED,
     * INVALID_FORMAT) if the stream is refused.
     */
    streamAudio(chunks: AsyncIterable<Uint8Array> | Iterable<Uint8Array>,
                format: AudioStreamFormat): Promise<StreamAudioSummary> {
        return new Promise((resolvePromise, reject) => {
            const call = this.stub.streamAudio((error, summary: ProtoStreamAudioSummary) => {
                if (error) {
                    reject(error);
                } else if (summary.error) {
                    reject(new CassetteError(summary.error, toErrorKind(summary.kind)));
                } else {
                    resolvePromise({
                        samplesReceived: Number(summary.samplesReceived),
                        samplesDropped: Number(summary.samplesDropped),
                        silenceSamplesInserted: Number(summary.silenceSamplesInserted),
                    });
                }
            });
            const first: ProtoAudioInputChunk = {
                format: {
                    sampleRate: format.sampleRate,
                    sampleFormat: format.sampleFormat === "s16"
                        ? ProtoAudioSampleFormat.AUDIO_SAMPLE_FORMAT_S16
                        : ProtoAudioSampleFormat.AUDIO_SAMPLE_FORMAT_F32,
                    channels: format.channels ?? 1,
                    channel: format.channel ?? "",
                    deviceName: format.deviceName ?? "",
                },
            };
            call.write(first);
            (async () => {
                try {
                    for await (const chunk of chunks) {
                        call.write({ samples: Buffer.from(chunk.buffer, chunk.byteOffset, chunk.byteLength) });
                    }
                } finally {
                    call.end();
                }
            })().catch(reject);
        });
    }

    /**
     * Play a 16-bit PCM WAV as the live tape, read in blocks and, with
     * `realtime` (the default), paced to wall-clock time as a deck's
     * line-out arrives. Call {@link play} once the deck shows the live tape.
     */
    async streamWav(wavFilepath: string, options?: { realtime?: boolean; channel?: "left" | "right" }):
            Promise<StreamAudioSummary> {
        const file = await open(wavFilepath, "r");
        const header = Buffer.alloc(12);
        await file.read(header, 0, 12, 0);
        let offset = 12;
        let rate = 0, channels = 1, bits = 0, dataOffset = 0, dataLength = 0;
        const chunkHeader = Buffer.alloc(8);
        for (;;) {
            const { bytesRead } = await file.read(chunkHeader, 0, 8, offset);
            if (bytesRead < 8) break;
            const id = chunkHeader.toString("ascii", 0, 4);
            const length = chunkHeader.readUInt32LE(4);
            if (id === "fmt ") {
                const fmt = Buffer.alloc(16);
                await file.read(fmt, 0, 16, offset + 8);
                channels = fmt.readUInt16LE(2);
                rate = fmt.readUInt32LE(4);
                bits = fmt.readUInt16LE(14);
            } else if (id === "data") {
                dataOffset = offset + 8;
                dataLength = length;
                break;
            }
            offset += 8 + length + (length & 1);
        }
        if (header.toString("ascii", 0, 4) !== "RIFF" || bits !== 16 || dataOffset === 0) {
            await file.close();
            throw new Error(`streamWav plays 16-bit PCM WAVs; ${wavFilepath} is not one`);
        }
        const frameBytes = 2 * channels;
        const blockBytes = Math.max(1, Math.floor(rate / 100)) * frameBytes;
        const realtime = options?.realtime ?? true;
        async function* blocks(): AsyncIterable<Uint8Array> {
            const start = Date.now();
            let sent = 0;
            try {
                for (let at = 0; at < dataLength; at += blockBytes) {
                    const length = Math.min(blockBytes, dataLength - at);
                    const block = Buffer.alloc(length);
                    await file.read(block, 0, length, dataOffset + at);
                    yield block;
                    sent += length / frameBytes;
                    if (realtime) {
                        const delay = start + (sent / rate) * 1000 - Date.now();
                        if (delay > 0) await new Promise((r) => setTimeout(r, delay));
                    }
                }
            } finally {
                await file.close();
            }
        }
        return this.streamAudio(blocks(), {
            sampleRate: rate, sampleFormat: "s16", channels: channels === 2 ? 2 : 1,
            channel: channels === 2 ? options?.channel : undefined, deviceName: basename(wavFilepath),
        });
    }

    /**
     * Where the tape in the deck carries signal, a level a second. The server
     * computes it off the emulation thread when a tape goes in and again when
     * a recording ends; until then, and for a live tape, `ready` is false.
     * DeckStatus.signalMapGeneration changes each time a new map is ready.
     * Throws CassetteError (DECK_EMPTY with no tape, or NO_CASSETTE_INTERFACE).
     */
    async signalMap(): Promise<SignalMap> {
        const response = await this.call<object, ProtoTapeSignalMap>("getTapeSignalMap", {});
        check(response);
        return { ready: response.ready, seconds: response.seconds, levels: new Uint8Array(response.levels) };
    }

    /** The deck's state, read without stopping the machine. */
    async status(): Promise<DeckStatus> {
        return toDeckStatus(await this.call<object, ProtoDeckStatus>("getDeckStatus", {}));
    }

    /**
     * The deck's status now, then on every change of presence, transport, relay
     * or tape, and while it plays its position at most every `minIntervalMs` of
     * emulated time.
     */
    async *subscribe(minIntervalMs = 100): AsyncIterable<DeckStatus> {
        const stream = this.stub.subscribeDeckStatus({ minIntervalMs });
        try {
            for await (const proto of toAsyncIterable<ProtoDeckStatus>(stream)) {
                yield toDeckStatus(proto);
            }
        } finally {
            stream.cancel();
        }
    }
}
