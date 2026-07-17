/**
 * Server subprocess management for the Beebium TypeScript client.
 *
 * Manages the lifecycle of a beebium-model-b server process, including
 * startup with port allocation, health checking, and graceful shutdown.
 */

import { spawn, type ChildProcess } from "node:child_process";
import { randomUUID } from "node:crypto";
import { existsSync } from "node:fs";
import { join } from "node:path";

import { ServerStartupError, ServerNotFoundError } from "./exceptions.js";
import { VERSION } from "./version.js";

/** Default build directory for the beebium server executables. */
const DEFAULT_BUILD_DIRPATH = "/Users/rjs/Code/beebium/build/src/server";

/** Map from model name to server executable suffix. */
const MODEL_SUFFIXES: Record<string, string> = {
    "B": "",
    "B+": "-plus",
    "B-RomRam": "-romram",
};

export interface ServerProcessOptions {
    /** Path to the server executable. If not specified, auto-detected from model. */
    executableFilepath?: string;
    /** Machine model (default "B"). */
    model?: "B" | "B+" | "B-RomRam";
    /** Additional command-line arguments to pass to the server. */
    args?: string[];
    /** Server startup timeout in milliseconds (default 5000). */
    timeout?: number;
    /** Provenance type string (default "typescript-client"). */
    provenanceType?: string;
    /** Provenance version string (default: the client VERSION). */
    provenanceVersion?: string;
}

/**
 * Manages a beebium-server subprocess.
 *
 * Handles startup with automatic port allocation, stdout parsing for
 * the listening port, and graceful shutdown via SIGTERM/SIGKILL.
 *
 * Usage:
 *     const server = new ServerProcess({ model: "B" });
 *     const port = await server.start();
 *     // ... use the server ...
 *     await server.stop();
 */
export class ServerProcess {
    private _process: ChildProcess | null = null;
    private _port = 0;
    private _exitCleanup: (() => void) | null = null;

    readonly provenanceUuid: string;

    private readonly executableFilepath: string;
    private readonly extraArgs: string[];
    private readonly provenanceType: string;
    private readonly provenanceVersion: string;

    constructor(options?: ServerProcessOptions) {
        this.provenanceUuid = randomUUID();
        this.provenanceType = options?.provenanceType ?? "typescript-client";
        this.provenanceVersion = options?.provenanceVersion ?? VERSION;
        this.extraArgs = options?.args ?? [];

        if (options?.executableFilepath) {
            this.executableFilepath = options.executableFilepath;
        } else if (process.env["BEEBIUM_SERVER"]) {
            // Environment variable override (used in CI and when the server
            // is not in the default build location).
            this.executableFilepath = process.env["BEEBIUM_SERVER"];
        } else {
            const model = options?.model ?? "B";
            const suffix = MODEL_SUFFIXES[model];
            if (suffix === undefined) {
                throw new ServerNotFoundError(`Unknown model: ${model}`);
            }
            const executableName = `beebium-model-b${suffix}`;
            const filepath = join(DEFAULT_BUILD_DIRPATH, executableName);
            if (!existsSync(filepath)) {
                throw new ServerNotFoundError(
                    `Server executable not found at ${filepath}. ` +
                    `Set the BEEBIUM_SERVER environment variable to the path of the server executable.`,
                );
            }
            this.executableFilepath = filepath;
        }
    }

    /**
     * Start the server process and wait for it to report its listening port.
     *
     * @param timeoutMs - Maximum time to wait for the server to start (default 5000).
     * @returns The port the server is listening on.
     * @throws ServerStartupError if the server fails to start.
     */
    async start(timeoutMs = 5000): Promise<number> {
        if (this._process !== null) {
            throw new ServerStartupError("Server is already running");
        }

        const args = [
            "--port", "0",
            "--wait=api",
            "--provenance-type", this.provenanceType,
            "--provenance-uuid", this.provenanceUuid,
            "--provenance-version", this.provenanceVersion,
            ...this.extraArgs,
        ];

        return new Promise<number>((resolve, reject) => {
            const child = spawn(this.executableFilepath, args, {
                stdio: ["ignore", "pipe", "pipe"],
            });

            this._process = child;

            let stdout = "";
            let stderr = "";
            let resolved = false;

            const timer = setTimeout(() => {
                if (!resolved) {
                    resolved = true;
                    reject(
                        new ServerStartupError(
                            `Server did not start within ${timeoutMs}ms\nstdout: ${stdout}\nstderr: ${stderr}`,
                        ),
                    );
                }
            }, timeoutMs);

            child.stdout?.on("data", (chunk: Buffer) => {
                stdout += chunk.toString();
                // Look for the port announcement
                const match = stdout.match(/Listening on port (\d+)/);
                if (match && !resolved) {
                    resolved = true;
                    clearTimeout(timer);
                    this._port = parseInt(match[1]!, 10);
                    resolve(this._port);
                }
            });

            child.stderr?.on("data", (chunk: Buffer) => {
                stderr += chunk.toString();
            });

            child.on("error", (err) => {
                if (!resolved) {
                    resolved = true;
                    clearTimeout(timer);
                    reject(
                        new ServerStartupError(
                            `Failed to spawn server: ${err.message}`,
                        ),
                    );
                }
            });

            child.on("exit", (code, signal) => {
                if (!resolved) {
                    resolved = true;
                    clearTimeout(timer);
                    reject(
                        new ServerStartupError(
                            `Server exited unexpectedly (code=${code}, signal=${signal})\nstdout: ${stdout}\nstderr: ${stderr}`,
                        ),
                    );
                }
            });

            // Register process exit cleanup to kill server if the Node process exits
            const cleanup = () => {
                if (child.exitCode === null && child.signalCode === null) {
                    child.kill("SIGTERM");
                }
            };
            process.on("exit", cleanup);
            this._exitCleanup = cleanup;
        });
    }

    /**
     * Stop the server process.
     *
     * Sends SIGTERM first, then SIGKILL if the server doesn't exit within
     * the timeout.
     *
     * @param timeoutMs - Maximum time to wait for graceful shutdown (default 2000).
     */
    async stop(timeoutMs = 2000): Promise<void> {
        const child = this._process;
        if (child === null) {
            return;
        }

        // Remove process exit cleanup handler
        if (this._exitCleanup !== null) {
            process.removeListener("exit", this._exitCleanup);
            this._exitCleanup = null;
        }

        // Already exited?
        if (child.exitCode !== null || child.signalCode !== null) {
            this._process = null;
            return;
        }

        return new Promise<void>((resolve) => {
            const forceKillTimer = setTimeout(() => {
                child.kill("SIGKILL");
            }, timeoutMs);

            child.once("exit", () => {
                clearTimeout(forceKillTimer);
                this._process = null;
                resolve();
            });

            child.kill("SIGTERM");
        });
    }

    /** The port the server is listening on (0 if not started). */
    get port(): number {
        return this._port;
    }

    /** The gRPC target string (e.g. "localhost:12345"). */
    get target(): string {
        return `localhost:${this._port}`;
    }

    /** Whether the server process is currently running. */
    get isRunning(): boolean {
        if (this._process === null) {
            return false;
        }
        return this._process.exitCode === null && this._process.signalCode === null;
    }
}
