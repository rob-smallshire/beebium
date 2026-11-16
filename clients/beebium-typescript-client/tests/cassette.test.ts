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

import { describe, it, expect, vi } from "vitest";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { Cassette, CassetteErrorKind, TapeTransport } from "../src/cassette.js";
import { CassetteError } from "../src/exceptions.js";

// Proto enum values matching the generated code
const KIND_DECK_EMPTY = 7;
const KIND_UNSUPPORTED_FORMAT = 4;
const TRANSPORT_PLAYING = 1;

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                callback(null, handler(request));
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

const TAPE = {
    name: "game.uef",
    url: "file:///tapes/game.uef",
    format: "UEF",
    title: "",
    origin: "tests",
    targetMachine: "",
    durationSeconds: 12.5,
};

describe("Cassette", () => {
    it("inserts a path as a file URL and returns the tape's metadata", async () => {
        const stub = createMockStub({
            insertTape: () => ({ success: true, error: "", kind: 0, tape: TAPE }),
        });
        const cassette = new Cassette(stub as any);
        const tape = await cassette.insert("tapes/game.uef");
        expect(stub.insertTape.mock.calls[0][0].url).toBe(pathToFileURL(resolve("tapes/game.uef")).href);
        expect(tape.name).toBe("game.uef");
        expect(tape.durationSeconds).toBe(12.5);
    });

    it("passes a URL through as it is", async () => {
        const stub = createMockStub({
            insertTape: () => ({ success: true, error: "", kind: 0, tape: TAPE }),
        });
        await new Cassette(stub as any).insert("file:///tapes/game.uef");
        expect(stub.insertTape.mock.calls[0][0].url).toBe("file:///tapes/game.uef");
    });

    it("throws CassetteError with the server's sentence and kind", async () => {
        const stub = createMockStub({
            play: () => ({ success: false, error: "There is no tape in the deck.", kind: KIND_DECK_EMPTY }),
            insertTape: () => ({
                success: false,
                error: "The file notes.txt is not a UEF or CSW tape image.",
                kind: KIND_UNSUPPORTED_FORMAT,
            }),
        });
        const cassette = new Cassette(stub as any);
        await expect(cassette.play()).rejects.toMatchObject({
            message: "There is no tape in the deck.",
            kind: CassetteErrorKind.DECK_EMPTY,
        });
        await expect(cassette.insert("notes.txt")).rejects.toBeInstanceOf(CassetteError);
    });

    it("maps the deck status", async () => {
        const stub = createMockStub({
            getDeckStatus: () => ({
                hasCassetteInterface: true,
                tapeInserted: true,
                tape: TAPE,
                transport: TRANSPORT_PLAYING,
                positionSeconds: 3.25,
                motorOn: true,
                ulaPart: "ferranti",
            }),
        });
        const status = await new Cassette(stub as any).status();
        expect(status.tape?.name).toBe("game.uef");
        expect(status.transport).toBe(TapeTransport.PLAYING);
        expect(status.positionSeconds).toBe(3.25);
        expect(status.motorOn).toBe(true);
    });

    it("reports an empty deck as no tape", async () => {
        const stub = createMockStub({
            getDeckStatus: () => ({
                hasCassetteInterface: true,
                tapeInserted: false,
                transport: 0,
                positionSeconds: 0,
                motorOn: false,
                ulaPart: "ferranti",
            }),
        });
        const status = await new Cassette(stub as any).status();
        expect(status.tape).toBeUndefined();
        expect(status.transport).toBe(TapeTransport.STOPPED);
    });

    it("seeks, and rewinds to the start", async () => {
        const stub = createMockStub({ seek: () => ({ success: true, error: "", kind: 0 }) });
        const cassette = new Cassette(stub as any);
        await cassette.seek(4.5);
        await cassette.rewind();
        expect(stub.seek.mock.calls.map((c: any[]) => c[0].positionSeconds)).toEqual([4.5, 0]);
    });
});
