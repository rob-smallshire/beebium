/**
 * Locating the server executable (#122): BEEBIUM_SERVER, then the checkout
 * build, then PATH, each exercised against a temporary directory tree.
 */

import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { chmodSync, mkdirSync, mkdtempSync, realpathSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";

import { ServerNotFoundError } from "../src/exceptions.js";
import { findInBuildTree, findOnPath, locateServer, serverExecutableName } from "../src/server-locator.js";

const EXE = "beebium-model-b";

let root: string;

/** Create an executable file (and its directories) at `filepath`. */
function makeExecutable(filepath: string, mode = 0o755): string {
    mkdirSync(dirname(filepath), { recursive: true });
    writeFileSync(filepath, "#!/bin/sh\n");
    chmodSync(filepath, mode);
    return filepath;
}

/** A directory that exists and holds no server. */
function emptyDir(name: string): string {
    const dirpath = join(root, name);
    mkdirSync(dirpath, { recursive: true });
    return dirpath;
}

beforeEach(() => {
    root = realpathSync(mkdtempSync(join(tmpdir(), "beebium-locator-")));
});

afterEach(() => {
    rmSync(root, { recursive: true, force: true });
});

describe("BEEBIUM_SERVER", () => {
    it("accepts a server binary", () => {
        const exe = makeExecutable(join(root, "anywhere", EXE));
        expect(locateServer(EXE, { env: { BEEBIUM_SERVER: exe }, cwd: emptyDir("cwd"), packageDirpath: emptyDir("pkg") }))
            .toBe(exe);
    });

    it("accepts an install root containing bin/", () => {
        const exe = makeExecutable(join(root, "prefix", "bin", EXE));
        expect(locateServer(EXE, { env: { BEEBIUM_SERVER: join(root, "prefix") }, cwd: emptyDir("cwd"), packageDirpath: emptyDir("pkg") }))
            .toBe(exe);
    });

    it("accepts a bin/ directory itself", () => {
        const exe = makeExecutable(join(root, "prefix", "bin", EXE));
        expect(locateServer(EXE, { env: { BEEBIUM_SERVER: join(root, "prefix", "bin") }, cwd: emptyDir("cwd"), packageDirpath: emptyDir("pkg") }))
            .toBe(exe);
    });

    it("outranks the checkout build", () => {
        const exe = makeExecutable(join(root, "env", EXE));
        makeExecutable(join(root, "checkout", "build", "src", "server", EXE));
        expect(locateServer(EXE, {
            env: { BEEBIUM_SERVER: exe },
            cwd: join(root, "checkout"),
            packageDirpath: emptyDir("pkg"),
        })).toBe(exe);
    });

    it("throws rather than falling through when it names nothing usable", () => {
        makeExecutable(join(root, "checkout", "build", "src", "server", EXE));
        const context = { env: { BEEBIUM_SERVER: join(root, "missing") }, cwd: join(root, "checkout"), packageDirpath: emptyDir("pkg") };
        expect(() => locateServer(EXE, context)).toThrow(ServerNotFoundError);
        expect(() => locateServer(EXE, context)).toThrow(/BEEBIUM_SERVER is .*missing/);
    });

    it("throws for a directory with no server in it", () => {
        const context = { env: { BEEBIUM_SERVER: emptyDir("empty-prefix") }, cwd: emptyDir("cwd"), packageDirpath: emptyDir("pkg") };
        expect(() => locateServer(EXE, context)).toThrow(/no beebium-model-b in it or in its bin\//);
    });
});

describe("the checkout build", () => {
    it("is found walking up from the package's own directory", () => {
        const exe = makeExecutable(join(root, "checkout", "build", "src", "server", EXE));
        const packageDirpath = emptyDir("checkout/clients/beebium-typescript-client/dist");
        expect(locateServer(EXE, { env: {}, cwd: emptyDir("elsewhere"), packageDirpath })).toBe(exe);
    });

    it("is found walking up from the working directory", () => {
        const exe = makeExecutable(join(root, "checkout", "build", "src", "server", EXE));
        const cwd = emptyDir("checkout/some/project/dir");
        expect(locateServer(EXE, { env: {}, cwd, packageDirpath: emptyDir("node_modules/@beebium/client/dist") }))
            .toBe(exe);
    });

    it("finds a multi-config generator's Release subdirectory", () => {
        const exe = makeExecutable(join(root, "checkout", "build", "src", "server", "Release", EXE));
        expect(findInBuildTree(EXE, [emptyDir("checkout/sub")])).toBe(exe);
    });

    it("prefers the single-config location to a Debug subdirectory", () => {
        const direct = makeExecutable(join(root, "checkout", "build", "src", "server", EXE));
        makeExecutable(join(root, "checkout", "build", "src", "server", "Debug", EXE));
        expect(findInBuildTree(EXE, [join(root, "checkout")])).toBe(direct);
    });

    it("outranks PATH", () => {
        const exe = makeExecutable(join(root, "checkout", "build", "src", "server", EXE));
        makeExecutable(join(root, "path-bin", EXE));
        expect(locateServer(EXE, { env: { PATH: join(root, "path-bin") }, cwd: join(root, "checkout"), packageDirpath: emptyDir("pkg") }))
            .toBe(exe);
    });

    it("finds the Windows .exe in a Windows build directory", () => {
        const exeName = serverExecutableName(EXE, "win32");
        expect(exeName).toBe("beebium-model-b.exe");
        const exe = makeExecutable(join(root, "checkout", "build-win-x64-release", "src", "server", "Release", exeName));
        expect(findInBuildTree(exeName, [join(root, "checkout")], "win32")).toBe(exe);
    });

    it("ignores a file that is not executable", () => {
        makeExecutable(join(root, "checkout", "build", "src", "server", EXE), 0o644);
        expect(findInBuildTree(EXE, [join(root, "checkout")])).toBeUndefined();
    });
});

describe("PATH", () => {
    it("is searched after the checkout build", () => {
        const exe = makeExecutable(join(root, "path-b", EXE));
        const pathValue = [join(root, "path-a"), join(root, "path-b")].join(":");
        expect(locateServer(EXE, { env: { PATH: pathValue }, cwd: emptyDir("cwd"), packageDirpath: emptyDir("pkg") }))
            .toBe(exe);
    });

    it("splits on ';' on Windows", () => {
        const exe = makeExecutable(join(root, "win-bin", "beebium-model-b.exe"));
        expect(findOnPath("beebium-model-b.exe", `${join(root, "nothing")};${join(root, "win-bin")}`, "win32")).toBe(exe);
    });
});

describe("nothing found", () => {
    it("throws ServerNotFoundError naming everything searched", () => {
        const cwd = emptyDir("cwd");
        const packageDirpath = emptyDir("pkg");
        const context = { env: { PATH: emptyDir("path-bin") }, cwd, packageDirpath };
        expect(() => locateServer(EXE, context)).toThrow(ServerNotFoundError);
        let message = "";
        try {
            locateServer(EXE, context);
        } catch (error) {
            message = (error as Error).message;
        }
        expect(message).toContain("beebium-model-b not found");
        expect(message).toContain("BEEBIUM_SERVER (not set)");
        expect(message).toContain("build/src/server");
        expect(message).toContain(packageDirpath);
        expect(message).toContain(cwd);
        expect(message).toContain("PATH");
    });
});
