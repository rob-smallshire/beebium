// Consumer-side smoke test for the packed npm tarball.
//
// Run from a directory where `@beebium/client` has been installed from `npm pack`
// output with --omit=dev. Everything here goes through the package name, so
// resolution follows package.json's "main" and the declared dependencies --
// not this repo's node_modules, and not a bundler.
//
// This exists because the checks that run against the working tree cannot see
// how the published package behaves. `npm ci` installs devDependencies, so a
// runtime import satisfied only by hoisting out of a dev-only package still
// resolves; and vitest resolves imports the way a bundler would, which accepts
// extensionless specifiers that Node's ESM resolver rejects. Both masked real
// breakage until this ran.

const failures = [];

// Awaits fn: several checks are async, and a sync try/catch would let their
// rejections escape and report a pass.
const check = async (what, fn) => {
    try {
        await fn();
        console.log(`  ok    ${what}`);
    } catch (error) {
        failures.push(`${what}: ${error.message}`);
        console.log(`  FAIL  ${what}: ${error.message}`);
    }
};

// The entry point named by "main". Importing it pulls in the generated stubs
// transitively, and with them protobufjs and long.
const pkg = await import("@beebium/client");

await check("entry point exports its client classes", () => {
    const expected = ["Beebium", "Connection", "CPU", "Memory", "System", "Video", "Keyboard"];
    const missing = expected.filter((name) => typeof pkg[name] !== "function");
    if (missing.length > 0) {
        throw new Error(`not exported as classes: ${missing.join(", ")}`);
    }
});

// A generated stub encodes and decodes every message on the wire, and is what
// imports protobufjs/minimal. Resolving it is necessary but not sufficient:
// round-trip a message so the dependency is exercised, not merely present.
await check("generated stub round-trips a message through protobufjs", async () => {
    const { VideoConfig } = await import("@beebium/client/dist/generated/video.js");
    const encoded = VideoConfig.encode(VideoConfig.fromPartial({})).finish();
    const decoded = VideoConfig.decode(encoded);
    if (typeof decoded !== "object" || decoded === null) {
        throw new Error("decode did not yield a message");
    }
});

// ts-proto is a code generator. It runs at build time and has no business in a
// consumer's runtime tree; if it reappears here, it has been declared as a
// runtime dependency again.
await check("ts-proto is absent from the runtime tree", async () => {
    let resolved = false;
    try {
        await import("ts-proto");
        resolved = true;
    } catch {
        // expected
    }
    if (resolved) {
        throw new Error("ts-proto resolved; it belongs in devDependencies");
    }
});

if (failures.length > 0) {
    console.error(`\n${failures.length} check(s) failed`);
    process.exit(1);
}
console.log("\nclean-room package smoke: all checks passed");
