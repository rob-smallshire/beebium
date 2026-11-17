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

#include "beebium/cassette/SampledSource.hpp"

#include "beebium/cassette/TapeSignal.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace beebium {

namespace {

class FileByteReader final : public ByteReader {
public:
    explicit FileByteReader(const std::filesystem::path& filepath)
        : file_(filepath, std::ios::binary) {
        std::error_code ec;
        size_ = std::filesystem::file_size(filepath, ec);
        if (ec) size_ = 0;
    }
    bool ok() const { return static_cast<bool>(file_); }
    uint64_t size() const override { return size_; }
    size_t read_at(uint64_t offset, std::span<uint8_t> out) override {
        if (offset >= size_) return 0;
        const auto n = static_cast<size_t>(std::min<uint64_t>(out.size(), size_ - offset));
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
        return static_cast<size_t>(file_.gcount());
    }

private:
    std::ifstream file_;
    uint64_t size_ = 0;
};

class MemoryByteReader final : public ByteReader {
public:
    explicit MemoryByteReader(std::span<const uint8_t> bytes) : bytes_(bytes.begin(), bytes.end()) {}
    uint64_t size() const override { return bytes_.size(); }
    size_t read_at(uint64_t offset, std::span<uint8_t> out) override {
        if (offset >= bytes_.size()) return 0;
        const auto n = static_cast<size_t>(std::min<uint64_t>(out.size(), bytes_.size() - offset));
        std::memcpy(out.data(), bytes_.data() + offset, n);
        return n;
    }

private:
    std::vector<uint8_t> bytes_;
};

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint16_t FORMAT_PCM = 0x0001;
constexpr uint16_t FORMAT_FLOAT = 0x0003;
constexpr uint16_t FORMAT_EXTENSIBLE = 0xFFFE;

}  // namespace

std::unique_ptr<ByteReader> ByteReader::of_file(const std::filesystem::path& filepath) {
    auto reader = std::make_unique<FileByteReader>(filepath);
    if (!reader->ok()) return nullptr;
    return reader;
}

std::unique_ptr<ByteReader> ByteReader::of_memory(std::span<const uint8_t> bytes) {
    return std::make_unique<MemoryByteReader>(bytes);
}

std::unique_ptr<WavSource> WavSource::open(std::unique_ptr<ByteReader> bytes, const std::string& name,
                                           std::optional<WavChannel> channel) {
    auto refuse = [&](TapeImageError::Kind kind, const std::string& why) {
        throw TapeImageError(kind, "The file " + name + " " + why);
    };
    uint8_t header[12];
    if (bytes->read_at(0, header) != sizeof(header) || std::memcmp(header, "RIFF", 4) != 0 ||
        std::memcmp(header + 8, "WAVE", 4) != 0) {
        refuse(TapeImageError::Kind::UnsupportedFormat, "is not a WAV file.");
    }

    std::unique_ptr<WavSource> wav(new WavSource());
    bool have_format = false;
    std::optional<uint64_t> data_size;
    uint64_t pos = 12;
    while (pos + 8 <= bytes->size()) {
        uint8_t chunk[8];
        bytes->read_at(pos, chunk);
        const uint32_t length = u32(chunk + 4);
        const uint64_t body = pos + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[40] = {};
            if (length < 16) refuse(TapeImageError::Kind::InvalidImage, "has a WAV format chunk that is too short.");
            bytes->read_at(body, std::span<uint8_t>(fmt, std::min<size_t>(length, sizeof(fmt))));
            uint16_t tag = u16(fmt);
            wav->channels_ = u16(fmt + 2);
            wav->rate_ = u32(fmt + 4);
            wav->frame_bytes_ = u16(fmt + 12);
            wav->bits_ = u16(fmt + 14);
            if (tag == FORMAT_EXTENSIBLE) {
                if (length < 40) refuse(TapeImageError::Kind::InvalidImage, "has an extensible WAV format chunk that is too short.");
                tag = u16(fmt + 24);  // the subformat GUID's first two bytes
            }
            if (tag != FORMAT_PCM && tag != FORMAT_FLOAT) {
                refuse(TapeImageError::Kind::UnsupportedFormat,
                       "is a WAV of a compressed or unusual encoding; Beebium plays PCM and 32-bit float WAVs.");
            }
            wav->float_ = tag == FORMAT_FLOAT;
            if (wav->float_ ? wav->bits_ != 32
                            : (wav->bits_ != 8 && wav->bits_ != 16 && wav->bits_ != 24 && wav->bits_ != 32)) {
                refuse(TapeImageError::Kind::UnsupportedFormat,
                       "is a WAV of " + std::to_string(wav->bits_) + "-bit " + (wav->float_ ? "float" : "integer") +
                           " samples; Beebium plays 8, 16, 24 and 32-bit integer and 32-bit float.");
            }
            if (wav->channels_ < 1 || wav->channels_ > 2) {
                refuse(TapeImageError::Kind::UnsupportedFormat,
                       "is a WAV of " + std::to_string(wav->channels_) + " channels; Beebium plays mono and stereo.");
            }
            if (wav->rate_ == 0 || wav->frame_bytes_ != wav->channels_ * wav->bits_ / 8) {
                refuse(TapeImageError::Kind::InvalidImage, "has a WAV format chunk that does not add up.");
            }
            have_format = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!have_format) refuse(TapeImageError::Kind::InvalidImage, "has WAV data before its format.");
            if (body + length > bytes->size()) {
                refuse(TapeImageError::Kind::InvalidImage, "is a WAV whose data runs past the end of the file.");
            }
            wav->data_offset_ = body;
            data_size = length;
            break;
        }
        pos = body + length + (length & 1);  // chunks are word aligned
    }
    if (!have_format || !data_size) refuse(TapeImageError::Kind::InvalidImage, "is a WAV with no format or no data.");

    wav->bytes_ = std::move(bytes);
    wav->frames_ = *data_size / wav->frame_bytes_;
    wav->cache_.resize(BLOCK_FRAMES * wav->frame_bytes_);

    if (wav->channels_ == 1) {
        wav->channel_ = WavChannel::Left;
    } else if (channel) {
        wav->channel_ = *channel;
    } else {
        // The louder channel over the first 10 seconds.
        const uint64_t frames = std::min<uint64_t>(wav->frames_, static_cast<uint64_t>(wav->rate_) * 10);
        double sum[2] = {0.0, 0.0};
        for (uint64_t f = 0; f < frames; ++f) {
            const uint64_t block = f / BLOCK_FRAMES;
            if (wav->cached_block_ != block) wav->load_block(block);
            const uint8_t* frame = wav->cache_.data() + (f % BLOCK_FRAMES) * wav->frame_bytes_;
            for (unsigned c = 0; c < 2; ++c) {
                const double s = wav->sample_at(frame, c);
                sum[c] += s * s;
            }
        }
        wav->channel_ = sum[1] > sum[0] ? WavChannel::Right : WavChannel::Left;
    }
    return wav;
}

float WavSource::sample_at(const uint8_t* frame, unsigned channel) const {
    const uint8_t* p = frame + channel * (bits_ / 8);
    if (float_) {
        float f;
        std::memcpy(&f, p, 4);
        return f;
    }
    switch (bits_) {
        case 8: return (static_cast<int>(p[0]) - 128) / 128.0f;
        case 16: return static_cast<int16_t>(u16(p)) / 32768.0f;
        case 24: {
            const int32_t v = static_cast<int32_t>((p[0] << 8) | (p[1] << 16) | (static_cast<uint32_t>(p[2]) << 24)) >> 8;
            return static_cast<float>(v) / 8388608.0f;
        }
        default: return static_cast<float>(static_cast<int32_t>(u32(p)) / 2147483648.0);
    }
}

bool WavSource::load_block(uint64_t block) {
    const uint64_t first = block * BLOCK_FRAMES;
    if (first >= frames_) return false;
    const auto frames = static_cast<size_t>(std::min<uint64_t>(BLOCK_FRAMES, frames_ - first));
    bytes_->read_at(data_offset_ + first * frame_bytes_, std::span<uint8_t>(cache_.data(), frames * frame_bytes_));
    cached_block_ = block;
    return true;
}

size_t WavSource::read(std::span<float> out) {
    size_t n = 0;
    const unsigned channel = channel_ == WavChannel::Right && channels_ == 2 ? 1 : 0;
    while (n < out.size() && position_ < frames_) {
        const uint64_t block = position_ / BLOCK_FRAMES;
        if (cached_block_ != block && !load_block(block)) break;
        out[n++] = sample_at(cache_.data() + (position_ % BLOCK_FRAMES) * frame_bytes_, channel);
        ++position_;
    }
    return n;
}

void WavSource::seek(uint64_t index) { position_ = std::min(index, frames_); }

}  // namespace beebium
