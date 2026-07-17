import { describe, it, expect, vi } from "vitest";
import { Keyboard, SHIFT_KEY, CTRL_KEY } from "../src/keyboard.js";

function createMockStub(methods: Record<string, (req: any) => any>) {
    const stub: Record<string, any> = {};
    for (const [name, handler] of Object.entries(methods)) {
        stub[name] = vi.fn((request: any, callback: Function) => {
            try {
                const response = handler(request);
                callback(null, response);
            } catch (err) {
                callback(err);
            }
        });
    }
    return stub;
}

function makeKeyboardStub() {
    return createMockStub({
        typeQuickly: (req: any) => ({
            accepted: true,
            error: "",
            pendingCharacters: req.text.length,
        }),
        keyDown: () => ({ accepted: true }),
        keyUp: () => ({ accepted: true }),
        getState: () => ({ pressedRows: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0] }),
        getTypingStatus: () => ({ idle: true, pendingCharacters: 0, stringsQueued: 0 }),
        clearTyping: () => ({ charactersCleared: 0 }),
        breakDown: () => ({ success: true }),
        breakUp: () => ({ success: true }),
        getBreakState: () => ({ isHeld: false }),
        getLockState: () => ({ capsLockOn: true, shiftLockOn: false }),
        getLinks: () => ({ value: 0xFF }),
        setLinks: () => ({ success: true, error: "" }),
        getStartupScreenMode: () => ({ mode: 7 }),
        setStartupScreenMode: () => ({ success: true, error: "" }),
        getStartupAutoBoot: () => ({ enabled: false }),
        setStartupAutoBoot: () => ({ success: true }),
        getKeyMapping: () => ({ found: true, ikNumber: 0x41, needsShift: false, name: "A" }),
        getAllKeyMappings: () => ({ mappings: [] }),
    });
}

describe("Keyboard", () => {
    describe("type", () => {
        it("sends TypeQuicklyRequest with the text and no timing knob", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const pending = await kb.type("PRINT 42\r");
            expect(pending).toBe(9);
            expect(stub.typeQuickly).toHaveBeenCalledWith(
                { text: "PRINT 42\r" },
                expect.any(Function),
            );
        });

        it("throws when not accepted", async () => {
            const stub = createMockStub({
                typeQuickly: () => ({
                    accepted: false,
                    error: "unmappable character",
                    pendingCharacters: 0,
                }),
            });
            const kb = new Keyboard(stub as any);
            await expect(kb.type("\x00")).rejects.toThrow("Text rejected: unmappable character");
        });
    });

    describe("matrixDown", () => {
        it("sends KeyRequest with correct ik_number encoding (row<<4|col)", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.matrixDown(4, 1); // 'A' key
            expect(stub.keyDown).toHaveBeenCalledWith(
                { ikNumber: (4 << 4) | 1 },
                expect.any(Function),
            );
        });

        it("encodes row=7 col=0 correctly (ESC)", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.matrixDown(7, 0);
            expect(stub.keyDown).toHaveBeenCalledWith(
                { ikNumber: 0x70 },
                expect.any(Function),
            );
        });
    });

    describe("matrixUp", () => {
        it("sends KeyRequest with correct ik_number encoding", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.matrixUp(4, 1);
            expect(stub.keyUp).toHaveBeenCalledWith(
                { ikNumber: (4 << 4) | 1 },
                expect.any(Function),
            );
        });
    });

    describe("keyDown", () => {
        it("with lowercase letter does NOT press shift", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.keyDown("a");
            // 'a' is at row=4, col=1, no shift
            const calls = stub.keyDown.mock.calls;
            // Should only have one keyDown call (the 'a' key itself)
            expect(calls).toHaveLength(1);
            expect(calls[0]![0]).toEqual({ ikNumber: (4 << 4) | 1 });
        });

        it("with uppercase letter DOES press shift first", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.keyDown("A");
            // Should have two keyDown calls: SHIFT then 'A'
            const calls = stub.keyDown.mock.calls;
            expect(calls).toHaveLength(2);
            // First call: SHIFT (row=0, col=0)
            expect(calls[0]![0]).toEqual({ ikNumber: (SHIFT_KEY[0] << 4) | SHIFT_KEY[1] });
            // Second call: A key (row=4, col=1)
            expect(calls[1]![0]).toEqual({ ikNumber: (4 << 4) | 1 });
        });

        it("returns true for mapped characters", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            expect(await kb.keyDown("z")).toBe(true);
        });

        it("returns false for unmapped characters", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            // Some obscure Unicode character not in BBC mapping
            expect(await kb.keyDown("\u00E9")).toBe(false);
        });
    });

    describe("keyUp", () => {
        it("with uppercase releases key then shift (if no other shifted keys held)", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            // First press A (which will press shift)
            await kb.keyDown("A");
            stub.keyDown.mockClear();
            stub.keyUp.mockClear();

            // Now release A
            await kb.keyUp("A");
            const calls = stub.keyUp.mock.calls;
            // Should release the key, then shift
            expect(calls).toHaveLength(2);
            // First: release A (row=4, col=1)
            expect(calls[0]![0]).toEqual({ ikNumber: (4 << 4) | 1 });
            // Second: release SHIFT (row=0, col=0)
            expect(calls[1]![0]).toEqual({ ikNumber: (SHIFT_KEY[0] << 4) | SHIFT_KEY[1] });
        });

        it("does not release shift if other shifted keys still held", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            // Press two shifted keys
            await kb.keyDown("A");
            await kb.keyDown("B");
            stub.keyUp.mockClear();

            // Release A but B is still held
            await kb.keyUp("A");
            const calls = stub.keyUp.mock.calls;
            // Only the A key release, not shift
            expect(calls).toHaveLength(1);
            expect(calls[0]![0]).toEqual({ ikNumber: (4 << 4) | 1 });
        });

        it("with lowercase does not touch shift", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.keyDown("a");
            stub.keyUp.mockClear();

            await kb.keyUp("a");
            const calls = stub.keyUp.mock.calls;
            expect(calls).toHaveLength(1);
            expect(calls[0]![0]).toEqual({ ikNumber: (4 << 4) | 1 });
        });
    });

    describe("shiftDown / shiftUp", () => {
        it("shiftDown calls correct matrix position", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.shiftDown();
            expect(stub.keyDown).toHaveBeenCalledWith(
                { ikNumber: (SHIFT_KEY[0] << 4) | SHIFT_KEY[1] },
                expect.any(Function),
            );
        });

        it("shiftUp calls correct matrix position", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.shiftUp();
            expect(stub.keyUp).toHaveBeenCalledWith(
                { ikNumber: (SHIFT_KEY[0] << 4) | SHIFT_KEY[1] },
                expect.any(Function),
            );
        });
    });

    describe("ctrlDown / ctrlUp", () => {
        it("ctrlDown calls correct matrix position", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.ctrlDown();
            expect(stub.keyDown).toHaveBeenCalledWith(
                { ikNumber: (CTRL_KEY[0] << 4) | CTRL_KEY[1] },
                expect.any(Function),
            );
        });

        it("ctrlUp calls correct matrix position", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.ctrlUp();
            expect(stub.keyUp).toHaveBeenCalledWith(
                { ikNumber: (CTRL_KEY[0] << 4) | CTRL_KEY[1] },
                expect.any(Function),
            );
        });
    });

    describe("breakDown / breakUp / isBreakHeld", () => {
        it("breakDown calls correct RPC", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const result = await kb.breakDown();
            expect(result).toBe(true);
            expect(stub.breakDown).toHaveBeenCalledWith({}, expect.any(Function));
        });

        it("breakUp calls correct RPC", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const result = await kb.breakUp();
            expect(result).toBe(true);
            expect(stub.breakUp).toHaveBeenCalledWith({}, expect.any(Function));
        });

        it("isBreakHeld returns the held state", async () => {
            const stub = createMockStub({
                getBreakState: () => ({ isHeld: true }),
            });
            const kb = new Keyboard(stub as any);
            expect(await kb.isBreakHeld()).toBe(true);
        });

        it("isBreakHeld returns false when not held", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            expect(await kb.isBreakHeld()).toBe(false);
        });
    });

    describe("getLinks / setLinks", () => {
        it("getLinks returns the links value", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const links = await kb.getLinks();
            expect(links).toBe(0xFF);
        });

        it("setLinks sends correct request", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const result = await kb.setLinks(0xF8);
            expect(result).toBe(true);
            expect(stub.setLinks).toHaveBeenCalledWith(
                { value: 0xF8 },
                expect.any(Function),
            );
        });

        it("setLinks throws for out-of-range value", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await expect(kb.setLinks(256)).rejects.toThrow("Links value must be 0-255");
            await expect(kb.setLinks(-1)).rejects.toThrow("Links value must be 0-255");
        });

        it("setLinks throws when server rejects", async () => {
            const stub = createMockStub({
                setLinks: () => ({ success: false, error: "invalid value" }),
            });
            const kb = new Keyboard(stub as any);
            await expect(kb.setLinks(0x00)).rejects.toThrow("invalid value");
        });
    });

    describe("getStartupScreenMode / setStartupScreenMode", () => {
        it("getStartupScreenMode returns the mode", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const mode = await kb.getStartupScreenMode();
            expect(mode).toBe(7);
        });

        it("setStartupScreenMode sends correct request", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await kb.setStartupScreenMode(0);
            expect(stub.setStartupScreenMode).toHaveBeenCalledWith(
                { mode: 0 },
                expect.any(Function),
            );
        });

        it("setStartupScreenMode throws for out-of-range mode", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            await expect(kb.setStartupScreenMode(8)).rejects.toThrow("Mode must be 0-7");
            await expect(kb.setStartupScreenMode(-1)).rejects.toThrow("Mode must be 0-7");
        });

        it("setStartupScreenMode throws when server rejects", async () => {
            const stub = createMockStub({
                setStartupScreenMode: () => ({ success: false, error: "not supported" }),
            });
            const kb = new Keyboard(stub as any);
            await expect(kb.setStartupScreenMode(3)).rejects.toThrow("not supported");
        });
    });

    describe("typingStatus", () => {
        it("maps response tuple correctly", async () => {
            const stub = createMockStub({
                getTypingStatus: () => ({
                    idle: false,
                    pendingCharacters: 15,
                    stringsQueued: 3,
                }),
            });
            const kb = new Keyboard(stub as any);
            const status = await kb.typingStatus();
            expect(status).toEqual({
                idle: false,
                pendingCharacters: 15,
                stringsQueued: 3,
            });
        });

        it("maps idle status", async () => {
            const stub = makeKeyboardStub();
            const kb = new Keyboard(stub as any);
            const status = await kb.typingStatus();
            expect(status.idle).toBe(true);
            expect(status.pendingCharacters).toBe(0);
        });
    });

    describe("getLockState", () => {
        it("maps the latch response to a LockState", async () => {
            const stub = createMockStub({
                getLockState: () => ({ capsLockOn: true, shiftLockOn: false }),
            });
            const kb = new Keyboard(stub as any);
            const state = await kb.getLockState();
            expect(state).toEqual({ capsLock: true, shiftLock: false });
        });

        it("reflects both locks engaged", async () => {
            const stub = createMockStub({
                getLockState: () => ({ capsLockOn: true, shiftLockOn: true }),
            });
            const kb = new Keyboard(stub as any);
            const state = await kb.getLockState();
            expect(state).toEqual({ capsLock: true, shiftLock: true });
        });
    });
});
