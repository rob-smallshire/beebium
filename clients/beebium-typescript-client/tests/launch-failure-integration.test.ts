// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version. Beebium is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. You should have received a copy of the GNU General Public License along with
// Beebium. If not, see <https://www.gnu.org/licenses/>.

/**
 * Beebium.launch stops the server it started when the launch fails (#74).
 *
 * A launch that fails after the server is running returns no Beebium, so the
 * caller has nothing to close; launch itself must stop the server. Each case
 * records the PID of every server launch starts, forces a failure after the
 * start, and asserts no recorded server is still alive.
 */

import { describe, it, expect, afterEach, vi } from "vitest";
import type { ChildProcess } from "node:child_process";
import { Beebium } from "../src/client.js";
import { Connection } from "../src/connection.js";
import { ProtocolMismatchError, TimeoutError } from "../src/exceptions.js";
import { ServerProcess } from "../src/server-process.js";

/** Whether a process with this PID exists. */
function isAlive(pid: number): boolean {
    try {
        process.kill(pid, 0);
        return true;
    } catch {
        return false;
    }
}

/** Record the PID of every server ServerProcess.start launches. */
function recordStartedPids(): number[] {
    const pids: number[] = [];
    const originalStart = ServerProcess.prototype.start;
    vi.spyOn(ServerProcess.prototype, "start").mockImplementation(async function (
        this: ServerProcess,
        timeoutMs?: number,
    ) {
        const port = await originalStart.call(this, timeoutMs);
        const child = (this as unknown as { _process: ChildProcess | null })._process;
        if (child?.pid !== undefined) {
            pids.push(child.pid);
        }
        return port;
    });
    return pids;
}

describe("Beebium.launch failure", () => {
    afterEach(() => {
        vi.restoreAllMocks();
    });

    it("stops the server when the protocol fingerprint does not match", async () => {
        const pids = recordStartedPids();
        vi.spyOn(Beebium.prototype as unknown as { verifyProtocol: () => Promise<void> }, "verifyProtocol")
            .mockRejectedValue(new ProtocolMismatchError("simulated protocol mismatch"));

        await expect(Beebium.launch({ model: "B" })).rejects.toBeInstanceOf(ProtocolMismatchError);

        expect(pids).toHaveLength(1);
        expect(pids.filter(isAlive)).toEqual([]);
    });

    it("stops the server when the connection never becomes ready", async () => {
        const pids = recordStartedPids();
        vi.spyOn(Connection.prototype, "waitForReady")
            .mockRejectedValue(new TimeoutError("simulated connection timeout"));

        await expect(Beebium.launch({ model: "B" })).rejects.toBeInstanceOf(TimeoutError);

        expect(pids).toHaveLength(1);
        expect(pids.filter(isAlive)).toEqual([]);
    });
});
