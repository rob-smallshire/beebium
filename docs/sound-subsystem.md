# Sound Subsystem

## Overview

Beebium implements BBC Micro audio through a combination of:
1. **SN76489 sound chip emulation** - 3 tone channels + 1 noise channel
2. **Flexible N x 32-bit audio format** - supports future expansion (speech, 1 MHz bus audio)
3. **gRPC AudioService** - streaming and introspection for frontends

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Beebium Core                           │
├─────────────────────────────────────────────────────────────┤
│  Machine::step()                                            │
│    ├─> System VIA Port A writes                            │
│    │     └─> Addressable Latch bit 0 (sound_write_enabled) │
│    │           └─> Sn76489::write(data)                    │
│    └─> Sn76489::tick() [called at 2 MHz CPU rate]          │
│          └─> Phase accumulator derives 250 kHz internal    │
│                └─> AudioBuffer::push(AudioSample)          │
├─────────────────────────────────────────────────────────────┤
│  AudioBuffer (circular SPSC queue, 48000 samples)           │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ gRPC stream
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   AudioService (gRPC)                       │
├─────────────────────────────────────────────────────────────┤
│  SubscribeAudio() → stream AudioChunk                       │
│  GetAudioFormat() → metadata (encoding, channel names)      │
│  GetChannelStates() → introspection (freq, vol, LFSR)      │
└─────────────────────────────────────────────────────────────┘
```

## SN76489 Sound Chip

### Hardware Characteristics

- **Input clock**: 4 MHz (from Video ULA, directly to pin 14)
- **Internal clock**: 250 kHz (4 MHz ÷ 16 internal divider)
- **Output sample rate**: 48 kHz (emulator output)
- **Channels**: 3 tone generators + 1 noise generator

**Emulator note**: The emulator calls `tick()` at 2 MHz (CPU clock rate) and uses a
phase accumulator to derive the same 250 kHz internal clock (2 MHz ÷ 8 = 250 kHz).

### Register Protocol

The chip uses a latch/data byte protocol:

**Latch byte** (bit 7 = 1): `%1RRRdddd`
- RRR (bits 4-6): Register select (0-7)
- dddd (bits 0-3): Data low nibble

**Data byte** (bit 7 = 0): `%0Ddddddd`
- dddddd (bits 0-5): Data high 6 bits (tone frequency only)

### Register Map

| Register | Function |
|----------|----------|
| 0 | Tone 0 frequency (10-bit) |
| 1 | Tone 0 volume (4-bit, 0=max, 15=silent) |
| 2 | Tone 1 frequency |
| 3 | Tone 1 volume |
| 4 | Tone 2 frequency |
| 5 | Tone 2 volume |
| 6 | Noise control (rate + mode) |
| 7 | Noise volume |

### BBC BASIC Channel Mapping

The MOS inverts tone channel numbers:

| BBC BASIC Channel | SN76489 Register | Description |
|-------------------|------------------|-------------|
| 0 | 6, 7 | Noise channel |
| 1 | 4, 5 | Tone 2 (highest numbered) |
| 2 | 2, 3 | Tone 1 |
| 3 | 0, 1 | Tone 0 (lowest numbered) |

This mapping is defined in the MOS soundParameterTable at address $EB40.

### Frequency Calculation

```
frequency_hz = clock_hz / (32 × divider)
```

Where:
- `clock_hz` = 4,000,000 (4 MHz)
- `divider` = 10-bit value from register (0 treated as 1024)
- Range: ~122 Hz to 125 kHz

### Volume Table

4-bit logarithmic attenuation (~2 dB per step):

| Register Value | Attenuation | Amplitude (8-bit) |
|----------------|-------------|-------------------|
| 0 | 0 dB | 127 |
| 1 | -2 dB | 101 |
| 2 | -4 dB | 80 |
| ... | ... | ... |
| 14 | -28 dB | 5 |
| 15 | Silence | 0 |

### Noise Generator

- **15-bit LFSR** with taps at bits 0 and 1
- **White noise**: XOR feedback, period 32767
- **Periodic noise**: Simple rotation, period 15

**Register 6 format** (noise control): `%----MRR`
- RR (bits 0-1): Rate select
- M (bit 2): Mode (0=periodic, 1=white noise)

**Noise rate select** (bits 0-1):
- 0: N/512 (~488 Hz)
- 1: N/1024 (~244 Hz)
- 2: N/2048 (~122 Hz)
- 3: Clocked from Tone 2 output

**LFSR reset behavior** (MAME-verified): Writing to register 6 (noise control) resets
the LFSR to its initial state (`0x4000`). This is important for deterministic noise
sequences and matches hardware behavior documented in MAME's SN76496 implementation.

### DC Bias Behavior

The SN76489 has unusual DC bias characteristics that are important for advanced audio techniques like sampled sound playback.

**Key observations from real hardware** (see [Chris Evans' analysis](https://scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style-from-sn76489.html)):

- The output is unipolar: it rests at a silence level and pulls to a louder
  level while the flip-flop is high, rather than swinging symmetrically about a
  fixed mid-point.
- A silent channel sits at the silence level with no swing.
- The mid-point (mean) voltage of an active channel therefore varies with the
  volume setting -- it is the silence level plus half the volume-dependent swing.

**Why this matters:**

Sampled sound playback on the BBC Micro works by rapidly modulating volume to encode PCM data. Because the output is unipolar, the short-term mean of a fast (short-period) channel tracks the volume register, so a PCM waveform emerges at baseband from the high-frequency carrier. Downstream circuitry (LM386 amplifier) filters out the carrier, leaving the modulated audio.

**Beebium implementation:**

Each emitted per-channel sample is unipolar and unsigned: 0 at the silence level,
rising to twice the volume-table amplitude while the flip-flop is high. The AC
swing about the mean equals the volume-table amplitude (up to 127), so ordinary
audio keeps its loudness while the mean now carries the sampled baseband. DC is
NOT removed in the core; that is a consumer responsibility (the macOS renderer
high-passes at 20 Hz).

`compute_voltage_levels` reports the same shape as metadata, with a mid-point
that varies with volume:

```cpp
struct ToneChannelState {
    // ... standard fields ...
    float dc_bias_v;   // Mid-point voltage: silence level + swing/2 (varies with volume)
    float peak_v;      // Voltage when output_bit=1: silence level + swing
    float trough_v;    // Voltage when output_bit=0: the silence level
};
```

The swing follows the 2 dB attenuation curve of the volume table; the mid-point
rises with it (silence level 0.8 V):

| Volume | Swing (V) | Mid-point / DC bias (V) |
|--------|-----------|-------------------------|
| 0 (max) | 0.800 | 1.200 |
| 5 | 0.253 | 0.927 |
| 10 | 0.080 | 0.840 |
| 15 (silent) | 0.000 | 0.800 |

**Frontend usage example:**

```python
# Samples are already unipolar (0 = silence). Mix, then remove DC and the carrier.
for sample in audio_chunk:
    tone0 = unpack_uint8(sample.sources[0], 0)  # 0..254, 0 = silence

    # Apply a high-pass to remove the (volume-dependent) DC, and a low-pass to
    # remove any residual carrier, before mixing/playback.
    filtered_output = process(tone0)
```

## Audio Sample Format

Each sample contains N x 32-bit source fields:

```cpp
struct AudioSample {
    static constexpr size_t MAX_SOURCES = 4;
    uint32_t sources[MAX_SOURCES];
};
```

### SN76489 Source (indices 0 and 1)

Encoding: two `ENCODING_2X16BIT_SIGNED` fields, each two signed 16-bit channels
(`pack_2x16bit` stores the first channel in the low half, the second in the
high half):

```
source 0 = (tone0, tone1)
source 1 = (tone2, noise)
```

Each channel is unipolar: 0 is the silence level, rising toward the full-scale
high level `Sn76489::FULL_SCALE` (16384). The short-term mean carries any
sampled-sound baseband. Full scale sits well below the int16 range so the
anti-alias filter's overshoot (~11%) and the sum of the three tone channels
stay within range. DC removal and any final scaling are the consumer's job
(the macOS renderer high-passes at 20 Hz and normalises by half full-scale, so
a volume-0 square keeps unit AC amplitude).

### Future Sources (reserved)

- Index 2: 1 MHz bus audio (Music 5000)
- Index 3: Reserved

## AudioBuffer

The `AudioBuffer` class is a lock-free single-producer single-consumer (SPSC) circular
queue that bridges the emulator core and gRPC streaming.

### Characteristics

| Property | Value |
|----------|-------|
| Default capacity | 48,000 samples (~1 second @ 48 kHz) |
| Sample size | 16 bytes (4 × 32-bit sources) |
| Memory usage | ~768 KB at default capacity |
| Thread safety | SPSC lock-free (one producer, one consumer) |
| Overflow behavior | `push()` returns `false`, sample dropped |

### Producer Interface (core thread)

```cpp
AudioBuffer buffer(48000);

// Single sample push (SN76489: two 2x16 fields)
AudioSample sample;
sample.pack_2x16bit(0, tone0, tone1);
sample.pack_2x16bit(1, tone2, noise);
if (!buffer.push(sample)) {
    // Buffer full - sample dropped
}

// Batch write (more efficient)
auto producer = buffer.get_producer_buffer();
// Write samples to producer.data()...
buffer.produce(count);
```

### Consumer Interface (gRPC thread)

```cpp
std::vector<AudioSample> samples(1024);
size_t count = buffer.read(samples.data(), samples.size());
// Process 'count' samples...
```

### Sequence Numbers

The buffer tracks a monotonically increasing sequence number for drop detection:

```cpp
uint64_t seq = buffer.sequence();  // Total samples ever produced
```

Frontends can detect dropped samples by comparing `AudioChunk.sequence` values.

## C++ API Usage

### Direct Chip Access

```cpp
#include "beebium/devices/Sn76489.hpp"
#include "beebium/AudioBuffer.hpp"

// Create chip: 4 MHz input clock, 48 kHz output sample rate
Sn76489 chip(4'000'000, 48'000);
AudioBuffer buffer;

// Set Tone 0 to ~440 Hz (A4)
// Divider = 4,000,000 / (32 × 440) ≈ 284 = 0x11C
chip.write(0x8C);  // Latch: Tone 0 freq, low nibble = 0xC
chip.write(0x11);  // Data: high 6 bits = 0x11

// Set Tone 0 volume to maximum
chip.write(0x90);  // Latch: Tone 0 volume = 0 (max)

// Generate samples (call every 2 MHz cycle)
for (int i = 0; i < 2'000'000; ++i) {
    chip.tick(buffer);
}

// Read generated samples
std::vector<AudioSample> samples(48000);
size_t count = buffer.read(samples.data(), samples.size());
```

### Introspection

```cpp
// Query tone channel state
auto state = chip.get_tone_channel_state(0);
std::cout << "Frequency: " << state.frequency_hz << " Hz\n";
std::cout << "Volume: " << (int)state.volume << "\n";
std::cout << "Amplitude: " << (int)state.amplitude << "\n";
std::cout << "DC bias: " << state.dc_bias_v << " V\n";

// Query noise channel state
auto noise = chip.get_noise_channel_state();
std::cout << "LFSR: 0x" << std::hex << noise.lfsr << "\n";
std::cout << "White mode: " << noise.white_mode << "\n";
```

### Integration with Machine

In `ModelBHardware`, the sound chip is wired to the System VIA:

```cpp
// Hardware state includes sound chip and audio buffer
Sn76489 sound_chip{4'000'000, 48'000};
std::optional<AudioBuffer> audio_buffer;

// SystemViaPeripheral routes writes through addressable latch
system_via_peripheral.set_sound_chip(&sound_chip);

// Machine::step() ticks the sound chip every cycle
if (state_.memory.audio_buffer) {
    state_.memory.sound_chip.tick(state_.memory.audio_buffer.value());
}
```

## gRPC AudioService

### SubscribeAudio

Streams audio chunks at 48 kHz:

```protobuf
rpc SubscribeAudio(SubscribeAudioRequest) returns (stream AudioChunk);

message AudioChunk {
    uint64 sequence = 1;      // For drop detection
    uint32 sample_count = 2;  // Samples in this chunk
    bytes samples = 4;        // Packed sample data
}
```

### GetAudioFormat

Returns metadata for interpreting samples:

```protobuf
rpc GetAudioFormat(GetAudioFormatRequest) returns (AudioFormat);

message AudioFormat {
    uint32 sample_rate = 1;           // 48000
    uint32 source_count = 2;          // 4
    repeated AudioSource sources = 3; // Per-source metadata
    repeated ChannelGroup groups = 4; // UI grouping hints
}
```

### GetChannelStates

Returns current chip state for introspection:

```protobuf
rpc GetChannelStates(GetChannelStatesRequest) returns (ChannelStatesResponse);

message ChannelState {
    uint32 channel_id = 1;
    uint32 frequency_divider = 3;
    uint32 volume = 6;
    float frequency_hz = 7;
    int32 amplitude = 8;
    // Noise-specific fields for channel 3
}
```

## Frontend Integration

### Sample Unpacking (Python example)

```python
def unpack_samples(chunk, format):
    samples = chunk.samples
    source_count = format.source_count

    for i in range(chunk.sample_count):
        offset = i * source_count * 4

        # SN76489: sources 0 and 1, each two little-endian signed 16-bit
        # channels. source 0 = (tone0, tone1), source 1 = (tone2, noise).
        tone0, tone1, tone2, noise = struct.unpack_from('<4h', samples, offset)

        # Unipolar: 0 = silence, up to Sn76489::FULL_SCALE (16384) at full volume.
        yield (tone0, tone1, tone2, noise)
```

### Per-Channel Volume/Panning

Frontends can apply per-channel processing before mixing:

```python
for tone0, tone1, tone2, noise in unpack_samples(chunk, format):
    left = right = 0

    if not muted[0]:
        left += tone0 * volume[0] * (1 - pan[0])
        right += tone0 * volume[0] * (1 + pan[0])

    # ... repeat for other channels

    output(left, right)
```

## Files

| File | Description |
|------|-------------|
| `src/core/include/beebium/devices/Sn76489.hpp` | SN76489 class declaration |
| `src/core/src/Sn76489.cpp` | SN76489 implementation |
| `src/core/include/beebium/AudioBuffer.hpp` | Circular buffer for samples |
| `src/service/proto/audio.proto` | gRPC service definitions |
| `src/service/include/beebium/service/AudioService.hpp` | Service implementation |
| `tests/test_sn76489.cpp` | Core unit tests (register protocol, frequency, volume) |
| `tests/test_sn76489_dc_bias.cpp` | DC bias metadata tests |
| `tests/test_sn76489_mame.cpp` | MAME-verified behavior tests |
| `tests/test_sound_basic.cpp` | BBC BASIC SOUND/ENVELOPE integration |
| `tests/test_sound_via.cpp` | Direct VIA integration tests |
| `tests/test_sound_edge_cases.cpp` | Edge cases and boundary conditions |
| `tests/test_sound_state.cpp` | State introspection tests |
| `tests/test_sound_timing.cpp` | Timing and sample rate tests |
| `tests/test_grpc_audio.cpp` | gRPC service tests |

## Useful References

- [SN76489 chip details](http://www.zeridajh.org/articles/me_sn76489_sound_chip_details/index.html)
- [SN76489 Development](https://www.smspower.org/Development/SN76489)
- [SN76489 bus side behavior](https://stardot.org.uk/forums/viewtopic.php?t=30838)
- [Sampled sound on the SN76489](https://stardot.org.uk/forums/viewtopic.php?t=17537) - DC bias and volume modulation for PCM playback
- [Sampled sound 1980s style](https://scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style-from-sn76489.html) - Chris Evans' hardware measurements
- [MOS 1.20 Reassembly](https://tobylobster.github.io/mos/mos/S-s16.html)
