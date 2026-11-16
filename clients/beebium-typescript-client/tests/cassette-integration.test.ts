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
 * The cassette client against a real server: insert, the deck's keys, an
 * error's kind and the status stream.
 */

import { describe, it, expect } from "vitest";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Beebium } from "../src/client.js";
import { CassetteErrorKind, TapeTransport } from "../src/cassette.js";
import { withServer } from "./server-harness.js";

/** A UEF of `seconds` of carrier. */
function toneUef(seconds: number): Buffer {
    const header = Buffer.from("UEF File!\u0000\u000a\u0000", "binary");
    const chunk = Buffer.alloc(8);
    chunk.writeUInt16LE(0x0110, 0);
    chunk.writeUInt32LE(2, 2);
    chunk.writeUInt16LE(seconds * 2400, 6);
    return Buffer.concat([header, chunk]);
}

describe("cassette integration", () => {
    it("inserts a tape stopped, plays, seeks and ejects", async () => {
        await withServer(async (conn, server) => {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            const dirpath = mkdtempSync(join(tmpdir(), "beebium-cassette-"));
            const filepath = join(dirpath, "tone.uef");
            writeFileSync(filepath, toneUef(10));

            await expect(bbc.cassette.play()).rejects.toMatchObject({ kind: CassetteErrorKind.DECK_EMPTY });

            const iter = bbc.cassette.subscribe(50)[Symbol.asyncIterator]();
            const first = await iter.next();
            expect(first.value!.tape).toBeUndefined();

            const tape = await bbc.cassette.insert(filepath);
            expect(tape.name).toBe("tone.uef");
            expect(tape.format).toBe("UEF");
            for (;;) {
                const next = await iter.next();
                if (next.done || next.value.tape) break;
            }
            await iter.return?.();

            let status = await bbc.cassette.status();
            expect(status.transport).toBe(TapeTransport.STOPPED);
            await bbc.cassette.play();
            expect((await bbc.cassette.status()).transport).toBe(TapeTransport.PLAYING);
            await bbc.cassette.seek(4.5);
            status = await bbc.cassette.status();
            expect(status.positionSeconds).toBeCloseTo(4.5, 3);
            await bbc.cassette.fastForward();
            expect((await bbc.cassette.status()).transport).toBe(TapeTransport.FAST_FORWARDING);
            await bbc.cassette.stop();
            await bbc.cassette.eject();
            expect((await bbc.cassette.status()).tape).toBeUndefined();
        });
    });

    it("creates a blank tape and records on it", async () => {
        await withServer(async (conn, server) => {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            const dirpath = mkdtempSync(join(tmpdir(), "beebium-cassette-"));
            const tapes = await bbc.cassette.listBlankTapes();
            expect(tapes.map((t) => t.label)).toContain("Blank C15 (15 minutes)");
            const filepath = await bbc.cassette.createTape("Blank C15 (15 minutes)", join(dirpath, "side-a"));
            expect(filepath.endsWith("side-a.uef")).toBe(true);
            const tape = await bbc.cassette.insert(filepath);
            expect(tape.recordable).toBe(true);
            await bbc.cassette.record();
            expect((await bbc.cassette.status()).transport).toBe(TapeTransport.RECORDING);
            await bbc.cassette.stop();
            await bbc.cassette.eject();
        });
    });
});
