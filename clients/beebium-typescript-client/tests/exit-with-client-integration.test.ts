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
 * A launched server does not outlive the Node process that launched it (#254).
 *
 * Each case runs the launching client as a child Node process, so the child
 * can be killed outright, as a tool's timeout kills a process tree, or exit.
 * The child runs the client compiled from this package's source, prints the
 * server's process id and address, then does as the case says.
 */

import { describe, it, expect, beforeAll, afterEach } from "vitest";
import { execFileSync, spawn, type ChildProcess } from "node:child_process";
import { mkdirSync, writeFileSync } from "node:fs";
import { createInterface } from "node:readline";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { Beebium } from "../src/client.js";
import { BeebiumError } from "../src/exceptions.js";

const PACKAGE_DIRPATH = join(dirname(fileURLToPath(import.meta.url)), "..");
// Inside node_modules, so the compiled client resolves @grpc/grpc-js.
const BUILD_DIRPATH = join(PACKAGE_DIRPATH, "node_modules", ".cache", "exit-with-client");
const LAUNCHER_FILEPATH = join(BUILD_DIRPATH, "launcher.mjs");
const EXIT_TIMEOUT_MS = 15000;

const LAUNCHER = `
import { Beebium } from "./client.js";

const action = process.argv[2];
const bbc = await Beebium.launch({ exitWithClient: action !== "no-watch", timeoutMs: 20000 });
const pid = bbc["server"]["_process"].pid; // private, as nothing public needs it
if (action === "detach") {
    await bbc.detach();
}
console.log(pid, bbc.target);
if (action === "exit") {
    process.exit(0); // without close()
}
if (action === "detach") {
    await bbc.close(); // disconnects only
    process.exit(0);
}
setInterval(() => {}, 1 << 30); // until killed
`;

function isAlive(pid: number): boolean {
    try {
        process.kill(pid, 0);
        return true;
    } catch {
        return false;
    }
}

async function waitUntilGone(pid: number, timeoutMs = EXIT_TIMEOUT_MS): Promise<boolean> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (!isAlive(pid)) {
            return true;
        }
        await new Promise((r) => setTimeout(r, 100));
    }
    return false;
}

const children: ChildProcess[] = [];
const serverPids: number[] = [];

/** Run the launcher with an action; resolve with the server's pid and target. */
async function runLauncher(action: string): Promise<{ child: ChildProcess; pid: number; target: string }> {
    const child = spawn(process.execPath, [LAUNCHER_FILEPATH, action], {
        stdio: ["ignore", "pipe", "inherit"],
    });
    children.push(child);
    const lines = createInterface({ input: child.stdout! });
    const line = await new Promise<string>((resolve, reject) => {
        lines.once("line", resolve);
        child.once("exit", (code) => reject(new Error(`launcher exited (${code}) before a server`)));
    });
    const [pidText, target] = line.split(" ");
    const pid = Number(pidText);
    serverPids.push(pid);
    return { child, pid, target: target! };
}

function exited(child: ChildProcess): Promise<number | null> {
    if (child.exitCode !== null || child.signalCode !== null) {
        return Promise.resolve(child.exitCode);
    }
    return new Promise((resolve) => child.once("exit", (code) => resolve(code)));
}

describe("Beebium.launch exitWithClient", () => {
    beforeAll(() => {
        mkdirSync(BUILD_DIRPATH, { recursive: true });
        execFileSync(join(PACKAGE_DIRPATH, "node_modules", ".bin", "tsc"),
            ["-p", PACKAGE_DIRPATH, "--outDir", BUILD_DIRPATH, "--declaration", "false",
             "--declarationMap", "false", "--sourceMap", "false"],
            { stdio: "inherit" });
        writeFileSync(LAUNCHER_FILEPATH, LAUNCHER);
    }, 120000);

    afterEach(async () => {
        for (const child of children.splice(0)) {
            if (child.exitCode === null && child.signalCode === null) {
                child.kill("SIGKILL");
            }
        }
        for (const pid of serverPids.splice(0)) {
            if (isAlive(pid)) {
                process.kill(pid, "SIGTERM");
                await waitUntilGone(pid);
            }
        }
    });

    it("stops the server when the launcher is killed", async () => {
        const { child, pid } = await runLauncher("wait");
        expect(isAlive(pid)).toBe(true);

        child.kill("SIGKILL"); // no cleanup runs
        await exited(child);

        expect(await waitUntilGone(pid)).toBe(true);
    }, 60000);

    it("without the watch, a killed launcher leaves its server (the control)", async () => {
        const { child, pid } = await runLauncher("no-watch");
        child.kill("SIGKILL");
        await exited(child);

        await new Promise((r) => setTimeout(r, 2000));
        expect(isAlive(pid)).toBe(true);
    }, 60000);

    it("stops the server when the launcher exits without closing", async () => {
        const { child, pid } = await runLauncher("exit");
        expect(await exited(child)).toBe(0);

        expect(await waitUntilGone(pid)).toBe(true);
    }, 60000);

    it("leaves a detached server running after the launcher exits", async () => {
        const { child, pid, target } = await runLauncher("detach");
        expect(await exited(child)).toBe(0);

        await new Promise((r) => setTimeout(r, 2000));
        expect(isAlive(pid)).toBe(true);
        const bbc = await Beebium.connect(target);
        try {
            expect((await bbc.system.requestShutdown()).accepted).toBe(true);
        } finally {
            await bbc.close();
        }
        expect(await waitUntilGone(pid)).toBe(true);
    }, 60000);

    it("lets only the launching client detach or release", async () => {
        const launched = await Beebium.launch({ timeoutMs: 20000 });
        try {
            const other = await Beebium.connect(launched.target);
            try {
                await expect(other.detach()).rejects.toBeInstanceOf(BeebiumError);
                await expect(other.system.releaseFromLauncher()).rejects.toBeInstanceOf(BeebiumError);
            } finally {
                await other.close();
            }
        } finally {
            await launched.close();
        }
    }, 60000);
});
