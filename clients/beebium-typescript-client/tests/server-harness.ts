/**
 * Shared test harness for integration tests that launch server processes.
 *
 * Tracks all spawned servers and kills any survivors after each test,
 * preventing orphaned processes when Vitest aborts a timed-out test
 * before the finally block in withServer can run.
 */

import { afterEach } from "vitest";
import { ServerProcess } from "../src/server-process.js";
import { Connection } from "../src/connection.js";

/** All servers spawned during the current test. */
const activeServers = new Set<ServerProcess>();

/**
 * After each test, forcibly stop any servers that are still running.
 * This catches cases where a test timeout prevented normal cleanup.
 */
afterEach(async () => {
    const survivors = [...activeServers].filter((s) => s.isRunning);
    activeServers.clear();
    await Promise.all(survivors.map((s) => s.stop()));
});

/**
 * Launch a fresh server, run the body, and tear down afterwards.
 * Every call produces a completely independent emulator instance.
 *
 * The server is tracked so that if the test times out before the
 * finally block runs, the afterEach hook will kill it.
 */
export async function withServer(
    body: (conn: Connection, server: ServerProcess) => Promise<void>,
): Promise<void> {
    const server = new ServerProcess({ model: "B" });
    activeServers.add(server);
    await server.start(10000);
    const conn = new Connection(server.target);
    await conn.waitForReady(5000);
    try {
        await body(conn, server);
    } finally {
        conn.close();
        await server.stop();
        activeServers.delete(server);
    }
}
