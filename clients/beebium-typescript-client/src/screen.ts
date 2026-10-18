/**
 * Screen reading utilities for the Beebium TypeScript client.
 *
 * The MODE 7 helpers read the screen as displayed, from the server's teletext
 * grid, so they follow hardware scrolling and shadow modes.
 */

import { BeebiumError } from "./exceptions.js";
import type { TeletextScreen } from "./video.js";

/** Screen mode definition. */
export interface ScreenModeInfo {
    base: number;
    bytesPerLine: number;
    lines: number;
    charsPerLine: number;
}

/**
 * Screen memory layout for each BBC Micro display mode.
 *
 * Format: base address, bytes per line, number of lines, characters per line.
 */
export const SCREEN_MODES: Record<number, ScreenModeInfo> = {
    0: { base: 0x3000, bytesPerLine: 640, lines: 32, charsPerLine: 80 },  // 640x256 2-colour, 80 chars
    1: { base: 0x3000, bytesPerLine: 320, lines: 32, charsPerLine: 40 },  // 320x256 4-colour, 40 chars
    2: { base: 0x3000, bytesPerLine: 160, lines: 32, charsPerLine: 20 },  // 160x256 16-colour, 20 chars
    3: { base: 0x4000, bytesPerLine: 640, lines: 25, charsPerLine: 80 },  // 640x200 2-colour (text), 80 chars
    4: { base: 0x5800, bytesPerLine: 320, lines: 32, charsPerLine: 40 },  // 320x256 2-colour, 40 chars
    5: { base: 0x5800, bytesPerLine: 160, lines: 32, charsPerLine: 20 },  // 160x256 4-colour, 20 chars
    6: { base: 0x6000, bytesPerLine: 320, lines: 25, charsPerLine: 40 },  // 320x200 2-colour (text), 40 chars
    7: { base: 0x7C00, bytesPerLine: 40,  lines: 25, charsPerLine: 40 },  // Teletext mode, 40 chars
};

/** What the MODE 7 helpers need: a video service that reads the teletext grid. */
export interface TeletextSource {
    teletextScreen(): Promise<TeletextScreen>;
}

/**
 * The displayed MODE 7 screen as text rows, or null when the display is not
 * MODE 7 (or has shown no MODE 7 frame yet).
 *
 * The cells come from the server's teletext grid, which the SAA5050 fills from
 * the bytes the video fetches, so they follow hardware scrolling and, in a
 * shadow mode, read the RAM on screen (the B+'s shadow RAM; the Integra-B's
 * main RAM while the CPU sees its shadow RAM at &3000) rather than what the
 * CPU sees at &7C00.
 */
async function displayedRows(video: TeletextSource): Promise<string[] | null> {
    const screen = await video.teletextScreen();
    if (!screen.active) return null;
    const rows: string[] = [];
    for (let row = 0; row < screen.rows; row++) {
        const chars: string[] = [];
        for (let column = 0; column < screen.columns; column++) {
            const code = screen.cells[row * screen.columns + column]!.character & 0x7f;
            chars.push(code >= 0x20 && code <= 0x7e ? String.fromCharCode(code) : " ");
        }
        rows.push(chars.join(""));
    }
    return rows;
}

/**
 * Read the displayed MODE 7 screen as text.
 *
 * Characters 0x20-0x7E are returned as ASCII; teletext control codes and 0x7F
 * become spaces. Read in one piece from the last complete frame.
 *
 * @param video - The machine's video service (`bbc.video`).
 * @param startRow - First row to read (0-based, default 0).
 * @param endRow - Last row to read (exclusive, default 25).
 * @returns The screen text with newlines between rows, trailing spaces stripped.
 * @throws BeebiumError If the display is not MODE 7.
 */
export async function readMode7Screen(
    video: TeletextSource,
    startRow = 0,
    endRow = 25,
): Promise<string> {
    const rows = await displayedRows(video);
    if (rows === null) {
        throw new BeebiumError("the display is not MODE 7, so there is no MODE 7 screen to read");
    }
    startRow = Math.max(0, Math.min(startRow, rows.length - 1));
    endRow = Math.max(startRow + 1, Math.min(endRow, rows.length));
    return rows.slice(startRow, endRow).map((row) => row.trimEnd()).join("\n");
}

/**
 * Whether `text` appears on the displayed MODE 7 screen. A display that is not
 * MODE 7 contains nothing, so this can poll a machine that is changing mode.
 *
 * @param video - The machine's video service (`bbc.video`).
 * @param text - The text to search for.
 * @param caseSensitive - Whether the search is case-sensitive (default true).
 */
export async function screenContains(
    video: TeletextSource,
    text: string,
    caseSensitive = true,
): Promise<boolean> {
    const rows = await displayedRows(video);
    if (rows === null) return false;
    const screenText = rows.map((row) => row.trimEnd()).join("\n");
    return caseSensitive
        ? screenText.includes(text)
        : screenText.toUpperCase().includes(text.toUpperCase());
}

/**
 * Whether any displayed MODE 7 row starts with `prefix`; false when the
 * display is not MODE 7.
 */
export async function rowStartsWith(video: TeletextSource, prefix: string): Promise<boolean> {
    const rows = await displayedRows(video);
    return rows !== null && rows.some((row) => row.startsWith(prefix));
}
