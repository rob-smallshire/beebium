/**
 * Integration tests for the serial clients against a real server.
 *
 * Unit coverage (serial.test.ts / rpc_serial.test.ts) mocks the stubs; these
 * exercise the actual gRPC wiring end-to-end: the WatchSerialStatus stream and
 * the rpc-serial peer.
 */

import { describe, it, expect } from "vitest";
import { Beebium } from "../src/client.js";
import { ServerProcess } from "../src/server-process.js";
import { Connection } from "../src/connection.js";
import { withServer } from "./server-harness.js";

describe("serial integration", () => {
    it("streams an initial snapshot and a pushed change", async () => {
        await withServer(async (conn, server) => {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            if (await bbc.debugger.isRunning()) {
                await bbc.debugger.stop(); // freeze chip state so only our write changes it
            }

            const iter = bbc.serial
                .watchStatus({ minIntervalMs: 20 })
                [Symbol.asyncIterator]();

            const first = await iter.next();
            expect(first.done).toBe(false);
            expect(first.value!.hasSerialSocket).toBe(true);
            expect(first.value!.connector).toBe("RS423"); // Model B connector (static label)

            // Flip the Serial ULA's RS423/cassette select; the server pushes it.
            const wantRs423 = !first.value!.rs423Selected;
            await bbc.memory.address.bus.writeByte(0xfe10, wantRs423 ? 0x40 : 0x00);

            let changed;
            for (;;) {
                const next = await iter.next();
                if (next.done) break;
                if (next.value.rs423Selected === wantRs423) {
                    changed = next.value;
                    break;
                }
            }
            expect(changed?.rs423Selected).toBe(wantRs423);
            await iter.return?.();
        });
    });

    it("rpc-serial: send queues bytes that status reports", async () => {
        const server = new ServerProcess({ model: "B", args: ["--rpc-serial"] });
        await server.start(10000);
        const conn = new Connection(server.target);
        await conn.waitForReady(5000);
        try {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            // Stop the machine so the queued bytes are not pulled into the ACIA.
            if (await bbc.debugger.isRunning()) {
                await bbc.debugger.stop();
            }

            const accepted = await bbc.rpcSerial.send(new Uint8Array([1, 2, 3, 4, 5]));
            expect(accepted).toBe(5);

            const status = await bbc.rpcSerial.getStatus();
            expect(status.rxPending).toBe(5);
        } finally {
            conn.close();
            await server.stop();
        }
    });
});

// host-serial pty/device modes are POSIX-only; the TS integration jobs run on
// Linux + macOS, but guard anyway for local Windows runs.
const isWindows = process.platform === "win32";

describe("host-serial integration", () => {
    it.skipIf(isWindows)("getConfig reports the pty bridge", async () => {
        const server = new ServerProcess({ model: "B", args: ["--host-serial", "mode=pty"] });
        await server.start(10000);
        const conn = new Connection(server.target);
        await conn.waitForReady(5000);
        try {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            const config = await bbc.hostSerial.getConfig();
            expect(config.mode).toBe("pty");
            expect(config.path.length).toBeGreaterThan(0);
            expect(config.serialOpen).toBe(true);
        } finally {
            conn.close();
            await server.stop();
        }
    });

    it.skipIf(isWindows)("setConfig rejects a runtime switch to pty", async () => {
        const server = new ServerProcess({ model: "B", args: ["--host-serial", "mode=pty"] });
        await server.start(10000);
        const conn = new Connection(server.target);
        await conn.waitForReady(5000);
        try {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            await expect(bbc.hostSerial.setConfig({ mode: "pty" })).rejects.toThrow();
        } finally {
            conn.close();
            await server.stop();
        }
    });

    it.skipIf(isWindows)("setConfig re-points to a device and applies a baud-only diff", async () => {
        const server = new ServerProcess({ model: "B", args: ["--host-serial", "mode=pty"] });
        await server.start(10000);
        const conn = new Connection(server.target);
        await conn.waitForReady(5000);
        try {
            const bbc = new Beebium(conn, server, server.provenanceUuid);
            // The re-point lands on the next emulation tick (process_pending_reopen
            // runs at the top of the endpoint's has_data()), so stop the machine
            // and drive ticks explicitly -- feedback, not a fixed sleep.
            if (await bbc.debugger.isRunning()) await bbc.debugger.stop();

            // A deliberately nonexistent path exercises the success-of-the-call
            // path plus the async open-failure reporting, without needing a real
            // device (Node has no built-in pty). The happy-path open is covered
            // by the C++ and Python tests.
            const devicePath = "/nonexistent-beebium-host-serial-device";
            await bbc.hostSerial.setConfig({ mode: "device", path: devicePath, baud: 9600 });

            let config = await bbc.hostSerial.getConfig();
            for (let i = 0; i < 20 && config.mode !== "device"; i++) {
                await bbc.debugger.stepCycles(2000);
                config = await bbc.hostSerial.getConfig();
            }
            expect(config.mode).toBe("device");
            expect(config.path).toBe(devicePath);
            expect(config.baud).toBe(9600);
            expect(config.serialOpen).toBe(false);     // bad path: surfaced, not thrown
            expect(config.openError.length).toBeGreaterThan(0);

            // Partial update: change only baud; the path is kept.
            await bbc.hostSerial.setConfig({ baud: 2400 });
            for (let i = 0; i < 20 && config.baud !== 2400; i++) {
                await bbc.debugger.stepCycles(2000);
                config = await bbc.hostSerial.getConfig();
            }
            expect(config.baud).toBe(2400);
            expect(config.path).toBe(devicePath);
        } finally {
            conn.close();
            await server.stop();
        }
    });
});
