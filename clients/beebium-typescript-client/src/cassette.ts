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

import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

import type {
    CassetteServiceClient,
    CassetteResponse as ProtoCassetteResponse,
    CreateTapeResponse as ProtoCreateTapeResponse,
    ListBlankTapesResponse as ProtoListBlankTapesResponse,
    DeckStatus as ProtoDeckStatus,
    InsertTapeResponse as ProtoInsertTapeResponse,
    TapeMetadata as ProtoTapeMetadata,
} from "./generated/cassette.js";
import {
    CassetteErrorKind as ProtoCassetteErrorKind,
    CreateTapeErrorKind as ProtoCreateTapeErrorKind,
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
    /** "UEF" or "CSW". */
    format: string;
    title: string;
    origin: string;
    targetMachine: string;
    durationSeconds: number;
    /** Whether {@link Cassette.record} can record on it: a UEF can, a CSW cannot. */
    recordable: boolean;
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

    /** Insert a UEF or CSW tape image the server can reach; the deck is left stopped. */
    async insert(pathOrUrl: string): Promise<TapeMetadata> {
        const response = await this.call<{ url: string }, ProtoInsertTapeResponse>(
            "insertTape", { url: toTapeUrl(pathOrUrl) });
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
            throw new CreateTapeError(`Creating a blank tape failed: ${response.error}`,
                toCreateTapeErrorKind(response.kind));
        }
        return response.path;
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
