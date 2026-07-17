/**
 * SN76489 Sound Generator access for the Beebium TypeScript client.
 */

import type { DeviceInspectionClient } from "./generated/debugger.js";
import type {
    SoundGeneratorState as ProtoSoundGeneratorState,
    SoundChannelState as ProtoSoundChannelState,
    GetSoundGeneratorStateRequest,
} from "./generated/debugger.js";
import { promisify } from "./call-utils.js";

export interface SoundChannelState {
    /** SN76489 channel index (0-2 = Tone0-2, 3 = Noise) */
    channelId: number;
    /** "Tone0", "Tone1", "Tone2", or "Noise" */
    channelName: string;
    /** 10-bit divider value (0-1023) */
    frequencyDivider: number;
    /** Current countdown value */
    counter: number;
    /** Square wave polarity */
    outputBit: boolean;
    /** 4-bit attenuation (0=max, 15=silent) */
    volume: number;
    /** Computed output frequency */
    frequencyHz: number;
    /** 2-bit rate select (0-3, where 3=Tone2). Only valid for noise channel. */
    noiseRate: number;
    /** True=white noise, False=periodic. Only valid for noise channel. */
    whiteNoise: boolean;
    /** 15-bit LFSR state. Only valid for noise channel. */
    lfsrState: number;
}

export interface SoundGeneratorState {
    /** 4 channels: Tone0, Tone1, Tone2, Noise */
    channels: SoundChannelState[];
    /** Currently latched register (0-7) */
    latchedRegister: number;
}

// -- MOS channel mapping ------------------------------------------------------

/**
 * MOS SOUND command channel mapping to SN76489 internal channels.
 * MOS channel 0 = Noise (SN76489 index 3)
 * MOS channel 1 = Tone0 (SN76489 index 0)
 * MOS channel 2 = Tone1 (SN76489 index 1)
 * MOS channel 3 = Tone2 (SN76489 index 2)
 */
export const MOS_TO_SN76489: Record<number, number> = { 0: 3, 1: 0, 2: 1, 3: 2 };
export const SN76489_TO_MOS: Record<number, number> = { 0: 1, 1: 2, 2: 3, 3: 0 };

// -- Convenience accessors ----------------------------------------------------

/** Get tone channel by SN76489 index (0-2). */
export function tone(state: SoundGeneratorState, index: number): SoundChannelState {
    if (index < 0 || index > 2) {
        throw new RangeError(`Tone channel index must be 0-2, got ${index}`);
    }
    return state.channels[index]!;
}

/** Get the noise channel. */
export function noise(state: SoundGeneratorState): SoundChannelState {
    return state.channels[3]!;
}

/** Get channel by MOS SOUND command number (0-3). */
export function mosChannel(state: SoundGeneratorState, mosNum: number): SoundChannelState {
    if (mosNum < 0 || mosNum > 3) {
        throw new RangeError(`MOS channel number must be 0-3, got ${mosNum}`);
    }
    return state.channels[MOS_TO_SN76489[mosNum]!]!;
}

// -- Formatting ---------------------------------------------------------------

/** Format a single channel state for display. */
function formatChannel(ch: SoundChannelState): string {
    if (ch.channelId === 3) {
        const mode = ch.whiteNoise ? "White" : "Periodic";
        return (
            `${ch.channelName}: Vol=${ch.volume} Rate=${ch.noiseRate} ` +
            `Mode=${mode} LFSR=${ch.lfsrState.toString(16).toUpperCase().padStart(4, "0")}`
        );
    }
    return (
        `${ch.channelName}: Vol=${ch.volume} Freq=${ch.frequencyDivider} ` +
        `(${ch.frequencyHz.toFixed(1)} Hz) Out=${ch.outputBit ? 1 : 0}`
    );
}

/** Format a sound generator state snapshot for display. */
export function formatSoundState(s: SoundGeneratorState): string {
    const lines = [`Latched register: ${s.latchedRegister}`];
    for (const ch of s.channels) {
        lines.push(formatChannel(ch));
    }
    return lines.join("\n");
}

// -- Conversion ---------------------------------------------------------------

function toSoundChannelState(proto: ProtoSoundChannelState): SoundChannelState {
    return {
        channelId: proto.channelId,
        channelName: proto.channelName,
        frequencyDivider: proto.frequencyDivider,
        counter: proto.counter,
        outputBit: proto.outputBit,
        volume: proto.volume,
        frequencyHz: proto.frequencyHz,
        noiseRate: proto.noiseRate,
        whiteNoise: proto.whiteNoise,
        lfsrState: proto.lfsrState,
    };
}

function toSoundGeneratorState(proto: ProtoSoundGeneratorState): SoundGeneratorState {
    return {
        channels: proto.channels.map(toSoundChannelState),
        latchedRegister: proto.latchedRegister,
    };
}

// -- Class --------------------------------------------------------------------

/**
 * SN76489 Sound Generator access.
 *
 * Provides read access to sound generator state, which is write-only on hardware.
 * Returns chip-native channel ordering (Tone0, Tone1, Tone2, Noise).
 */
export class Sound {
    private readonly stub: DeviceInspectionClient;

    constructor(stub: DeviceInspectionClient) {
        this.stub = stub;
    }

    /** Get the current sound generator state. */
    async getState(): Promise<SoundGeneratorState> {
        const response = await promisify<GetSoundGeneratorStateRequest, ProtoSoundGeneratorState>(
            this.stub as unknown as Record<string, Function>,
            "getSoundGeneratorState",
            {},
        );
        return toSoundGeneratorState(response);
    }
}
