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
import { Cassette, CassetteErrorKind, CreateTapeErrorKind, TapeTransport } from "../src/cassette.js";
import { CassetteError, CreateTapeError } from "../src/exceptions.js";

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
    recordable: true,
    sampleRate: 0,
    channel: "",
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
                error: "The file notes.txt is not a UEF, CSW or WAV tape image.",
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
                signalMapReady: true,
                signalMapGeneration: 4,
            }),
        });
        const status = await new Cassette(stub as any).status();
        expect(status.signalMapReady).toBe(true);
        expect(status.signalMapGeneration).toBe(4);
        expect(status.tape?.name).toBe("game.uef");
        expect(status.transport).toBe(TapeTransport.PLAYING);
        expect(status.positionSeconds).toBe(3.25);
        expect(status.motorOn).toBe(true);
    });

    it("fetches the signal map, and throws DECK_EMPTY with no tape", async () => {
        let inserted = true;
        const stub = createMockStub({
            getTapeSignalMap: () =>
                inserted
                    ? { success: true, error: "", kind: 0, ready: true, seconds: 3, levels: Buffer.from([0, 128, 255]) }
                    : { success: false, error: "There is no tape in the deck.", kind: KIND_DECK_EMPTY,
                        ready: false, seconds: 0, levels: Buffer.alloc(0) },
        });
        const cassette = new Cassette(stub as any);
        const map = await cassette.signalMap();
        expect(map.ready).toBe(true);
        expect(map.seconds).toBe(3);
        expect(Array.from(map.levels)).toEqual([0, 128, 255]);
        inserted = false;
        await expect(cassette.signalMap()).rejects.toMatchObject({ kind: CassetteErrorKind.DECK_EMPTY });
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

    it("seeks", async () => {
        const stub = createMockStub({ seek: () => ({ success: true, error: "", kind: 0 }) });
        const cassette = new Cassette(stub as any);
        await cassette.seek(4.5);
        await cassette.seek(0);
        expect(stub.seek.mock.calls.map((c: any[]) => c[0].positionSeconds)).toEqual([4.5, 0]);
    });

    it("winds with Rewind and FastForward and maps the winding transports", async () => {
        const ok = () => ({ success: true, error: "", kind: 0 });
        const stub = createMockStub({
            rewind: ok,
            fastForward: ok,
            getDeckStatus: () => ({
                hasCassetteInterface: true,
                tapeInserted: true,
                tape: TAPE,
                transport: 4,
                positionSeconds: 1,
                motorOn: false,
                ulaPart: "ferranti",
            }),
        });
        const cassette = new Cassette(stub as any);
        await cassette.rewind();
        await cassette.fastForward();
        expect(stub.rewind).toHaveBeenCalledTimes(1);
        expect(stub.fastForward).toHaveBeenCalledTimes(1);
        expect((await cassette.status()).transport).toBe(TapeTransport.FAST_FORWARDING);
    });

    it("records, and refuses with READ_ONLY", async () => {
        const stub = createMockStub({
            record: () => ({ success: false, error: "The tape game.csw is a CSW image, and Beebium records only on UEF tapes.", kind: 9 }),
        });
        await expect(new Cassette(stub as any).record()).rejects.toMatchObject({
            kind: CassetteErrorKind.READ_ONLY,
        });
    });

    it("lists blank tapes and creates one, with its error kinds", async () => {
        const stub = createMockStub({
            listBlankTapes: () => ({
                tapes: [{ label: "C15", filename: "C15.uef", kind: "tape",
                          sizeBytes: 36, durationSeconds: 900 }],
            }),
            createTape: (req: any) => req.label === "C15"
                ? { success: true, error: "", kind: 0, path: "/tmp/side-a.uef" }
                : { success: false, error: "no blank tape named 'x'", kind: 1, path: "" },
        });
        const cassette = new Cassette(stub as any);
        const tapes = await cassette.listBlankTapes();
        expect(tapes[0].durationSeconds).toBe(900);
        expect(await cassette.createTape("C15", "/tmp/side-a")).toBe("/tmp/side-a.uef");
        await expect(cassette.createTape("x", "/tmp/y")).rejects.toBeInstanceOf(CreateTapeError);
        await expect(cassette.createTape("x", "/tmp/y")).rejects.toMatchObject({
            kind: CreateTapeErrorKind.UNKNOWN_LABEL,
        });
    });

    it("passes a WAV's channel and reports its rate", async () => {
        const stub = createMockStub({
            insertTape: () => ({
                success: true, error: "", kind: 0,
                tape: { ...TAPE, name: "take.wav", format: "WAV", recordable: false, sampleRate: 48000, channel: "right" },
            }),
        });
        const tape = await new Cassette(stub as any).insert("/tapes/take.wav", "right");
        expect(stub.insertTape.mock.calls[0][0].channel).toBe("right");
        expect(tape.sampleRate).toBe(48000);
        expect(tape.channel).toBe("right");
        await new Cassette(stub as any).insert("/tapes/take.wav");
        expect(stub.insertTape.mock.calls[1][0]).not.toHaveProperty("channel");
    });
});
