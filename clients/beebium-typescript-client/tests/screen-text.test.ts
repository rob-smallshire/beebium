/**
 * Reading text off the display, whatever mode is producing it.
 *
 * The caller selects in pixels and the server picks a reading strategy per
 * band of scanlines, so nothing here mentions a mode except to set one up.
 * See docs/discussion/screen-text-extraction.md.
 */

import { describe, it, expect, afterEach } from "vitest";
import { Beebium } from "../src/client.js";

const activeHosts = new Set<Beebium>();

afterEach(async () => {
    const survivors = [...activeHosts];
    activeHosts.clear();
    await Promise.all(survivors.map(async (h) => {
        try { await h.close(); } catch { /* ignore */ }
    }));
});

async function launchAtPrompt(): Promise<Beebium> {
    const host = await Beebium.launch({ model: "B", timeoutMs: 20000 });
    activeHosts.add(host);
    await host.debugger.run();

    // Wait for BASIC rather than a fixed delay.
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
        const screen = await host.video.screenText();
        if (screen.text.includes(">")) return host;
        await new Promise((r) => setTimeout(r, 100));
    }
    throw new Error("machine did not reach the BASIC prompt");
}

/**
 * Leave the machine in a bitmap mode with something printed on it.
 *
 * Waited on through the geometry rather than through the text API: whether
 * that API can read a bitmap mode is the very thing under test.
 *
 * The wait keys on the cell width, which is 16 for the SAA5050 (two batches of
 * eight) and 8 for every bitmap mode. The row pitch is no good for this: a
 * frame captured while the CRTC is being reprogrammed is briefly shorter than a
 * screen, so the pitch derived from it dips before the mode has actually
 * changed.
 */
const TELETEXT_CELL_WIDTH = 16;

async function switchToMode(bbc: Beebium, mode: number, message: string): Promise<void> {
    await bbc.keyboard.type(`MODE ${mode}\rPRINT "${message}"\r`);

    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
        const band = (await bbc.video.screenGeometry()).bands[0];
        if (band !== undefined && band.cellWidth !== TELETEXT_CELL_WIDTH) return;
        await new Promise((r) => setTimeout(r, 100));
    }
    throw new Error(`machine did not reach MODE ${mode}`);
}

/** Advance the machine until the screen text satisfies a predicate. */
async function waitFor(
    bbc: Beebium,
    predicate: (text: string) => boolean,
): Promise<void> {
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) {
        if (predicate((await bbc.video.screenText()).text)) return;
        await new Promise((r) => setTimeout(r, 100));
    }
    throw new Error("screen text did not satisfy the predicate in time");
}

describe("screenText in teletext", () => {
    it("reads the boot banner", async () => {
        const bbc = await launchAtPrompt();
        const screen = await bbc.video.screenText();

        expect(screen.supported).toBe(true);
        expect(screen.text).toContain("BBC Computer");
        expect(screen.text).toContain("BASIC");
    }, 60000);

    it("is never uncertain, its cells being exact character codes", async () => {
        const bbc = await launchAtPrompt();
        const screen = await bbc.video.screenText();

        expect(screen.unreadableCells).toBe(0);
        expect(screen.ambiguousCells).toBe(0);
    }, 60000);

    it("returns runs and not only a string", async () => {
        const bbc = await launchAtPrompt();
        const screen = await bbc.video.screenText();

        expect(screen.runs.length).toBeGreaterThan(0);
        const banner = screen.runs.find((run) => run.text.includes("BBC Computer"));
        expect(banner).toBeDefined();
        expect(banner!.cellWidth).toBeGreaterThan(0);
        expect(banner!.cellHeight).toBeGreaterThan(0);
    }, 60000);

    it("joins lines with LF", async () => {
        // The wire carries one canonical form; converting to a platform-native
        // ending is the client's business, where text meets a clipboard.
        const bbc = await launchAtPrompt();
        const { text } = await bbc.video.screenText();

        expect(text).not.toContain("\r");
        expect(text).toContain("\n");
    }, 60000);

    it("agrees with the teletext reading", async () => {
        // Both go to the same capture, so they must not disagree about what a
        // MODE 7 screen says.
        const bbc = await launchAtPrompt();

        const viaScreenText = (await bbc.video.screenText()).text;
        const viaTeletext = (await bbc.video.teletextScreen()).text;

        expect(viaScreenText).toBe(viaTeletext);
    }, 60000);
});

describe("screenText in bitmap modes", () => {
    it("reads MODE 4 text through the same API", async () => {
        // The glyph-recognising strategy reads the bitmap display through the
        // same API and client that once returned "not supported" here -- the
        // property the whole design is judged on.
        const bbc = await launchAtPrompt();
        await switchToMode(bbc, 4, "BITMAP TEXT");
        await waitFor(bbc, (text) => text.includes("BITMAP TEXT"));

        const screen = await bbc.video.screenText();

        expect(screen.supported).toBe(true);
        expect(screen.text).toContain("BITMAP TEXT");
        expect(screen.text).not.toContain("BBC Computer");
    }, 60000);

    it("carries per-cell readability so a client highlights only what it read", async () => {
        const bbc = await launchAtPrompt();
        await switchToMode(bbc, 4, "READABLE");
        await waitFor(bbc, (text) => text.includes("READABLE"));

        const run = (await bbc.video.screenText()).runs.find((r) =>
            r.text.includes("READABLE"),
        );
        expect(run).toBeDefined();
        expect(run!.cells.length).toBeGreaterThan(0);
        expect(run!.cells.every((cell) => cell.matched)).toBe(true);
    }, 60000);
});

describe("screenGeometry", () => {
    it("reports one 40x25 band in teletext", async () => {
        const bbc = await launchAtPrompt();
        const geometry = await bbc.video.screenGeometry();

        expect(geometry.bands).toHaveLength(1);
        const band = geometry.bands[0]!;
        expect(Math.floor((band.bottom - band.top) / band.rowPitch)).toBe(25);
        expect(band.cellWidth).toBe(16);
        expect(band.columnPitch).toBe(16);
    }, 60000);

    it("reports a bitmap grid for a band it cannot read", async () => {
        // Snapping is about where the cells are, reading about what is in
        // them, and they are separate questions.
        const bbc = await launchAtPrompt();
        await switchToMode(bbc, 4, "ANYTHING");

        const band = (await bbc.video.screenGeometry()).bands[0]!;

        expect(band.cellWidth).toBe(8);
        expect(band.columnPitch).toBe(8);
        expect(band.cellHeight).toBe(8);
        expect(band.rowPitch).toBe(8);
    }, 60000);

    it("puts an eight-line glyph on a ten-line pitch in MODE 6", async () => {
        // The two spare scanlines are blanked rather than painted with the
        // background colour, so the cell is shorter than the step. Reporting
        // the pitch as the cell size is silent while the background is black
        // and total once it is not.
        const bbc = await launchAtPrompt();
        await switchToMode(bbc, 6, "ANYTHING");

        const band = (await bbc.video.screenGeometry()).bands[0]!;

        expect(band.rowPitch).toBe(10);
        expect(band.cellHeight).toBe(8);
    }, 60000);

    it("gives every band a usable grid", async () => {
        const bbc = await launchAtPrompt();

        for (const band of (await bbc.video.screenGeometry()).bands) {
            expect(band.bottom).toBeGreaterThan(band.top);
            expect(band.cellWidth).toBeGreaterThan(0);
            expect(band.cellHeight).toBeGreaterThan(0);
            expect(band.columnPitch).toBeGreaterThanOrEqual(band.cellWidth);
            expect(band.rowPitch).toBeGreaterThanOrEqual(band.cellHeight);
        }
    }, 60000);
});
