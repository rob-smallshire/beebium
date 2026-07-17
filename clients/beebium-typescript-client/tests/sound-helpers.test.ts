import { describe, it, expect } from "vitest";
import {
    MOS_TO_SN76489,
    SN76489_TO_MOS,
    tone,
    noise,
    mosChannel,
    type SoundGeneratorState,
    type SoundChannelState,
} from "../src/sound.js";

function makeChannel(id: number, name: string): SoundChannelState {
    return {
        channelId: id,
        channelName: name,
        frequencyDivider: 0,
        counter: 0,
        outputBit: false,
        volume: 15,
        frequencyHz: 0,
        noiseRate: 0,
        whiteNoise: false,
        lfsrState: 0,
    };
}

function makeState(): SoundGeneratorState {
    return {
        channels: [
            makeChannel(0, "Tone0"),
            makeChannel(1, "Tone1"),
            makeChannel(2, "Tone2"),
            makeChannel(3, "Noise"),
        ],
        latchedRegister: 0,
    };
}

describe("MOS_TO_SN76489 mapping", () => {
    it("MOS channel 0 (noise) maps to SN76489 channel 3", () => {
        expect(MOS_TO_SN76489[0]).toBe(3);
    });

    it("MOS channel 1 maps to SN76489 channel 0 (Tone0)", () => {
        expect(MOS_TO_SN76489[1]).toBe(0);
    });

    it("MOS channel 2 maps to SN76489 channel 1 (Tone1)", () => {
        expect(MOS_TO_SN76489[2]).toBe(1);
    });

    it("MOS channel 3 maps to SN76489 channel 2 (Tone2)", () => {
        expect(MOS_TO_SN76489[3]).toBe(2);
    });
});

describe("SN76489_TO_MOS mapping", () => {
    it("SN76489 channel 0 maps to MOS channel 1", () => {
        expect(SN76489_TO_MOS[0]).toBe(1);
    });

    it("SN76489 channel 1 maps to MOS channel 2", () => {
        expect(SN76489_TO_MOS[1]).toBe(2);
    });

    it("SN76489 channel 2 maps to MOS channel 3", () => {
        expect(SN76489_TO_MOS[2]).toBe(3);
    });

    it("SN76489 channel 3 maps to MOS channel 0", () => {
        expect(SN76489_TO_MOS[3]).toBe(0);
    });
});

describe("tone()", () => {
    it("returns Tone0 for index 0", () => {
        const state = makeState();
        const ch = tone(state, 0);
        expect(ch.channelId).toBe(0);
        expect(ch.channelName).toBe("Tone0");
    });

    it("returns Tone1 for index 1", () => {
        const state = makeState();
        const ch = tone(state, 1);
        expect(ch.channelId).toBe(1);
        expect(ch.channelName).toBe("Tone1");
    });

    it("returns Tone2 for index 2", () => {
        const state = makeState();
        const ch = tone(state, 2);
        expect(ch.channelId).toBe(2);
        expect(ch.channelName).toBe("Tone2");
    });

    it("throws RangeError for index 3", () => {
        const state = makeState();
        expect(() => tone(state, 3)).toThrow(RangeError);
    });

    it("throws RangeError for negative index", () => {
        const state = makeState();
        expect(() => tone(state, -1)).toThrow(RangeError);
    });
});

describe("noise()", () => {
    it("returns the noise channel (index 3)", () => {
        const state = makeState();
        const ch = noise(state);
        expect(ch.channelId).toBe(3);
        expect(ch.channelName).toBe("Noise");
    });
});

describe("mosChannel()", () => {
    it("MOS 0 returns noise channel", () => {
        const state = makeState();
        const ch = mosChannel(state, 0);
        expect(ch.channelId).toBe(3);
    });

    it("MOS 1 returns Tone0", () => {
        const state = makeState();
        const ch = mosChannel(state, 1);
        expect(ch.channelId).toBe(0);
    });

    it("MOS 2 returns Tone1", () => {
        const state = makeState();
        const ch = mosChannel(state, 2);
        expect(ch.channelId).toBe(1);
    });

    it("MOS 3 returns Tone2", () => {
        const state = makeState();
        const ch = mosChannel(state, 3);
        expect(ch.channelId).toBe(2);
    });

    it("throws RangeError for MOS channel 4", () => {
        const state = makeState();
        expect(() => mosChannel(state, 4)).toThrow(RangeError);
    });

    it("throws RangeError for negative MOS channel", () => {
        const state = makeState();
        expect(() => mosChannel(state, -1)).toThrow(RangeError);
    });
});
