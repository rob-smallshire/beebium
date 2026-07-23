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
import type { TeletextScreenCell } from "../src/generated/video.js";

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

// Character-set codes as they arrive on the wire (proto TeletextCharacterSet).
const CHARSET_CONTIGUOUS = 1;
const CHARSET_SEPARATED = 2;

/**
 * The per-cell attributes the SAA5050 resolves -- colour, character set,
 * concealment, flash, double height -- carried already-resolved on the cell so
 * nothing downstream re-runs the serial control codes. Each is driven end to
 * end with the BASIC control code that sets it: CHR$(129..159) map to the
 * chip's 0x01..0x1F once the top bit is masked. The core capture is pinned
 * deterministically in tests/test_teletext_grid.cpp; here we prove the wire
 * carries it.
 */
describe("teletextScreen cell attributes", () => {
    // Poll the live machine until a cell satisfies the predicate, and return it.
    async function findCell(
        bbc: Beebium,
        predicate: (c: TeletextScreenCell) => boolean,
        timeoutMs = 20000,
    ): Promise<TeletextScreenCell> {
        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline) {
            const screen = await bbc.video.teletextScreen();
            const hit = screen.cells.find(predicate);
            if (hit !== undefined) return hit;
            await new Promise((r) => setTimeout(r, 100));
        }
        throw new Error("no cell matched the attribute predicate");
    }

    it("carries a foreground colour and flags the control code that set it", async () => {
        const bbc = await launchAtPrompt();
        await bbc.keyboard.type('PRINT CHR$(130);"Q"\r'); // alpha green, then Q

        const cell = await findCell(bbc, (c) => c.character === "Q".charCodeAt(0) && c.fg === 2);
        expect(cell.fg).toBe(2);
        expect(cell.isControlCode).toBe(false);

        const control = await findCell(bbc, (c) => c.isControlCode && c.character === 0x02);
        expect(control.isControlCode).toBe(true);
    }, 60000);

    it("carries conceal even though the text hides it", async () => {
        const bbc = await launchAtPrompt();
        await bbc.keyboard.type('PRINT CHR$(152);"Q"\r'); // conceal, then Q

        const cell = await findCell(bbc, (c) => c.character === "Q".charCodeAt(0) && c.concealed);
        expect(cell.concealed).toBe(true);
    }, 60000);

    it("switches the character set on a graphics code", async () => {
        const bbc = await launchAtPrompt();

        await bbc.keyboard.type("PRINT CHR$(151);CHR$(255)\r"); // graphics white (contiguous)
        const contiguous = await findCell(bbc, (c) => c.charset === CHARSET_CONTIGUOUS);
        expect(contiguous.charset).toBe(CHARSET_CONTIGUOUS);

        await bbc.keyboard.type("PRINT CHR$(151);CHR$(154);CHR$(255)\r"); // separated
        const separated = await findCell(bbc, (c) => c.charset === CHARSET_SEPARATED);
        expect(separated.charset).toBe(CHARSET_SEPARATED);
    }, 60000);

    it("carries double height", async () => {
        const bbc = await launchAtPrompt();
        await bbc.keyboard.type('PRINT CHR$(141);"Q"\r'); // double height, then Q

        const cell = await findCell(bbc, (c) => c.character === "Q".charCodeAt(0) && c.doubleHeightTop);
        expect(cell.doubleHeightTop).toBe(true);
        expect(cell.doubleHeightBottom).toBe(false);
    }, 60000);

    it("carries flash on the concealed phase", async () => {
        const bbc = await launchAtPrompt();
        await bbc.keyboard.type('PRINT CHR$(136);"Q"\r'); // flash, then Q

        const cell = await findCell(bbc, (c) => c.character === "Q".charCodeAt(0) && c.flashing);
        expect(cell.flashing).toBe(true);
    }, 60000);
});
