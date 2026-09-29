/**
 * Locating a beebium server executable.
 *
 * The same resolution order as the Python client, after an explicit path
 * given to ServerProcess or Beebium.launch:
 *
 *   1. BEEBIUM_SERVER: a server binary, an install root containing bin/, or a
 *      bin/ directory itself. When set it must resolve; a wrong value throws
 *      rather than silently falling through.
 *   2. The checkout build: a build/src/server directory (or another common
 *      CMake build directory name, with or without a multi-config Release or
 *      Debug subdirectory) at or above this package's own directory or the
 *      current working directory.
 *   3. PATH.
 *
 * Nothing is found: ServerNotFoundError names everything that was searched.
 */

import { accessSync, constants, statSync } from "node:fs";
import { delimiter, dirname, join, parse, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { ServerNotFoundError } from "./exceptions.js";

/** Build directory names searched below each ancestor. */
const BUILD_DIRNAMES = ["build", "cmake-build-debug", "cmake-build-release"];
const WINDOWS_BUILD_DIRNAMES = [
    "build-win-x64-release",
    "build-win-x64-debug",
    "out/build/x64-Release",
    "out/build/x64-Debug",
];
/** Per-configuration subdirectories a multi-config generator (Visual Studio,
 * Xcode, Ninja Multi-Config) puts the binaries in; "" is a single-config build. */
const CONFIG_DIRNAMES = ["", "Release", "Debug", "RelWithDebInfo", "MinSizeRel"];
/** How many ancestors of each starting directory are searched. */
const MAX_ANCESTOR_DEPTH = 32;

/** What the locator reads from its surroundings; overridable for tests. */
export interface ServerSearchContext {
    /** Environment variables (default: process.env). */
    env?: Record<string, string | undefined>;
    /** Current working directory (default: process.cwd()). */
    cwd?: string;
    /** This package's own directory (default: the directory of this module). */
    packageDirpath?: string;
    /** Platform (default: process.platform). */
    platform?: NodeJS.Platform;
}

/** The server executable's file name on a platform, e.g. beebium-model-b.exe. */
export function serverExecutableName(baseName: string, platform: NodeJS.Platform = process.platform): string {
    return platform === "win32" ? `${baseName}.exe` : baseName;
}

function isExecutableFile(filepath: string, platform: NodeJS.Platform): boolean {
    try {
        if (!statSync(filepath).isFile()) {
            return false;
        }
        if (platform !== "win32") {
            accessSync(filepath, constants.X_OK);
        }
        return true;
    } catch {
        return false;
    }
}

function isDirectory(dirpath: string): boolean {
    try {
        return statSync(dirpath).isDirectory();
    } catch {
        return false;
    }
}

/** A directory and its ancestors, nearest first, up to MAX_ANCESTOR_DEPTH. */
function ancestors(dirpath: string): string[] {
    const result: string[] = [];
    let current = resolve(dirpath);
    const root = parse(current).root;
    for (let depth = 0; depth <= MAX_ANCESTOR_DEPTH; depth++) {
        result.push(current);
        if (current === root) {
            break;
        }
        current = dirname(current);
    }
    return result;
}

/**
 * Resolve BEEBIUM_SERVER: a binary, an install root containing bin/, or a
 * bin/ directory. Throws when it names nothing usable.
 */
function fromEnvironment(value: string, exeName: string, platform: NodeJS.Platform): string {
    if (isDirectory(value)) {
        for (const candidate of [join(value, "bin", exeName), join(value, exeName)]) {
            if (isExecutableFile(candidate, platform)) {
                return candidate;
            }
        }
        throw new ServerNotFoundError(
            `BEEBIUM_SERVER is ${value}, a directory with no ${exeName} in it or in its bin/.`,
        );
    }
    if (isExecutableFile(value, platform)) {
        return value;
    }
    throw new ServerNotFoundError(`BEEBIUM_SERVER is ${value}, which is not an executable server.`);
}

/** Find the server in a build tree at or above any of `startDirpaths`. */
export function findInBuildTree(
    exeName: string,
    startDirpaths: string[],
    platform: NodeJS.Platform = process.platform,
): string | undefined {
    const buildDirnames = platform === "win32" ? [...BUILD_DIRNAMES, ...WINDOWS_BUILD_DIRNAMES] : BUILD_DIRNAMES;
    const seen = new Set<string>();
    for (const start of startDirpaths) {
        for (const ancestor of ancestors(start)) {
            if (seen.has(ancestor)) {
                continue;
            }
            seen.add(ancestor);
            for (const buildDirname of buildDirnames) {
                const serverDirpath = join(ancestor, buildDirname, "src", "server");
                for (const configDirname of CONFIG_DIRNAMES) {
                    const candidate = join(serverDirpath, configDirname, exeName);
                    if (isExecutableFile(candidate, platform)) {
                        return candidate;
                    }
                }
            }
        }
    }
    return undefined;
}

/** Find the server on PATH. */
export function findOnPath(exeName: string, pathValue: string | undefined, platform: NodeJS.Platform = process.platform): string | undefined {
    const separator = platform === "win32" ? ";" : delimiter;
    for (const dirpath of (pathValue ?? "").split(separator)) {
        if (dirpath === "") {
            continue;
        }
        const candidate = join(dirpath, exeName);
        if (isExecutableFile(candidate, platform)) {
            return candidate;
        }
    }
    return undefined;
}

/**
 * Locate the server executable named `baseName` (e.g. "beebium-model-b").
 *
 * @throws ServerNotFoundError if BEEBIUM_SERVER names nothing usable, or if no
 *     server is found anywhere; the message names everything searched.
 */
export function locateServer(baseName: string, context: ServerSearchContext = {}): string {
    const env = context.env ?? process.env;
    const platform = context.platform ?? process.platform;
    const cwd = context.cwd ?? process.cwd();
    const packageDirpath = context.packageDirpath ?? dirname(fileURLToPath(import.meta.url));
    const exeName = serverExecutableName(baseName, platform);

    const fromEnv = env["BEEBIUM_SERVER"];
    if (fromEnv) {
        return fromEnvironment(fromEnv, exeName, platform);
    }

    const startDirpaths = [packageDirpath, cwd];
    const built = findInBuildTree(exeName, startDirpaths, platform);
    if (built !== undefined) {
        return built;
    }

    const onPath = findOnPath(exeName, env["PATH"] ?? env["Path"], platform);
    if (onPath !== undefined) {
        return onPath;
    }

    throw new ServerNotFoundError(
        `${exeName} not found. Searched: BEEBIUM_SERVER (not set); ` +
        `a build/src/server directory at or above ${startDirpaths.join(" and ")}; ` +
        `and PATH. Set BEEBIUM_SERVER to the server executable or its install root, ` +
        `or pass its path explicitly.`,
    );
}
