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
 * Integration test: the server emits a periodic liveness heartbeat on the
 * WatchServerStatus stream. An idle-but-healthy connection still produces
 * regular events, so a client can detect a silently-unreachable server (a
 * network partition or a frozen process) by their absence.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";
import { ServerStatus } from "../src/system.js";

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

describe("server status heartbeat", () => {
    it("delivers periodic heartbeats on an idle status stream", async () => {
        const host = await Beebium.launch({ model: "B", timeoutMs: 20000 });
        activeHosts.add(host);

        const statuses: ServerStatus[] = [];
        // Consume the (blocking) status stream in the background; it ends when
        // the host is closed in afterEach.
        const collecting = (async () => {
            for await (const event of host.system.watchStatus()) {
                statuses.push(event.status);
            }
        })();
        collecting.catch(() => { /* stream ends on close */ });

        // Watch an idle server for a window spanning several heartbeats.
        await new Promise((resolve) => setTimeout(resolve, 4000));

        // Without a heartbeat the server sends a single READY and then nothing.
        // A ~0.5s heartbeat yields several events, at least a couple of which
        // are heartbeats specifically.
        expect(statuses.length).toBeGreaterThanOrEqual(3);
        expect(
            statuses.filter((s) => s === ServerStatus.HEARTBEAT).length,
        ).toBeGreaterThanOrEqual(2);
    }, 30000);
});
