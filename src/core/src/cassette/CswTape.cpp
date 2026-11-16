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

#include "beebium/cassette/CswTape.hpp"

#include "Inflate.hpp"

#include <cstring>
#include <stdexcept>

namespace beebium {

namespace {

constexpr char MAGIC[] = "Compressed Square Wave\x1A";  // 23 bytes
constexpr size_t MAGIC_LENGTH = 23;

uint16_t read_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t read_u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::unique_ptr<CswTape> CswTape::open(std::span<const uint8_t> bytes, const std::string& name) {
    if (bytes.size() < 0x20 || std::memcmp(bytes.data(), MAGIC, MAGIC_LENGTH) != 0) {
        throw TapeImageError(TapeImageError::Kind::UnsupportedFormat, "The file " + name + " is not a CSW tape image.");
    }
    const unsigned major = bytes[0x17];
    const unsigned minor = bytes[0x18];

    std::unique_ptr<CswTape> tape(new CswTape());
    unsigned compression = 0;
    uint8_t flags = 0;
    size_t data_offset = 0;
    if (major == 1) {
        tape->sample_rate_ = read_u16(&bytes[0x19]);
        compression = bytes[0x1B];
        flags = bytes[0x1C];
        data_offset = 0x20;
        if (compression != 1) {
            throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name +
                                     " is a version 1 CSW with a compression other than RLE, "
                                     "which Beebium does not read.");
        }
    } else if (major == 2) {
        if (bytes.size() < 0x34) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " ends part way through its CSW header.");
        }
        tape->sample_rate_ = read_u32(&bytes[0x19]);
        compression = bytes[0x21];
        flags = bytes[0x22];
        const unsigned extension_length = bytes[0x23];
        char application[17] = {};
        std::memcpy(application, &bytes[0x24], 16);
        tape->metadata_.origin = application;
        data_offset = 0x34 + extension_length;
        if (data_offset > bytes.size()) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " ends part way through its CSW header.");
        }
        if (compression != 1 && compression != 2) {
            throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name + " uses CSW compression " +
                                     std::to_string(compression) + ", which Beebium does not read.");
        }
    } else {
        throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name + " is a CSW of version " + std::to_string(major) +
                                 "." + std::to_string(minor) + ", which Beebium does not read.");
    }
    if (tape->sample_rate_ == 0) {
        throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name + " gives a sample rate of 0, so it cannot be played.");
    }
    tape->initial_level_ = (flags & 0x01) != 0;
    tape->gap_samples_ = tape->sample_rate_ / 10;  // 100 ms

    std::vector<uint8_t> inflated;
    std::span<const uint8_t> data = bytes.subspan(data_offset);
    if (compression == 2) {
        auto result = cassette_detail::inflate_all(data, cassette_detail::Container::Zlib);
        if (!result) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " has damaged compressed data.");
        }
        inflated = std::move(*result);
        data = inflated;
    }

    // Decode the pulse lengths: a byte each, or 0 followed by a 32-bit length.
    tape->pulses_.reserve(data.size());
    for (size_t i = 0; i < data.size();) {
        uint32_t length = data[i++];
        if (length == 0) {
            if (data.size() - i < 4) {
                throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " ends part way through a long pulse.");
            }
            length = read_u32(&data[i]);
            i += 4;
        }
        if (length == 0) continue;
        if (tape->pulses_.size() % CHECKPOINT_INTERVAL == 0) {
            tape->checkpoints_.push_back(tape->total_samples_);
        }
        tape->pulses_.push_back(length);
        tape->total_samples_ += length;
    }
    return tape;
}

TapeItem CswTape::next() {
    if (next_pulse_ >= pulses_.size()) {
        return TapeItem::end(time_of(total_samples_));
    }
    const size_t k = next_pulse_++;
    const uint32_t length = pulses_[k];
    const uint64_t start = samples_before_next_;
    samples_before_next_ += length;
    if (length > gap_samples_) {
        return TapeItem::gap(time_of(samples_before_next_));
    }
    const bool level = initial_level_ != ((k & 1) != 0);
    return TapeItem::edge(time_of(start), level);
}

void CswTape::seek(TapeTime time) {
    // The last checkpoint at or before `time`, then pulse by pulse.
    size_t block = 0;
    while (block + 1 < checkpoints_.size() && time_of(checkpoints_[block + 1]) <= time) ++block;
    next_pulse_ = block * CHECKPOINT_INTERVAL;
    samples_before_next_ = checkpoints_.empty() ? 0 : checkpoints_[block];
    while (next_pulse_ < pulses_.size()) {
        const uint32_t length = pulses_[next_pulse_];
        const uint64_t end = samples_before_next_ + length;
        // A pulse is before `time` if its edge (its start) is, or, for a gap,
        // if the silence has ended by then.
        const bool before = length > gap_samples_ ? time_of(end) <= time
                                                  : time_of(samples_before_next_) < time;
        if (!before) break;
        samples_before_next_ = end;
        ++next_pulse_;
    }
}

std::optional<TapeTime> CswTape::duration() const {
    return time_of(total_samples_);
}

}  // namespace beebium
