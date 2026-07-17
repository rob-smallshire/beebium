import { describe, it, expect, vi } from "vitest";
import { Crtc } from "../src/crtc.js";
import { VideoUla } from "../src/video-ula.js";
import { AddressableLatch } from "../src/latch.js";
import { Sound } from "../src/sound.js";
import { TubeUlaInspection } from "../src/tube-ula.js";

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

describe("Crtc", () => {
    describe("getState", () => {
        it("maps registers array and all state fields", async () => {
            const registers = [
                63, 40, 49, 0x28, 38, 0, 25, 30,
                0, 7, 0x67, 8, 0x20, 0x00, 0, 0,
                0, 0,
            ];
            const stub = createMockStub({
                getCrtcState: () => ({
                    registers,
                    addressRegister: 12,
                    column: 15,
                    row: 3,
                    raster: 5,
                    charAddr: 0x2050,
                    screenStart: 0x2000,
                    cursorPosition: 0x2040,
                    inHsync: false,
                    inVsync: true,
                    displayEnabled: true,
                }),
            });
            const crtc = new Crtc(stub as any);
            const state = await crtc.getState();
            expect(state.registers).toEqual(registers);
            expect(state.addressRegister).toBe(12);
            expect(state.column).toBe(15);
            expect(state.row).toBe(3);
            expect(state.raster).toBe(5);
            expect(state.charAddr).toBe(0x2050);
            expect(state.screenStart).toBe(0x2000);
            expect(state.cursorPosition).toBe(0x2040);
            expect(state.inHsync).toBe(false);
            expect(state.inVsync).toBe(true);
            expect(state.displayEnabled).toBe(true);
        });

        it("copies registers array (not by reference)", async () => {
            const originalRegs = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17];
            const stub = createMockStub({
                getCrtcState: () => ({
                    registers: originalRegs,
                    addressRegister: 0,
                    column: 0,
                    row: 0,
                    raster: 0,
                    charAddr: 0,
                    screenStart: 0,
                    cursorPosition: 0,
                    inHsync: false,
                    inVsync: false,
                    displayEnabled: false,
                }),
            });
            const crtc = new Crtc(stub as any);
            const state = await crtc.getState();
            // Mutating the returned registers should not affect the original
            state.registers[0] = 999;
            expect(originalRegs[0]).toBe(0);
        });
    });
});

describe("VideoUla", () => {
    describe("getState", () => {
        it("maps control and palette", async () => {
            const palette = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
            const stub = createMockStub({
                getVideoUlaState: () => ({
                    control: 0x9C,
                    palette,
                }),
            });
            const ula = new VideoUla(stub as any);
            const state = await ula.getState();
            expect(state.control).toBe(0x9C);
            expect(state.palette).toEqual(palette);
            expect(state.palette).toHaveLength(16);
        });

        it("copies palette array", async () => {
            const originalPalette = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
            const stub = createMockStub({
                getVideoUlaState: () => ({
                    control: 0,
                    palette: originalPalette,
                }),
            });
            const ula = new VideoUla(stub as any);
            const state = await ula.getState();
            state.palette[0] = 99;
            expect(originalPalette[0]).toBe(0);
        });
    });
});

describe("AddressableLatch", () => {
    describe("getState", () => {
        it("maps all decoded fields", async () => {
            const stub = createMockStub({
                getAddressableLatchState: () => ({
                    value: 0xCF,
                    screenBase: 3,
                    soundWriteEnable: true,
                    speechRead: true,
                    speechWrite: false,
                    keyboardWrite: true,
                    capsLockLed: true,
                    shiftLockLed: false,
                }),
            });
            const latch = new AddressableLatch(stub as any);
            const state = await latch.getState();
            expect(state.value).toBe(0xCF);
            expect(state.screenBase).toBe(3);
            expect(state.soundWriteEnable).toBe(true);
            expect(state.speechRead).toBe(true);
            expect(state.speechWrite).toBe(false);
            expect(state.keyboardWrite).toBe(true);
            expect(state.capsLockLed).toBe(true);
            expect(state.shiftLockLed).toBe(false);
        });
    });
});

describe("Sound", () => {
    describe("getState", () => {
        it("maps channels array and latchedRegister", async () => {
            const channels = [
                {
                    channelId: 0,
                    channelName: "Tone0",
                    frequencyDivider: 256,
                    counter: 128,
                    outputBit: true,
                    volume: 5,
                    frequencyHz: 488.28,
                    noiseRate: 0,
                    whiteNoise: false,
                    lfsrState: 0,
                },
                {
                    channelId: 1,
                    channelName: "Tone1",
                    frequencyDivider: 512,
                    counter: 0,
                    outputBit: false,
                    volume: 15,
                    frequencyHz: 244.14,
                    noiseRate: 0,
                    whiteNoise: false,
                    lfsrState: 0,
                },
                {
                    channelId: 2,
                    channelName: "Tone2",
                    frequencyDivider: 0,
                    counter: 0,
                    outputBit: true,
                    volume: 15,
                    frequencyHz: 0,
                    noiseRate: 0,
                    whiteNoise: false,
                    lfsrState: 0,
                },
                {
                    channelId: 3,
                    channelName: "Noise",
                    frequencyDivider: 0,
                    counter: 0,
                    outputBit: false,
                    volume: 15,
                    frequencyHz: 0,
                    noiseRate: 2,
                    whiteNoise: true,
                    lfsrState: 0x4000,
                },
            ];
            const stub = createMockStub({
                getSoundGeneratorState: () => ({
                    channels,
                    latchedRegister: 3,
                }),
            });
            const sound = new Sound(stub as any);
            const state = await sound.getState();
            expect(state.channels).toHaveLength(4);
            expect(state.latchedRegister).toBe(3);

            const tone0 = state.channels[0]!;
            expect(tone0.channelId).toBe(0);
            expect(tone0.channelName).toBe("Tone0");
            expect(tone0.frequencyDivider).toBe(256);
            expect(tone0.volume).toBe(5);
            expect(tone0.outputBit).toBe(true);

            const noise = state.channels[3]!;
            expect(noise.channelId).toBe(3);
            expect(noise.channelName).toBe("Noise");
            expect(noise.noiseRate).toBe(2);
            expect(noise.whiteNoise).toBe(true);
            expect(noise.lfsrState).toBe(0x4000);
        });
    });
});

describe("TubeUlaInspection", () => {
    describe("getState", () => {
        it("maps the complex nested structure", async () => {
            const stub = createMockStub({
                getTubeState: () => ({
                    enabled: true,
                    controlFlags: {
                        q: true,
                        i: false,
                        j: true,
                        m: false,
                        v: true,
                        p: false,
                    },
                    r1H2p: { value: 0x42, dataAvailable: true },
                    r1P2h: {
                        count: 3,
                        data: new Uint8Array([0x10, 0x20, 0x30]),
                        dataAvailable: true,
                    },
                    r2H2p: { value: 0x00, dataAvailable: false },
                    r2P2h: { value: 0xFF, dataAvailable: true },
                    r3H2p: {
                        count: 2,
                        data: new Uint8Array([0xAA, 0xBB]),
                        pending: true,
                        threshold: 2,
                    },
                    r3P2h: {
                        count: 0,
                        data: new Uint8Array([]),
                        pending: false,
                        threshold: 1,
                    },
                    r4H2p: { value: 0x11, dataAvailable: true },
                    r4P2h: { value: 0x22, dataAvailable: false },
                    hostStatus: {
                        r1Status: 0x80,
                        r2Status: 0x40,
                        r3Status: 0x20,
                        r4Status: 0x10,
                    },
                    parasiteStatus: {
                        r1Status: 0x01,
                        r2Status: 0x02,
                        r3Status: 0x24,
                        r4Status: 0x08,
                    },
                    interrupts: {
                        hirq: true,
                        pirq: false,
                        pnmiLevel: true,
                        pnmiEdge: false,
                    },
                    hostStretched: true,
                    counters: {
                        r1H2pWrites: 100,
                        r1H2pReads: 99,
                        r2H2pWrites: 50,
                        r2H2pReads: 50,
                        r3H2pWrites: 200,
                        r3H2pReads: 198,
                        r4H2pWrites: 10,
                        r4H2pReads: 10,
                        r1P2hWrites: 300,
                        r1P2hReads: 297,
                        r2P2hWrites: 0,
                        r2P2hReads: 0,
                        r3P2hWrites: 0,
                        r3P2hReads: 0,
                        r4P2hWrites: 5,
                        r4P2hReads: 5,
                    },
                }),
            });
            const tubeUla = new TubeUlaInspection(stub as any);
            const state = await tubeUla.getState();

            expect(state.enabled).toBe(true);

            // Control flags
            expect(state.controlFlags.q).toBe(true);
            expect(state.controlFlags.i).toBe(false);
            expect(state.controlFlags.j).toBe(true);
            expect(state.controlFlags.m).toBe(false);
            expect(state.controlFlags.v).toBe(true);
            expect(state.controlFlags.p).toBe(false);

            // R1
            expect(state.r1H2p.value).toBe(0x42);
            expect(state.r1H2p.dataAvailable).toBe(true);
            expect(state.r1P2h.count).toBe(3);
            expect(state.r1P2h.dataAvailable).toBe(true);

            // R2
            expect(state.r2H2p.dataAvailable).toBe(false);
            expect(state.r2P2h.value).toBe(0xFF);

            // R3
            expect(state.r3H2p.count).toBe(2);
            expect(state.r3H2p.pending).toBe(true);
            expect(state.r3H2p.threshold).toBe(2);
            expect(state.r3P2h.threshold).toBe(1);

            // R4
            expect(state.r4H2p.value).toBe(0x11);

            // Status
            expect(state.hostStatus.r1Status).toBe(0x80);
            expect(state.parasiteStatus.r3Status).toBe(0x24);

            // Interrupts
            expect(state.interrupts.hirq).toBe(true);
            expect(state.interrupts.pirq).toBe(false);
            expect(state.interrupts.pnmiLevel).toBe(true);

            // Stretched
            expect(state.hostStretched).toBe(true);

            // Counters
            expect(state.counters.r1H2pWrites).toBe(100);
            expect(state.counters.r1P2hReads).toBe(297);
        });

        it("uses defaults when optional proto fields are undefined", async () => {
            const stub = createMockStub({
                getTubeState: () => ({
                    enabled: false,
                    controlFlags: undefined,
                    r1H2p: undefined,
                    r1P2h: undefined,
                    r2H2p: undefined,
                    r2P2h: undefined,
                    r3H2p: undefined,
                    r3P2h: undefined,
                    r4H2p: undefined,
                    r4P2h: undefined,
                    hostStatus: undefined,
                    parasiteStatus: undefined,
                    interrupts: undefined,
                    hostStretched: false,
                    counters: undefined,
                }),
            });
            const tubeUla = new TubeUlaInspection(stub as any);
            const state = await tubeUla.getState();

            expect(state.enabled).toBe(false);
            expect(state.controlFlags.q).toBe(false);
            expect(state.r1H2p.value).toBe(0);
            expect(state.r1P2h.count).toBe(0);
            expect(state.r3H2p.threshold).toBe(1);
            expect(state.hostStatus.r1Status).toBe(0);
            expect(state.interrupts.hirq).toBe(false);
            expect(state.counters.r1H2pWrites).toBe(0);
        });
    });
});
