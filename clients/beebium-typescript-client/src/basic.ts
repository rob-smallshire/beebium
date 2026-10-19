/**
 * BBC BASIC workflow helpers for the Beebium TypeScript client.
 *
 * Provides high-level functions for common operations like waiting for
 * prompts, running programs, and reading screen output.
 */

import type { Beebium } from "./client.js";
import { SCREEN_MODES, readMode7Screen, rowStartsWith, screenContains } from "./screen.js";
import { TimeoutError } from "./exceptions.js";

/** The BBC BASIC prompt. */
const PROMPT = ">";

function delay(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * BBC BASIC workflow helpers.
 *
 * Provides high-level functions for working with BBC BASIC including
 * waiting for prompts, running programs, and reading screen output.
 *
 * Usage:
 *     await bbc.basic.waitForPrompt();
 *     await bbc.basic.runProgram("10 PRINT \"HELLO\"\n20 END");
 *     const text = await bbc.basic.readScreenText(0, 5);
 */
export class Basic {
    private readonly client: Beebium;

    constructor(client: Beebium) {
        this.client = client;
    }

    /**
     * Wait for the BASIC ">" prompt to appear on the MODE 7 display.
     *
     * Polls the displayed MODE 7 screen for a line starting with ">"; a
     * display that is not MODE 7 has none yet.
     *
     * @param timeoutMs - Maximum time to wait in milliseconds (default 5000).
     * @param pollIntervalMs - How often to check in milliseconds (default 10).
     * @returns True if the prompt was found.
     * @throws TimeoutError if the prompt does not appear within the timeout.
     */
    async waitForPrompt(timeoutMs = 5000, pollIntervalMs = 10): Promise<boolean> {
        const deadline = Date.now() + timeoutMs;

        while (Date.now() < deadline) {
            if (await rowStartsWith(this.client.video, PROMPT)) {
                return true;
            }
            await delay(pollIntervalMs);
        }

        throw new TimeoutError(
            `BASIC prompt not found within ${timeoutMs}ms`,
        );
    }

    /**
     * Wait for specific text to appear on the Mode 7 screen.
     *
     * @param text - The text to look for.
     * @param timeoutMs - Maximum time to wait in milliseconds (default 5000).
     * @param pollIntervalMs - How often to check in milliseconds (default 10).
     * @param caseSensitive - Whether the search is case-sensitive (default true).
     * @returns True if the text was found.
     * @throws TimeoutError if the text does not appear within the timeout.
     */
    async waitForText(
        text: string,
        timeoutMs = 5000,
        pollIntervalMs = 10,
        caseSensitive = true,
    ): Promise<boolean> {
        const deadline = Date.now() + timeoutMs;

        while (Date.now() < deadline) {
            if (await screenContains(this.client.video, text, caseSensitive)) {
                return true;
            }
            await delay(pollIntervalMs);
        }

        throw new TimeoutError(
            `Text '${text}' not found within ${timeoutMs}ms`,
        );
    }

    /**
     * Read text from the displayed screen.
     *
     * @param startRow - First row to read (0-based, default 0).
     * @param endRow - Last row to read (exclusive). Undefined means all rows for the mode.
     * @param mode - Screen mode (default 7 for teletext).
     * @returns The screen text as a string with newlines between rows. In mode
     *   7 it is blank until a MODE 7 frame has been displayed; ask
     *   `video.teletextScreen()` for `active` to know whether the display is MODE 7.
     */
    async readScreenText(startRow = 0, endRow?: number, mode = 7): Promise<string> {
        const modeInfo = SCREEN_MODES[mode];
        if (modeInfo === undefined) {
            throw new Error(`Unsupported mode: ${mode}`);
        }

        const { lines } = modeInfo;
        const effectiveEndRow = endRow ?? lines;

        const clampedStart = Math.max(0, Math.min(startRow, lines - 1));
        const clampedEnd = Math.max(clampedStart + 1, Math.min(effectiveEndRow, lines));

        if (mode === 7) {
            return readMode7Screen(this.client.video, clampedStart, clampedEnd);
        }

        // Graphics modes: decoding pixels to characters is not implemented.
        const rows: string[] = [];
        for (let row = clampedStart; row < clampedEnd; row++) {
            rows.push("[graphics mode - not implemented]");
        }

        return rows.join("\n");
    }

    /**
     * Type and run a BASIC program.
     *
     * Waits for the BASIC prompt, types NEW, enters each line of the
     * program, then types RUN. Optionally waits for the program to
     * complete (prompt reappears).
     *
     * @param source - The BASIC program source code.
     * @param waitForCompletion - Whether to wait for the program to finish (default true).
     * @param completionTimeoutMs - Maximum time to wait for completion in milliseconds (default 10000).
     */
    async runProgram(
        source: string,
        waitForCompletion = true,
        completionTimeoutMs = 10000,
    ): Promise<void> {
        // Wait for the prompt first
        await this.waitForPrompt();

        // Type NEW to clear any existing program
        await this.client.keyboard.type("NEW\r");
        await delay(100);

        // Type each line of the program
        const lines = source.trim().split("\n");
        for (const line of lines) {
            await this.client.keyboard.type(line.trim() + "\r");
            await delay(50);
        }

        // Wait for prompt after entering program
        await this.waitForPrompt();

        // Run the program
        await this.client.keyboard.type("RUN\r");

        if (waitForCompletion) {
            // Give program time to start
            await delay(100);
            await this.waitForPrompt(completionTimeoutMs);
        }
    }

    /**
     * Type text and press RETURN.
     *
     * @param text - The text to type.
     * @param enterDelayMs - Delay before pressing RETURN in milliseconds (default 50).
     */
    async typeAndEnter(text: string, enterDelayMs = 50): Promise<void> {
        await this.client.keyboard.type(text);
        await delay(enterDelayMs);
        await this.client.keyboard.pressReturn();
    }

    /**
     * Get the current HIMEM value.
     *
     * HIMEM is stored at 0x0006-0x0007 as a little-endian 16-bit value.
     *
     * @returns The HIMEM address (top of BASIC memory).
     */
    async getHimem(): Promise<number> {
        const data = await this.client.memory.address.bus.read(0x0006, 2);
        return data[0]! | (data[1]! << 8);
    }

    /**
     * Get the current PAGE value.
     *
     * PAGE is stored at 0x0018-0x0019 as a little-endian 16-bit value.
     *
     * @returns The PAGE address (start of BASIC program area).
     */
    async getPage(): Promise<number> {
        const data = await this.client.memory.address.bus.read(0x0018, 2);
        return data[0]! | (data[1]! << 8);
    }

    /**
     * Get the current TOP value.
     *
     * TOP is stored at 0x0012-0x0013 as a little-endian 16-bit value.
     *
     * @returns The TOP address (end of current BASIC program).
     */
    async getTop(): Promise<number> {
        const data = await this.client.memory.address.bus.read(0x0012, 2);
        return data[0]! | (data[1]! << 8);
    }
}
