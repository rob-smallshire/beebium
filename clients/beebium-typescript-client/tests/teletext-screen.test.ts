/**
 * Reading the MODE 7 screen as characters.
 *
 * The cells are captured inside the SAA5050 after control codes are resolved,
 * so unlike reading screen memory there is no hardware-scroll offset to undo
 * and no attribute state to re-derive. See
 * docs/discussion/teletext-cell-capture.md.
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
        const screen = await host.video.teletextScreen();
        if (screen.text.includes(">")) return host;
        await new Promise((r) => setTimeout(r, 100));
    }
    throw new Error("machine did not reach the BASIC prompt");
}

describe("teletextScreen", () => {
    it("reads the boot screen back as text", async () => {
        const bbc = await launchAtPrompt();
        const screen = await bbc.video.teletextScreen();

        expect(screen.active).toBe(true);
        expect(screen.rows).toBe(25);
        expect(screen.columns).toBe(40);
        expect(screen.cells).toHaveLength(25 * 40);
        expect(screen.text).toContain("BBC Computer");
        expect(screen.text).toContain("BASIC");
    }, 60000);

    it("joins lines with LF", async () => {
        // The wire carries one canonical form; converting to a platform-native
        // ending is the client's business, where text meets a clipboard.
        const bbc = await launchAtPrompt();
        const { text } = await bbc.video.teletextScreen();

        expect(text).not.toContain("\r");
        expect(text).toContain("\n");
    }, 60000);

    it("has no trailing blank lines", async () => {
        // A screen is 25 rows whatever is written on it.
        const bbc = await launchAtPrompt();
        const { text } = await bbc.video.teletextScreen();

        expect(text).toBe(text.replace(/\n+$/, ""));
    }, 60000);

    it("advances the frame number", async () => {
        const bbc = await launchAtPrompt();
        const first = (await bbc.video.teletextScreen()).frameNumber;
        await new Promise((r) => setTimeout(r, 200));

        expect((await bbc.video.teletextScreen()).frameNumber).toBeGreaterThan(first);
    }, 60000);
});

describe("teletextScreen regions", () => {
    it("returns only the region's cells", async () => {
        const bbc = await launchAtPrompt();
        const region = await bbc.video.teletextScreen({
            row: 0, column: 0, rows: 4, columns: 10,
        });

        expect(region.rows).toBe(4);
        expect(region.columns).toBe(10);
        expect(region.cells).toHaveLength(40);
    }, 60000);

    it("clips a region that runs off the screen", async () => {
        const bbc = await launchAtPrompt();
        const region = await bbc.video.teletextScreen({
            row: 20, column: 30, rows: 100, columns: 100,
        });

        expect(region.rows).toBe(5);
        expect(region.columns).toBe(10);
    }, 60000);

    it("reads the banner row as that text", async () => {
        const bbc = await launchAtPrompt();
        const whole = await bbc.video.teletextScreen();
        const bannerRow = whole.text.split("\n")
            .findIndex((line) => line.includes("BBC Computer"));
        expect(bannerRow).toBeGreaterThanOrEqual(0);

        const region = await bbc.video.teletextScreen({ row: bannerRow, rows: 1 });
        expect(region.rows).toBe(1);
        expect(region.text).toContain("BBC Computer");
    }, 60000);
});

describe("teletextScreen cells", () => {
    it("carries resolved attributes", async () => {
        const bbc = await launchAtPrompt();
        const screen = await bbc.video.teletextScreen();

        for (const cell of screen.cells.slice(0, 200)) {
            expect(cell.fg).toBeGreaterThanOrEqual(0);
            expect(cell.fg).toBeLessThanOrEqual(7);
            expect(cell.character).toBeLessThan(0x80);
        }
    }, 60000);

    it("looks up a cell matching the text", async () => {
        const bbc = await launchAtPrompt();
        const screen = await bbc.video.teletextScreen();

        const lines = screen.text.split("\n");
        const bannerRow = lines.findIndex((line) => line.includes("BBC Computer"));
        const line = lines[bannerRow];
        expect(line).toBeDefined();
        const column = line!.indexOf("BBC Computer");

        expect(String.fromCharCode(screen.cell(bannerRow, column).character)).toBe("B");
    }, 60000);

    it("rejects a cell lookup outside the region", async () => {
        // Silently returning undefined would surface far from the mistake.
        const bbc = await launchAtPrompt();
        const region = await bbc.video.teletextScreen({ rows: 2, columns: 2 });

        expect(() => region.cell(5, 5)).toThrow(RangeError);
    }, 60000);
});
