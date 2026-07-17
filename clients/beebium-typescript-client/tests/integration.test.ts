/**
 * Integration tests for the Beebium TypeScript client.
 *
 * Each test launches a fresh beebium-model-b server so there is no shared
 * emulator state between tests. This makes tests independent, order-
 * insensitive, and easy to debug in isolation.
 */

import { describe, it, expect } from "vitest";
import { Connection } from "../src/connection.js";
import { Debugger } from "../src/debugger.js";
import { CPU } from "../src/cpu.js";
import { Memory } from "../src/memory.js";
import { Keyboard } from "../src/keyboard.js";
import { System, ServerStatus } from "../src/system.js";
import { Via, ViaId } from "../src/via.js";
import { Crtc } from "../src/crtc.js";
import { VideoUla, CTRL_TELETEXT } from "../src/video-ula.js";
import { AddressableLatch } from "../src/latch.js";
import { Sound } from "../src/sound.js";
import { Disc } from "../src/disc.js";
import { Econet } from "../src/econet.js";
import { Tube } from "../src/tube.js";
import { TubeUlaInspection } from "../src/tube-ula.js";
import { withServer } from "./server-harness.js";

// =========================================================================
// System Service
// =========================================================================

describe("Integration: System Service", () => {
    it("should return machine identity with correct model", async () => {
        await withServer(async (conn, server) => {
            const system = new System(conn.systemStub, server.provenanceUuid);
            const identity = await system.getIdentity();
            expect(identity.uuid).toBeTruthy();
            expect(identity.modelType).toMatch(/[Mm]odel.?[Bb]/);
            expect(identity.modelName).toBeTruthy();
        });
    });

    it("should return launch provenance", async () => {
        await withServer(async (conn, server) => {
            const system = new System(conn.systemStub, server.provenanceUuid);
            const provenance = await system.getProvenance();
            expect(provenance.type).toBe("typescript-client");
            expect(provenance.instanceUuid).toBe(server.provenanceUuid);
            expect(provenance.version).toBe("0.1.0");
            expect(provenance.timestamp).toBeGreaterThan(0);
        });
    });

    it("should report clock speed as a number", async () => {
        await withServer(async (conn) => {
            const system = new System(conn.systemStub);
            const hz = await system.getClockSpeedHz();
            expect(typeof hz).toBe("number");
            // May be 2000000 (2 MHz) or 0 if not yet populated by server
            expect(hz).toBeGreaterThanOrEqual(0);
        });
    });

    it("should stream READY as first status event", async () => {
        await withServer(async (conn) => {
            const system = new System(conn.systemStub);
            for await (const event of system.watchStatus()) {
                expect(event.status).toBe(ServerStatus.READY);
                expect(event.message).toBe("Server ready");
                break;
            }
        });
    });

    it("should wait for ready without timeout", async () => {
        await withServer(async (conn) => {
            const system = new System(conn.systemStub);
            const ready = await system.waitForReady(5000);
            expect(ready).toBe(true);
        });
    });
});

// =========================================================================
// Debugger - Execution Control
// =========================================================================

describe("Integration: Debugger", () => {
    it("should report stopped state initially (--wait=api)", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const state = await dbg.getState();
            expect(state.isRunning).toBe(false);
            expect(typeof state.cycleCount).toBe("number");
            expect(typeof state.sequence).toBe("number");
        });
    });

    it("should stop when already stopped", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const state = await dbg.stop();
            expect(state.isRunning).toBe(false);
        });
    });

    it("should step 1 instruction", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const result = await dbg.step(1);
            expect(result.success).toBe(true);
            expect(result.instructionsExecuted).toBe(1);
            expect(result.cyclesExecuted).toBeGreaterThan(0);
        });
    });

    it("should step 100 instructions", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const result = await dbg.step(100);
            expect(result.success).toBe(true);
            expect(result.instructionsExecuted).toBe(100);
            expect(result.cyclesExecuted).toBeGreaterThan(100);
        });
    });

    it("should step by cycles", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const result = await dbg.stepCycles(10);
            expect(result.success).toBe(true);
            expect(result.cyclesExecuted).toBeGreaterThanOrEqual(1);
        });
    });

    it("should run and then stop", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await dbg.run();
            expect(await dbg.isRunning()).toBe(true);
            const state = await dbg.stop();
            expect(state.isRunning).toBe(false);
        });
    });

    it("should reset the machine", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await dbg.step(100); // advance some
            await dbg.reset();
            const state = await dbg.getState();
            expect(state.isRunning).toBe(false);
        });
    });
});

// =========================================================================
// CPU Registers
// =========================================================================

describe("Integration: CPU Registers", () => {
    it("should read all registers in valid ranges", async () => {
        await withServer(async (conn) => {
            const cpu = new CPU(conn.debuggerStub);
            const regs = await cpu.getRegisters();
            expect(regs.a).toBeGreaterThanOrEqual(0);
            expect(regs.a).toBeLessThanOrEqual(255);
            expect(regs.x).toBeGreaterThanOrEqual(0);
            expect(regs.x).toBeLessThanOrEqual(255);
            expect(regs.y).toBeGreaterThanOrEqual(0);
            expect(regs.y).toBeLessThanOrEqual(255);
            expect(regs.sp).toBeGreaterThanOrEqual(0);
            expect(regs.sp).toBeLessThanOrEqual(255);
            expect(regs.pc).toBeGreaterThanOrEqual(0);
            expect(regs.pc).toBeLessThanOrEqual(0xFFFF);
            expect(regs.p).toBeGreaterThanOrEqual(0);
            expect(regs.p).toBeLessThanOrEqual(255);
        });
    });

    it("should set and read back PC", async () => {
        await withServer(async (conn) => {
            const cpu = new CPU(conn.debuggerStub);
            await cpu.setPc(0x1234);
            expect(await cpu.getPc()).toBe(0x1234);
        });
    });

    it("should set and read back A", async () => {
        await withServer(async (conn) => {
            const cpu = new CPU(conn.debuggerStub);
            await cpu.setA(0x42);
            expect(await cpu.getA()).toBe(0x42);
        });
    });

    it("should set multiple registers at once", async () => {
        await withServer(async (conn) => {
            const cpu = new CPU(conn.debuggerStub);
            await cpu.setRegisters({ a: 0x11, x: 0x22, y: 0x33 });
            const regs = await cpu.getRegisters();
            expect(regs.a).toBe(0x11);
            expect(regs.x).toBe(0x22);
            expect(regs.y).toBe(0x33);
        });
    });
});

// =========================================================================
// Memory
// =========================================================================

describe("Integration: Memory", () => {
    it("should peek bytes from ROM area (non-zero content)", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            const data = await mem.address.peek.read(0x8000, 16);
            expect(data.length).toBe(16);
            expect(Array.from(data).some((b) => b !== 0)).toBe(true);
        });
    });

    it("should write a byte and read it back", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            await mem.address.bus.writeByte(0x0400, 0x42);
            expect(await mem.address.peek.readByte(0x0400)).toBe(0x42);
        });
    });

    it("should write and read back multiple bytes", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            const data = Buffer.from([0x01, 0x02, 0x03, 0x04]);
            await mem.address.bus.write(0x0400, data);
            const readBack = await mem.address.peek.read(0x0400, 4);
            expect(Array.from(readBack)).toEqual([0x01, 0x02, 0x03, 0x04]);
        });
    });

    it("should fill a range and verify", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            await mem.address.bus.fill(0x0400, 0x0410, 0xAA);
            const data = await mem.address.peek.read(0x0400, 16);
            for (let i = 0; i < 16; i++) {
                expect(data[i]).toBe(0xAA);
            }
        });
    });

    it("should list memory regions including main_ram", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            const regions = await mem.getRegions();
            expect(regions.length).toBeGreaterThan(0);
            const mainRam = regions.find((r) => r.name === "main_ram");
            expect(mainRam).toBeDefined();
            expect(mainRam!.readable).toBe(true);
            expect(mainRam!.writable).toBe(true);
        });
    });

    it("should return machine type", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            const mt = await mem.getMachineType();
            expect(mt).toMatch(/[Mm]odel.?[Bb]/);
        });
    });

    it("should read via named region", async () => {
        await withServer(async (conn) => {
            const mem = new Memory(conn.debuggerStub);
            await mem.address.bus.writeByte(0x0500, 0x77);
            const byte = await mem.region("main_ram").peek.readByte(0x0500);
            expect(byte).toBe(0x77);
        });
    });
});

// =========================================================================
// Keyboard
// =========================================================================

describe("Integration: Keyboard", () => {
    it("should return keyboard state with 10 rows", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            const state = await kb.getState();
            expect(state.pressedRows.length).toBe(10);
        });
    });

    it("should press and release a matrix key", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            expect(await kb.matrixDown(4, 1)).toBe(true);
            expect(await kb.matrixUp(4, 1)).toBe(true);
        });
    });

    it("should enqueue text via type()", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            const pending = await kb.type("HELLO");
            expect(pending).toBeGreaterThanOrEqual(5);
        });
    });

    it("should report and clear typing status", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            await kb.type("TEST");
            const status = await kb.typingStatus();
            expect(status.pendingCharacters).toBeGreaterThan(0);
            await kb.clearTyping();
            const after = await kb.typingStatus();
            expect(after.pendingCharacters).toBe(0);
        });
    });

    it("should set and read back keyboard links", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            const original = await kb.getLinks();
            await kb.setLinks(0xF8);
            expect(await kb.getLinks()).toBe(0xF8);
            await kb.setLinks(original);
        });
    });

    it("should get startup screen mode in range 0-7", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            const mode = await kb.getStartupScreenMode();
            expect(mode).toBeGreaterThanOrEqual(0);
            expect(mode).toBeLessThanOrEqual(7);
        });
    });

    it("should handle break key down and up", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            expect(await kb.breakDown()).toBe(true);
            expect(await kb.isBreakHeld()).toBe(true);
            expect(await kb.breakUp()).toBe(true);
        });
    });

    it("should return key mapping for a character", async () => {
        await withServer(async (conn) => {
            const kb = new Keyboard(conn.keyboardStub);
            const mapping = await kb.getKeyMapping("A");
            expect(mapping).toBeDefined();
            expect(mapping!.needsShift).toBe(true);
        });
    });
});

// =========================================================================
// Device Inspection (VIA, CRTC, Video ULA, Latch, Sound)
// =========================================================================

describe("Integration: System VIA", () => {
    it("should return valid register values", async () => {
        await withServer(async (conn) => {
            const via = new Via(conn.deviceInspectionStub, ViaId.SYSTEM);
            const state = await via.getState();
            expect(typeof state.ora).toBe("number");
            expect(typeof state.orb).toBe("number");
            expect(typeof state.ddra).toBe("number");
            expect(typeof state.ddrb).toBe("number");
            expect(typeof state.acr).toBe("number");
            expect(typeof state.ifr).toBe("number");
            expect(typeof state.ier).toBe("number");
            expect(state.t1c).toBeGreaterThanOrEqual(0);
            expect(state.t1c).toBeLessThanOrEqual(0xFFFF);
            expect(state.t1l).toBeGreaterThanOrEqual(0);
            expect(state.t1l).toBeLessThanOrEqual(0xFFFF);
            expect(typeof state.ca1).toBe("boolean");
        });
    });
});

describe("Integration: User VIA", () => {
    it("should return valid register values", async () => {
        await withServer(async (conn) => {
            const via = new Via(conn.deviceInspectionStub, ViaId.USER);
            const state = await via.getState();
            expect(typeof state.ora).toBe("number");
            expect(typeof state.t1Pending).toBe("boolean");
            expect(typeof state.t2Pending).toBe("boolean");
        });
    });
});

describe("Integration: CRTC", () => {
    it("should return 18 registers", async () => {
        await withServer(async (conn) => {
            const crtc = new Crtc(conn.deviceInspectionStub);
            const state = await crtc.getState();
            expect(state.registers.length).toBe(18);
            expect(typeof state.inHsync).toBe("boolean");
            expect(typeof state.inVsync).toBe("boolean");
            expect(typeof state.displayEnabled).toBe("boolean");
        });
    });

    it("should have non-zero R0 after MOS boot", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const crtc = new Crtc(conn.deviceInspectionStub);
            await dbg.step(500000);
            const state = await crtc.getState();
            expect(state.registers[0]).toBeGreaterThan(0);
        });
    });
});

describe("Integration: Video ULA", () => {
    it("should return control register and 16-entry palette", async () => {
        await withServer(async (conn) => {
            const ula = new VideoUla(conn.deviceInspectionStub);
            const state = await ula.getState();
            expect(typeof state.control).toBe("number");
            expect(state.palette.length).toBe(16);
        });
    });

    it("should have teletext bit set after MOS boot (Mode 7)", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const ula = new VideoUla(conn.deviceInspectionStub);
            await dbg.step(500000);
            const state = await ula.getState();
            expect(state.control & CTRL_TELETEXT).toBe(CTRL_TELETEXT);
        });
    });
});

describe("Integration: Addressable Latch", () => {
    it("should return decoded fields", async () => {
        await withServer(async (conn) => {
            const latch = new AddressableLatch(conn.deviceInspectionStub);
            const state = await latch.getState();
            expect(typeof state.value).toBe("number");
            expect(typeof state.soundWriteEnable).toBe("boolean");
            expect(typeof state.capsLockLed).toBe("boolean");
            expect(typeof state.shiftLockLed).toBe("boolean");
        });
    });
});

describe("Integration: Sound", () => {
    it("should return 4 channels with correct names", async () => {
        await withServer(async (conn) => {
            const snd = new Sound(conn.deviceInspectionStub);
            const state = await snd.getState();
            expect(state.channels.length).toBe(4);
            expect(typeof state.latchedRegister).toBe("number");
            expect(state.channels[0]!.channelId).toBe(0);
            expect(state.channels[3]!.channelName).toMatch(/[Nn]oise/);
        });
    });
});

// =========================================================================
// Disc, Econet, Tube, Tube ULA
// =========================================================================

describe("Integration: Disc Controller", () => {
    it("should return disc controller status with drives array", async () => {
        await withServer(async (conn) => {
            const disc = new Disc(conn.discStub);
            const status = await disc.getStatus();
            expect(typeof status.hasDiscController).toBe("boolean");
            expect(typeof status.isSocketed).toBe("boolean");
            expect(Array.isArray(status.drives)).toBe(true);
        });
    });
});

describe("Integration: Econet", () => {
    it("should return Econet status", async () => {
        await withServer(async (conn) => {
            const econet = new Econet(conn.econetStub);
            const status = await econet.getStatus();
            expect(typeof status.hasEconetSocket).toBe("boolean");
            expect(typeof status.enabled).toBe("boolean");
        });
    });
});

describe("Integration: Tube", () => {
    it("should return Tube status", async () => {
        await withServer(async (conn) => {
            const tube = new Tube(conn.tubeStub);
            const status = await tube.getStatus();
            expect(typeof status.hasTubeSocket).toBe("boolean");
            expect(typeof status.enabled).toBe("boolean");
            expect(typeof status.parasiteConnected).toBe("boolean");
        });
    });
});

describe("Integration: Tube ULA", () => {
    it("should return Tube ULA state", async () => {
        await withServer(async (conn) => {
            const tubeUla = new TubeUlaInspection(conn.deviceInspectionStub);
            const state = await tubeUla.getState();
            expect(typeof state.enabled).toBe("boolean");
        });
    });
});

// =========================================================================
// Breakpoints
// =========================================================================

describe("Integration: Breakpoints", () => {
    it("should add, list, and remove a breakpoint", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const bpId = await dbg.addBreakpoint(0xD9CD);
            expect(typeof bpId).toBe("number");

            const bps = await dbg.listBreakpoints();
            expect(bps.some((bp) => bp.id === bpId)).toBe(true);
            expect(bps.find((bp) => bp.id === bpId)!.startAddress).toBe(0xD9CD);

            expect(await dbg.removeBreakpoint(bpId)).toBe(true);
            const after = await dbg.listBreakpoints();
            expect(after.some((bp) => bp.id === bpId)).toBe(false);
        });
    });

    it("should clear all breakpoints", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            await dbg.addBreakpoint(0x1000);
            await dbg.addBreakpoint(0x2000);
            const cleared = await dbg.clearBreakpoints();
            expect(cleared).toBeGreaterThanOrEqual(2);
            expect((await dbg.listBreakpoints()).length).toBe(0);
        });
    });

    it("should run until a breakpoint address", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const cpu = new CPU(conn.debuggerStub);
            const mem = new Memory(conn.debuggerStub);

            // SEI; NOP; NOP; NOP(bp here); JMP $0401
            await mem.address.bus.write(
                0x0400,
                Buffer.from([0x78, 0xEA, 0xEA, 0xEA, 0x4C, 0x01, 0x04]),
            );
            await cpu.setPc(0x0400);
            // Step past SEI so interrupts are disabled before free-run
            await dbg.step(1);
            const event = await dbg.runUntil(0x0403);
            expect(event.state.isRunning).toBe(false);
            expect(await cpu.getPc()).toBe(0x0403);
        });
    });
});

// =========================================================================
// End-to-end workflows
// =========================================================================

describe("Integration: Workflows", () => {
    it("should reach ROM area after reset and 1000 steps", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const cpu = new CPU(conn.debuggerStub);
            await dbg.reset();
            await dbg.step(1000);
            expect(await cpu.getPc()).toBeGreaterThanOrEqual(0x8000);
        });
    });

    it("should execute a small program in RAM", async () => {
        await withServer(async (conn) => {
            const dbg = new Debugger(conn.debuggerStub);
            const cpu = new CPU(conn.debuggerStub);
            const mem = new Memory(conn.debuggerStub);

            // SEI; LDA #$42; STA $70
            await mem.address.bus.write(0x0400, Buffer.from([0x78, 0xA9, 0x42, 0x85, 0x70]));
            await mem.address.bus.writeByte(0x0070, 0x00);
            await cpu.setPc(0x0400);
            await dbg.step(3);

            expect(await cpu.getA()).toBe(0x42);
            expect(await mem.address.peek.readByte(0x0070)).toBe(0x42);
            // PC should be past the STA instruction (exact value depends on
            // the 6502's PC representation -- it points to the next fetch byte)
            expect(await cpu.getPc()).toBeGreaterThanOrEqual(0x0405);
        });
    });
});
