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
    DeckStatus as ProtoDeckStatus,
    InsertTapeResponse as ProtoInsertTapeResponse,
    TapeMetadata as ProtoTapeMetadata,
} from "./generated/cassette.js";
import {
    CassetteErrorKind as ProtoCassetteErrorKind,
    TapeTransport as ProtoTapeTransport,
} from "./generated/cassette.js";
import { promisify } from "./call-utils.js";
import { toAsyncIterable } from "./stream-utils.js";
import { CassetteError } from "./exceptions.js";

/** Whether the deck is playing. */
export enum TapeTransport {
    STOPPED = "stopped",
    PLAYING = "playing",
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
        default:
            return CassetteErrorKind.UNSPECIFIED;
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
    };
}

function toDeckStatus(proto: ProtoDeckStatus): DeckStatus {
    return {
        hasCassetteInterface: proto.hasCassetteInterface,
        tape: proto.tapeInserted && proto.tape ? toTapeMetadata(proto.tape) : undefined,
        transport: proto.transport === ProtoTapeTransport.TAPE_TRANSPORT_PLAYING
            ? TapeTransport.PLAYING : TapeTransport.STOPPED,
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

    /** Seek to the start. */
    async rewind(): Promise<void> {
        await this.seek(0);
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
