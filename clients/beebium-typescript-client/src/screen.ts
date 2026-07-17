/**
 * Screen reading utilities for the Beebium TypeScript client.
 *
 * Provides Mode 7 screen memory reading and text search functions.
 * Functions accept a generic read function so they can work with any
 * memory accessor without a direct dependency on the Memory class.
 */

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

/** Mode 7 (teletext) screen base address. */
export const MODE7_BASE = 0x7C00;

/** Mode 7 bytes per line (one byte per character). */
export const MODE7_BYTES_PER_LINE = 40;

/** Mode 7 number of display lines. */
export const MODE7_LINES = 25;

/**
 * A function that reads memory from the emulator.
 *
 * This abstraction allows screen reading to work with any memory accessor
 * (bus, peek, region) without coupling to a specific class.
 */
export type ReadFn = (address: number, length: number) => Promise<Buffer>;

/**
 * Read Mode 7 screen memory as text.
 *
 * Bytes in the range 0x20-0x7E are returned as ASCII characters.
 * All other bytes (including teletext control codes 0x00-0x1F
 * and 0x7F) are replaced with spaces.
 *
 * @param readFn - A function that reads memory (e.g., peek.read).
 * @param startRow - First row to read (0-based, default 0).
 * @param endRow - Last row to read (exclusive, default 25).
 * @returns The screen text with newlines between rows, trailing spaces stripped.
 */
export async function readMode7Screen(
    readFn: ReadFn,
    startRow = 0,
    endRow = 25,
): Promise<string> {
    startRow = Math.max(0, Math.min(startRow, MODE7_LINES - 1));
    endRow = Math.max(startRow + 1, Math.min(endRow, MODE7_LINES));

    const totalBytes = (endRow - startRow) * MODE7_BYTES_PER_LINE;
    const startAddr = MODE7_BASE + startRow * MODE7_BYTES_PER_LINE;
    const data = await readFn(startAddr, totalBytes);

    const rows: string[] = [];
    for (let row = 0; row < endRow - startRow; row++) {
        const offset = row * MODE7_BYTES_PER_LINE;
        const chars: string[] = [];
        for (let col = 0; col < MODE7_BYTES_PER_LINE; col++) {
            const b = data[offset + col]!;
            if (b >= 0x20 && b <= 0x7E) {
                chars.push(String.fromCharCode(b));
            } else {
                chars.push(" ");
            }
        }
        rows.push(chars.join("").trimEnd());
    }

    return rows.join("\n");
}

/**
 * Search Mode 7 screen memory for an ASCII string.
 *
 * Searches the raw byte values in screen memory for the given text.
 *
 * @param readFn - A function that reads memory (e.g., peek.read).
 * @param text - The text to search for.
 * @param caseSensitive - Whether the search is case-sensitive (default true).
 * @returns True if the text is found in screen memory.
 */
export async function screenContains(
    readFn: ReadFn,
    text: string,
    caseSensitive = true,
): Promise<boolean> {
    const screenText = await readMode7Screen(readFn);
    if (caseSensitive) {
        return screenText.includes(text);
    }
    return screenText.toUpperCase().includes(text.toUpperCase());
}
