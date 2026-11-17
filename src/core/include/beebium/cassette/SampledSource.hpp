// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace beebium {

// A tape as sound: mono samples in [-1, 1) at a fixed rate, pulled in blocks.
// A WAV file is one; a live stream (a later round) is another. Called on the
// emulation thread.
class SampledSource {
public:
    virtual ~SampledSource() = default;

    // Fill `out` with the next samples; returns how many, fewer only at the end.
    virtual size_t read(std::span<float> out) = 0;
    // The next read starts at sample `index`.
    virtual void seek(uint64_t index) = 0;
    virtual uint64_t sample_count() const = 0;
    virtual uint32_t rate() const = 0;
};

// Bytes a WAV is read from: a file, read on demand, or memory.
class ByteReader {
public:
    virtual ~ByteReader() = default;
    virtual uint64_t size() const = 0;
    // Read up to out.size() bytes at `offset`; returns how many.
    virtual size_t read_at(uint64_t offset, std::span<uint8_t> out) = 0;

    static std::unique_ptr<ByteReader> of_file(const std::filesystem::path& filepath);
    static std::unique_ptr<ByteReader> of_memory(std::span<const uint8_t> bytes);
};

// A stereo WAV's channel.
enum class WavChannel : uint8_t { Left, Right };

// A RIFF/WAVE file: PCM of 8 (unsigned), 16, 24 or 32-bit integer samples, or
// 32-bit float, at any rate, mono or stereo, plain or WAVE_FORMAT_EXTENSIBLE
// with one of those subformats. The data is read on demand in blocks, so a WAV
// of any length plays without being loaded whole.
//
// A stereo file plays one channel: the one asked for, or else the one with the
// higher RMS over the first 10 seconds (left on a tie). Anything else (ADPCM,
// mu-law, more than two channels, a data chunk running past the end of the
// file) is refused with a TapeImageError naming the file.
class WavSource final : public SampledSource {
public:
    static std::unique_ptr<WavSource> open(std::unique_ptr<ByteReader> bytes, const std::string& name,
                                           std::optional<WavChannel> channel = std::nullopt);

    size_t read(std::span<float> out) override;
    void seek(uint64_t index) override;
    uint64_t sample_count() const override { return frames_; }
    uint32_t rate() const override { return rate_; }

    unsigned channels() const { return channels_; }
    unsigned bits() const { return bits_; }
    bool is_float() const { return float_; }
    // The channel played: Left for a mono file.
    WavChannel channel() const { return channel_; }

    // Frames a block of the cache holds.
    static constexpr size_t BLOCK_FRAMES = 16384;

private:
    WavSource() = default;

    // The sample of `channel` in frame `frame` of the current block.
    float sample_at(const uint8_t* frame, unsigned channel) const;
    bool load_block(uint64_t block);

    std::unique_ptr<ByteReader> bytes_;
    uint64_t data_offset_ = 0;
    uint64_t frames_ = 0;
    uint32_t rate_ = 0;
    unsigned channels_ = 1;
    unsigned bits_ = 16;
    bool float_ = false;
    unsigned frame_bytes_ = 2;
    WavChannel channel_ = WavChannel::Left;

    uint64_t position_ = 0;  // the next frame read() returns
    std::optional<uint64_t> cached_block_;
    std::vector<uint8_t> cache_;
};

}  // namespace beebium
