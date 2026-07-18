/**
 * Keyboard input interface for the Beebium client.
 */

import { promisify } from "./call-utils.js";
import {
    CTRL_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    RETURN_KEY,
    SHIFT_KEY,
    SPACE_KEY,
    charToMatrix,
} from "./keyboard-map.js";
import type {
    AllKeyMappingsResponse,
    BreakDownResponse,
    BreakKeyState,
    BreakUpResponse,
    ClearTypingResponse,
    KeyMappingEntry,
    KeyResponse,
    KeyboardServiceClient,
    KeyboardState as KeyboardStateProto,
    LinksState,
    LockKeyState,
    SetLinksResponse,
    SetStartupAutoBootResponse,
    SetStartupScreenModeResponse,
    StartupAutoBootState,
    StartupScreenModeState,
    TypeQuicklyResponse,
    TypingStatus,
} from "./generated/keyboard.js";
import type { Beebium } from "./client.js";

// CAPS LOCK and SHIFT LOCK live on row 4/5 column 0 of the keyboard matrix.
// These are not modifier keys -- a tap toggles a sticky state held by MOS.
const CAPS_LOCK_KEY: readonly [number, number] = [4, 0];
const SHIFT_LOCK_KEY: readonly [number, number] = [5, 0];

// Cycles the lock key is held before being released. The MOS scans the
// keyboard matrix from a ~10ms timer IRQ and debounces transitions, so
// 50ms held is comfortably above the minimum to register a clean press.
const LOCK_TAP_HOLD_CYCLES = 100_000;

// Bound on emulator cycles spent waiting for the logical lock latch to flip
// after the key is released. The latch is updated by the MOS keyboard scan
// (~100Hz, every ~10ms emulated), so the flip lands within a scan period or
// two. 1,000,000 cycles == 500ms emulated -- tens of scan periods, so a
// timeout indicates a real fault. The bound is in emulated cycles, so it is
// independent of wall-clock emulation speed.
const LOCK_TAP_TIMEOUT_CYCLES = 1_000_000;

// Cycles between logical-latch reads while polling for the flip.
const LOCK_TAP_POLL_CHUNK_CYCLES = 5_000;

// Cycles to spin after the LED edge is observed, giving MOS time to
// scan the key release and complete its keyboard-handler routine.
// Without this settle, a back-to-back tap of the same key can look like
// a continuous press and fail to re-trigger the toggle.
const LOCK_TAP_RELEASE_SETTLE_CYCLES = 100_000;

export {
    CTRL_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    RETURN_KEY,
    SHIFT_KEY,
    SPACE_KEY,
    charToMatrix,
    matrixToChar,
} from "./keyboard-map.js";

/**
 * Current state of the keyboard matrix.
 */
export interface KeyboardState {
    /** Bitmap of pressed keys (10 rows). */
    pressedRows: number[];

    /** Check if a specific key is pressed. */
    isPressed(row: number, column: number): boolean;
}

/**
 * Logical CAPS LOCK / SHIFT LOCK state, as held by the MOS.
 *
 * These are the exact latch bits, not the duty-cycle-filtered lock-LED
 * brightness -- so they are correct at any emulation speed.
 */
export interface LockState {
    capsLock: boolean;
    shiftLock: boolean;
}

function createKeyboardState(pressedRows: number[]): KeyboardState {
    return {
        pressedRows,
        isPressed(row: number, column: number): boolean {
            if (row >= 0 && row < pressedRows.length) {
                const rowValue = pressedRows[row];
                return rowValue !== undefined && (rowValue & (1 << column)) !== 0;
            }
            return false;
        },
    };
}

/**
 * Keyboard input interface.
 *
 * Provides both low-level matrix access and high-level text input.
 *
 * Usage:
 *     // Type text (cycle-paced, use \r for RETURN)
 *     await keyboard.type("PRINT 42\r");
 *
 *     // Press specific keys
 *     await keyboard.keyDown('A');
 *     await keyboard.keyUp('A');
 *
 *     // Matrix-level access
 *     await keyboard.matrixDown(4, 1);  // 'A' key
 */
export class Keyboard {
    private readonly _stub: KeyboardServiceClient;
    private readonly _pressedKeys = new Set<string>();
    private readonly _client: Beebium | undefined;

    /**
     * Construct a keyboard wrapper.
     *
     * @param stub - gRPC stub for the KeyboardService.
     * @param client - Optional reference to the parent Beebium client.
     *   Required by `withTextInput()` and `tapCapsLock()`/`tapShiftLock()`,
     *   which read the addressable-latch LED state and step the emulator
     *   between key down and key up so the MOS can scan the matrix.
     */
    constructor(stub: KeyboardServiceClient, client?: Beebium) {
        this._stub = stub;
        this._client = client;
    }

    // =========================================================================
    // High-level text input (cycle-paced via server-side TypeAheadQueue)
    // =========================================================================

    /**
     * Type a string of text, paced reliably by the server.
     *
     * The text is enqueued and typed character-by-character as the
     * emulator runs. Use `\r` for RETURN, `\x1b` for ESCAPE,
     * `\x7f` for DELETE, and `\t` for TAB.
     *
     * Returns immediately; the emulator must be running for the
     * keystrokes to be processed. The server paces keystrokes to be
     * reliably registered by the MOS keyboard scan, so there is no
     * timing knob to tune. For deliberate custom timing, drive
     * `keyDown` / `keyUp` directly.
     *
     * By default the server translates host text conventions into what the
     * BBC keyboard can type: every line-ending convention becomes a single
     * `\r`, typographic punctuation folds to ASCII, and the SAA5050 teletext
     * glyphs map back to the codes that produce them. So `\n` and `\r\n` both
     * mean RETURN once, not twice.
     *
     * Pass `translate: false` to type the text exactly as given. Note that
     * `\r\n` then presses RETURN twice, because both CR and LF are RETURN.
     *
     * @param text - The text to type.
     * @param options - Optional settings.
     * @param options.translate - Whether the server should translate host text
     *     conventions. Defaults to true.
     * @returns Total pending characters in queue after enqueue.
     * @throws Error if text contains characters that cannot be typed on the
     *     BBC keyboard. The message names the first such character.
     */
    async type(
        text: string,
        options?: { translate?: boolean },
    ): Promise<number> {
        const response = await promisify<
            { text: string; translate?: boolean },
            TypeQuicklyResponse
        >(this._stub as unknown as Record<string, Function>, "typeQuickly", {
            text,
            translate: options?.translate,
        });
        if (!response.accepted) {
            throw new Error(`Text rejected: ${response.error}`);
        }
        return response.pendingCharacters;
    }

    /** Enqueue a RETURN keypress. */
    async pressReturn(): Promise<void> {
        await this.type("\r");
    }

    /** Enqueue an ESCAPE keypress. */
    async pressEscape(): Promise<void> {
        await this.type("\x1b");
    }

    /** Enqueue a DELETE keypress. */
    async pressDelete(): Promise<void> {
        await this.type("\x7f");
    }

    /** Enqueue a SPACE keypress. */
    async pressSpace(): Promise<void> {
        await this.type(" ");
    }

    // =========================================================================
    // Character-level input
    // =========================================================================

    /**
     * Press a key for the given character.
     *
     * Automatically handles SHIFT for uppercase and shifted symbols.
     *
     * @param char - The character to press.
     * @returns True if the character is mapped, false otherwise.
     */
    async keyDown(char: string): Promise<boolean> {
        const mapping = charToMatrix(char);
        if (mapping === undefined) {
            return false;
        }

        const [row, column, needsShift] = mapping;

        if (needsShift) {
            await this.matrixDown(...SHIFT_KEY);
        }
        await this.matrixDown(row, column);

        this._pressedKeys.add(`${char}:${needsShift}`);
        return true;
    }

    /**
     * Release a key for the given character.
     *
     * @param char - The character to release.
     * @returns True if the character is mapped, false otherwise.
     */
    async keyUp(char: string): Promise<boolean> {
        const mapping = charToMatrix(char);
        if (mapping === undefined) {
            return false;
        }

        const [row, column, needsShift] = mapping;

        await this.matrixUp(row, column);

        // Only release shift if no other shifted keys are pressed
        if (needsShift) {
            this._pressedKeys.delete(`${char}:true`);
            let anyShifted = false;
            for (const key of this._pressedKeys) {
                if (key.endsWith(":true")) {
                    anyShifted = true;
                    break;
                }
            }
            if (!anyShifted) {
                await this.matrixUp(...SHIFT_KEY);
            }
        } else {
            this._pressedKeys.delete(`${char}:false`);
        }

        return true;
    }

    /** Release all currently pressed keys. */
    async releaseAll(): Promise<void> {
        const keys = [...this._pressedKeys];
        for (const key of keys) {
            const char = key.substring(0, key.lastIndexOf(":"));
            await this.keyUp(char);
        }
    }

    // =========================================================================
    // Lock keys (CAPS LOCK and SHIFT LOCK)
    // =========================================================================
    // CAPS LOCK and SHIFT LOCK toggle a sticky state held by the MOS. A "tap"
    // is a complete key-down/key-up cycle with enough emulated time between
    // for the MOS keyboard scan to detect both edges. The current state can
    // be read from the addressable-latch LED bits.

    /**
     * Get the logical CAPS LOCK and SHIFT LOCK state.
     *
     * Reads the MOS-maintained lock latch directly, so the result is exact
     * and correct at any emulation speed -- unlike the lock-LED brightness
     * from the IndicatorService, which is duty-cycle filtered over a
     * wall-clock window for display and only settles to 0/255 in real time.
     */
    async getLockState(): Promise<LockState> {
        const response = await promisify<Record<string, never>, LockKeyState>(
            this._stub as unknown as Record<string, Function>,
            "getLockState",
            {},
        );
        return { capsLock: response.capsLockOn, shiftLock: response.shiftLockOn };
    }

    /**
     * Tap the CAPS LOCK key, toggling the MOS caps-lock state.
     *
     * Polls the logical lock latch (via `getLockState`) until it has flipped,
     * confirming that the MOS keyboard scan has registered the toggle before
     * returning.
     *
     * The emulator must be running on entry -- the MOS keyboard scan
     * is interrupt-driven and only progresses while CPU cycles are
     * being executed.
     *
     * @param timeoutCycles - Maximum emulator cycles to wait for the latch to
     *   flip. Default 1,000,000 cycles (500ms emulated).
     * @throws Error if the emulator is not running, or if the latch fails to
     *   flip within `timeoutCycles`.
     */
    async tapCapsLock(timeoutCycles: number = LOCK_TAP_TIMEOUT_CYCLES): Promise<void> {
        await this._tapLockKey(
            CAPS_LOCK_KEY[0],
            CAPS_LOCK_KEY[1],
            "CAPS LOCK",
            timeoutCycles,
        );
    }

    /**
     * Tap the SHIFT LOCK key, toggling the MOS shift-lock state.
     *
     * See `tapCapsLock` for the polling, running-mode precondition, and
     * timeout semantics.
     */
    async tapShiftLock(timeoutCycles: number = LOCK_TAP_TIMEOUT_CYCLES): Promise<void> {
        await this._tapLockKey(
            SHIFT_LOCK_KEY[0],
            SHIFT_LOCK_KEY[1],
            "SHIFT LOCK",
            timeoutCycles,
        );
    }

    /**
     * Run `body` with CAPS LOCK and SHIFT LOCK disabled, then restore
     * the original lock state.
     *
     * The BBC Micro boots with CAPS LOCK on, so without this guard
     * `keyboard.type("hello")` echoes as "HELLO" on screen, and
     * `keyboard.type("HELLO")` (which presses SHIFT for each letter)
     * echoes as "hello". Wrapping typing in this helper makes the screen
     * match the source string.
     *
     * The emulator must be running on entry, both so the entry tap
     * registers and so any queued typing inside the block can drain.
     * Pending typing is drained via `waitForTyping()` before the locks
     * are restored, so any in-flight characters are typed under the
     * same lock state that produced them.
     *
     * @throws Error if the emulator is not running on entry.
     *
     * @example
     * await bbc.debugger.run();
     * await bbc.keyboard.withTextInput(async () => {
     *     await bbc.keyboard.type("Hello, World!\r");
     *     await bbc.runUntilOrTimeout(predicate, 2.0);
     * });
     */
    async withTextInput<T>(body: () => Promise<T>): Promise<T> {
        const client = this._requireClient("withTextInput()");
        if (!(await client.debugger.isRunning())) {
            throw new Error(
                "withTextInput() requires a running emulator. Call " +
                "bbc.debugger.run() before invoking it.",
            );
        }

        // One latch query gives the exact starting state at any speed.
        const initial = await this.getLockState();

        if (initial.capsLock) {
            await this.tapCapsLock();
        }
        if (initial.shiftLock) {
            await this.tapShiftLock();
        }

        try {
            return await body();
        } finally {
            // Drain pending typing under the disabled-locks state so the
            // restoring tap doesn't change the case of in-flight characters.
            await this.waitForTyping();

            const current = await this.getLockState();
            if (current.capsLock !== initial.capsLock) {
                await this.tapCapsLock();
            }
            if (current.shiftLock !== initial.shiftLock) {
                await this.tapShiftLock();
            }
        }
    }

    private async _lockIsOn(which: "CAPS LOCK" | "SHIFT LOCK"): Promise<boolean> {
        const state = await this.getLockState();
        return which === "CAPS LOCK" ? state.capsLock : state.shiftLock;
    }

    private async _tapLockKey(
        row: number,
        column: number,
        which: "CAPS LOCK" | "SHIFT LOCK",
        timeoutCycles: number,
    ): Promise<void> {
        const client = this._requireClient("tap");
        const dbg = client.debugger;
        if (!(await dbg.isRunning())) {
            throw new Error(
                "Lock key tap requires a running emulator: the MOS " +
                "keyboard scan is interrupt-driven and only progresses " +
                "while the CPU is executing. Call bbc.debugger.run() " +
                "before tapping.",
            );
        }

        // The logical latch is exact and immediate, so the starting state is
        // read in a single query -- no waiting for a filter to settle.
        const initial = await this._lockIsOn(which);
        await this.matrixDown(row, column);
        await this._waitEmulatedCycles(LOCK_TAP_HOLD_CYCLES);
        await this.matrixUp(row, column);

        const startCycles = (await dbg.getState()).cycleCount;
        while ((await dbg.getState()).cycleCount - startCycles < timeoutCycles) {
            if (await this._lockIsOn(which) !== initial) {
                // MOS may still be inside its keyboard-handler IRQ
                // routine, processing the press. Wait for the release
                // to be scanned and the key-debounce window to clear
                // before returning, otherwise an immediately-following
                // tap of the same key looks like a continuous press
                // and never re-triggers the toggle.
                await this._waitEmulatedCycles(LOCK_TAP_RELEASE_SETTLE_CYCLES);
                return;
            }
            await this._waitEmulatedCycles(LOCK_TAP_POLL_CHUNK_CYCLES);
        }
        if (await this._lockIsOn(which) !== initial) {
            await this._waitEmulatedCycles(LOCK_TAP_RELEASE_SETTLE_CYCLES);
            return;
        }
        throw new Error(
            `Lock key tap not registered: the ${which} latch did not flip ` +
            `within ${timeoutCycles} emulator cycles. Is the emulator ` +
            `running fast enough to advance the MOS keyboard scan?`,
        );
    }

    private async _waitEmulatedCycles(cycles: number): Promise<void> {
        const client = this._requireClient("tap");
        const dbg = client.debugger;
        const target = (await dbg.getState()).cycleCount + cycles;
        // 2 MHz host clock; sleep approximately the equivalent wall-clock
        // time, then top up by polling. Keep delays small enough not to
        // overshoot but large enough to reduce gRPC chatter.
        const sleepMs = Math.max(5, Math.floor(cycles / 2_000_000 * 1000 / 2));
        while ((await dbg.getState()).cycleCount < target) {
            await new Promise(r => setTimeout(r, sleepMs));
        }
    }

    private _requireClient(feature: string): Beebium {
        if (this._client === undefined) {
            throw new Error(
                `${feature} requires Keyboard to be constructed with a client ` +
                "reference (use bbc.keyboard rather than constructing Keyboard " +
                "directly).",
            );
        }
        return this._client;
    }

    // =========================================================================
    // Modifier keys
    // =========================================================================

    /** Press the SHIFT key. */
    async shiftDown(): Promise<void> {
        await this.matrixDown(...SHIFT_KEY);
    }

    /** Release the SHIFT key. */
    async shiftUp(): Promise<void> {
        await this.matrixUp(...SHIFT_KEY);
    }

    /** Press the CTRL key. */
    async ctrlDown(): Promise<void> {
        await this.matrixDown(...CTRL_KEY);
    }

    /** Release the CTRL key. */
    async ctrlUp(): Promise<void> {
        await this.matrixUp(...CTRL_KEY);
    }

    // =========================================================================
    // Matrix-level input
    // =========================================================================

    /**
     * Press a key by BBC keyboard matrix position.
     *
     * @param row - The keyboard matrix row (0-7).
     * @param column - The keyboard matrix column (0-9).
     * @returns True if accepted by server.
     */
    async matrixDown(row: number, column: number): Promise<boolean> {
        const response = await promisify<{ ikNumber: number }, KeyResponse>(
            this._stub as unknown as Record<string, Function>,
            "keyDown",
            { ikNumber: (row << 4) | column },
        );
        return response.accepted;
    }

    /**
     * Release a key by BBC keyboard matrix position.
     *
     * @param row - The keyboard matrix row (0-7).
     * @param column - The keyboard matrix column (0-9).
     * @returns True if accepted by server.
     */
    async matrixUp(row: number, column: number): Promise<boolean> {
        const response = await promisify<{ ikNumber: number }, KeyResponse>(
            this._stub as unknown as Record<string, Function>,
            "keyUp",
            { ikNumber: (row << 4) | column },
        );
        return response.accepted;
    }

    /**
     * Get current keyboard state (pressed keys bitmap).
     *
     * @returns The current keyboard state.
     */
    async getState(): Promise<KeyboardState> {
        const response = await promisify<Record<string, never>, KeyboardStateProto>(
            this._stub as unknown as Record<string, Function>,
            "getState",
            {},
        );
        return createKeyboardState([...response.pressedRows]);
    }

    // =========================================================================
    // Type-ahead status
    // =========================================================================

    /**
     * Get type-ahead queue status.
     *
     * @returns Object with idle, pendingCharacters, and stringsQueued.
     */
    async typingStatus(): Promise<{ idle: boolean; pendingCharacters: number; stringsQueued: number }> {
        const response = await promisify<Record<string, never>, TypingStatus>(
            this._stub as unknown as Record<string, Function>,
            "getTypingStatus",
            {},
        );
        return {
            idle: response.idle,
            pendingCharacters: response.pendingCharacters,
            stringsQueued: response.stringsQueued,
        };
    }

    /**
     * Clear the type-ahead queue.
     *
     * @returns Number of characters that were cleared.
     */
    async clearTyping(): Promise<number> {
        const response = await promisify<Record<string, never>, ClearTypingResponse>(
            this._stub as unknown as Record<string, Function>,
            "clearTyping",
            {},
        );
        return response.charactersCleared;
    }

    /**
     * Wait until the type-ahead queue is empty.
     *
     * @param pollIntervalMs - Milliseconds between status checks (default 10).
     */
    async waitForTyping(pollIntervalMs: number = 10): Promise<void> {
        while (true) {
            const status = await this.typingStatus();
            if (status.idle) {
                return;
            }
            await new Promise(r => setTimeout(r, pollIntervalMs));
        }
    }

    // =========================================================================
    // Character-to-key mapping (server-side canonical mapping)
    // =========================================================================

    /**
     * Get the BBC key mapping for a character from the server.
     *
     * This queries the canonical mapping table maintained by the server.
     *
     * @param char - A single character.
     * @returns Object with ikNumber, needsShift, and name, or undefined if unmappable.
     */
    async getKeyMapping(char: string): Promise<{ ikNumber: number; needsShift: boolean; name: string } | undefined> {
        const response = await promisify<{ character: string }, KeyMappingEntry>(
            this._stub as unknown as Record<string, Function>,
            "getKeyMapping",
            { character: char },
        );
        if (!response.found) {
            return undefined;
        }
        return {
            ikNumber: response.ikNumber,
            needsShift: response.needsShift,
            name: response.name,
        };
    }

    /**
     * Get the complete key mapping table from the server.
     *
     * @returns Array of mapping entries with character, ikNumber, needsShift, and name.
     *          Character is empty string for named-only keys (function keys, etc.).
     */
    async getAllMappings(): Promise<Array<{ character: string; ikNumber: number; needsShift: boolean; name: string }>> {
        const response = await promisify<Record<string, never>, AllKeyMappingsResponse>(
            this._stub as unknown as Record<string, Function>,
            "getAllKeyMappings",
            {},
        );
        return response.mappings.map(entry => ({
            character: entry.character,
            ikNumber: entry.ikNumber,
            needsShift: entry.needsShift,
            name: entry.name,
        }));
    }

    /**
     * Check if all characters in text can be typed (using server mapping).
     *
     * @param text - The text to check.
     * @returns True if all characters are typeable according to the server.
     */
    async isTypeableOnServer(text: string): Promise<boolean> {
        for (const char of text) {
            const mapping = await this.getKeyMapping(char);
            if (mapping === undefined) {
                return false;
            }
        }
        return true;
    }

    // =========================================================================
    // Break key operations
    // =========================================================================
    // The Break key is NOT part of the keyboard matrix. It is directly
    // connected to the reset circuit (IC16 NE555 timer) via pin 4.
    //
    // While Break is held: CPU is halted (reset line held low)
    // On Break release: Soft reset sequence begins
    //
    // Note: Hardware always does a soft reset. MOS checks if Ctrl is held
    // during its reset sequence to decide between warm/cold reset behavior.

    /**
     * Hold the Break key (halt the CPU).
     *
     * This asserts the reset line, halting the CPU. The CPU remains
     * halted until breakUp() is called.
     *
     * @returns True if successful.
     */
    async breakDown(): Promise<boolean> {
        const response = await promisify<Record<string, never>, BreakDownResponse>(
            this._stub as unknown as Record<string, Function>,
            "breakDown",
            {},
        );
        return response.success;
    }

    /**
     * Release the Break key (begin soft reset sequence).
     *
     * This releases the reset line. The CPU will execute the 7-cycle
     * reset sequence and then begin execution from the reset vector.
     *
     * MOS checks if Ctrl is held at this moment to determine reset type:
     * - If Ctrl pressed: MOS performs "hard reset" (clears VIA config)
     * - If Ctrl not pressed: MOS performs "warm reset" (preserves state)
     *
     * @returns True if successful.
     */
    async breakUp(): Promise<boolean> {
        const response = await promisify<Record<string, never>, BreakUpResponse>(
            this._stub as unknown as Record<string, Function>,
            "breakUp",
            {},
        );
        return response.success;
    }

    /**
     * Check if the Break key is currently held.
     *
     * @returns True if Break is currently held (CPU halted).
     */
    async isBreakHeld(): Promise<boolean> {
        const response = await promisify<Record<string, never>, BreakKeyState>(
            this._stub as unknown as Record<string, Function>,
            "getBreakState",
            {},
        );
        return response.isHeld;
    }

    /**
     * Press and release the Break key (perform soft reset).
     *
     * This is a convenience method that calls breakDown(), waits
     * briefly, then calls breakUp().
     *
     * @param holdTimeMs - How long to hold Break in milliseconds (default 20).
     * @returns True if both operations succeeded.
     */
    async pressBreak(holdTimeMs: number = 20): Promise<boolean> {
        const downOk = await this.breakDown();
        await new Promise(r => setTimeout(r, holdTimeMs));
        const upOk = await this.breakUp();
        return downOk && upOk;
    }

    /**
     * Press Ctrl-Break (trigger MOS hard reset).
     *
     * This holds Ctrl while pressing Break, which causes MOS to
     * detect a "hard reset" and perform full reinitialization.
     *
     * Note: The hardware always performs a soft reset. MOS checks
     * the keyboard matrix during its reset routine and clears the
     * VIA configuration if Ctrl is held, simulating a hard reset.
     *
     * @param holdTimeMs - How long to hold Break in milliseconds (default 20).
     * @returns True if all operations succeeded.
     */
    async ctrlBreak(holdTimeMs: number = 20): Promise<boolean> {
        await this.ctrlDown();
        await new Promise(r => setTimeout(r, 10)); // Brief delay to ensure Ctrl is registered
        const downOk = await this.breakDown();
        await new Promise(r => setTimeout(r, holdTimeMs));
        const upOk = await this.breakUp();
        await new Promise(r => setTimeout(r, 10)); // Brief delay before releasing Ctrl
        await this.ctrlUp();
        return downOk && upOk;
    }

    // =========================================================================
    // Keyboard links (DIP switches)
    // =========================================================================
    // The BBC Micro has 8 keyboard links (row 0, columns 2-9) that form a
    // startup options byte. Active-low: bit SET = link broken (open),
    // bit CLEAR = link made.
    //
    // Bit layout:
    //   Bits 0-2: Screen mode (XOR'd with 7 by MOS, so 0x07 = Mode 7)
    //   Bit 3: SHIFT-BREAK action (1 = normal, 0 = reversed/auto-boot)
    //   Bits 4-7: ROM-dependent (disc timing, filing system, etc.)
    //
    // Default value 0xFF (all links broken) = Mode 7, normal SHIFT-BREAK.

    /**
     * Get the raw keyboard links byte (8 bits).
     *
     * @returns The current 8-bit links value (0-255).
     */
    async getLinks(): Promise<number> {
        const response = await promisify<Record<string, never>, LinksState>(
            this._stub as unknown as Record<string, Function>,
            "getLinks",
            {},
        );
        return response.value;
    }

    /**
     * Set the raw keyboard links byte (8 bits).
     *
     * @param value - The 8-bit links value (0-255).
     * @returns True if successful.
     * @throws Error if value is not in range 0-255 or server rejects.
     */
    async setLinks(value: number): Promise<boolean> {
        if (value < 0 || value > 255) {
            throw new Error("Links value must be 0-255");
        }
        const response = await promisify<{ value: number }, SetLinksResponse>(
            this._stub as unknown as Record<string, Function>,
            "setLinks",
            { value },
        );
        if (!response.success) {
            throw new Error(response.error);
        }
        return true;
    }

    /**
     * Get the startup screen mode (0-7).
     *
     * @returns The configured startup screen mode.
     */
    async getStartupScreenMode(): Promise<number> {
        const response = await promisify<Record<string, never>, StartupScreenModeState>(
            this._stub as unknown as Record<string, Function>,
            "getStartupScreenMode",
            {},
        );
        return response.mode;
    }

    /**
     * Set the startup screen mode (0-7).
     *
     * @param mode - The screen mode (0-7).
     * @returns True if successful.
     * @throws Error if mode is not in range 0-7 or server rejects.
     */
    async setStartupScreenMode(mode: number): Promise<boolean> {
        if (mode < 0 || mode > 7) {
            throw new Error("Mode must be 0-7");
        }
        const response = await promisify<{ mode: number }, SetStartupScreenModeResponse>(
            this._stub as unknown as Record<string, Function>,
            "setStartupScreenMode",
            { mode },
        );
        if (!response.success) {
            throw new Error(response.error);
        }
        return true;
    }

    /**
     * Check if auto-boot on SHIFT-BREAK is enabled.
     *
     * @returns True if SHIFT-BREAK triggers auto-boot.
     */
    async getStartupAutoBoot(): Promise<boolean> {
        const response = await promisify<Record<string, never>, StartupAutoBootState>(
            this._stub as unknown as Record<string, Function>,
            "getStartupAutoBoot",
            {},
        );
        return response.enabled;
    }

    /**
     * Enable or disable auto-boot on SHIFT-BREAK.
     *
     * When enabled, SHIFT-BREAK causes MOS to load and run !Boot.
     *
     * @param enabled - True to enable auto-boot.
     * @returns True if successful.
     */
    async setStartupAutoBoot(enabled: boolean): Promise<boolean> {
        const response = await promisify<{ enabled: boolean }, SetStartupAutoBootResponse>(
            this._stub as unknown as Record<string, Function>,
            "setStartupAutoBoot",
            { enabled },
        );
        return response.success;
    }
}
