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

#include "beebium/cassette/UefWriter.hpp"

#include "Inflate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace beebium {

namespace {

// Nominal half-cycles at the 1200 Hz base, in ULA clocks.
constexpr double SHORT_HALF = ULA_CLOCK_HZ / 4800.0;  // 2400 Hz, 256.41 clocks
constexpr double LONG_HALF = ULA_CLOCK_HZ / 2400.0;   // 1200 Hz, 512.82 clocks
// More than two 1200 Hz half-cycles without an edge is silence.
constexpr TapeTime GAP_CLOCKS = 2 * 512;
// A silence within this of a whole number of 1200 Hz half-cycles is that
// number: edges fall on whole clocks, so each end of a silence the reader
// rendered can be a clock short of where the nominal base puts it.
constexpr double WHOLE_HALVES_CLOCKS = 2.0;
// A run of more short cycles than any frame holds is carrier, not data.
constexpr size_t CARRIER_RUN_CYCLES = 24;

enum : uint8_t { SHORT = 0, LONG = 1 };

struct Format {
    unsigned data_bits;
    char parity;  // 'N', 'E' or 'O'
};
constexpr Format FORMATS[] = {{8, 'N'}, {8, 'E'}, {8, 'O'}, {7, 'E'}, {7, 'O'}};

void put_u16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

// Collects chunks, merging runs of carrier, bytes and bits of one kind, and
// keeps the nominal time written so far.
class ChunkStream {
public:
    void carrier(size_t cycles) {
        flush_bytes();
        flush_bits();
        pending_carrier_ += cycles;
        written_ += static_cast<double>(cycles) * 2.0 * SHORT_HALF;
    }

    void byte(uint8_t value, const Format& format, bool baud_300) {
        flush_carrier();
        flush_bits();
        if (baud_300 != baud_300_) {
            flush_bytes();
            std::vector<uint8_t> body;
            put_u16(body, baud_300 ? 300 : 1200);
            chunks_.push_back({0x0117, std::move(body)});
            baud_300_ = baud_300;
        }
        if (!bytes_.empty() && (format.data_bits != format_.data_bits || format.parity != format_.parity)) {
            flush_bytes();
        }
        format_ = format;
        bytes_.push_back(value);
        unsigned ones = 0;
        for (unsigned i = 0; i < format.data_bits; ++i) ones += (value >> i) & 1;
        const unsigned mult = baud_300 ? 4 : 1;
        bit_time(false, mult);
        for (unsigned i = 0; i < format.data_bits; ++i) bit_time(((value >> i) & 1) != 0, mult);
        if (format.parity == 'E') bit_time((ones & 1) != 0, mult);
        if (format.parity == 'O') bit_time((ones & 1) == 0, mult);
        bit_time(true, mult);
    }

    void bit(bool one) {
        flush_carrier();
        flush_bytes();
        bits_.push_back(one);
        bit_time(one, 1);
    }

    void gap(double clocks) {
        if (!(clocks >= 1.0)) return;
        flush_carrier();
        flush_bytes();
        flush_bits();
        const double halves = std::round(clocks / LONG_HALF);
        std::vector<uint8_t> body;
        if (halves >= 1.0 && halves <= 65535.0 && std::fabs(clocks - halves * LONG_HALF) <= WHOLE_HALVES_CLOCKS) {
            put_u16(body, static_cast<uint32_t>(halves));
            chunks_.push_back({0x0112, std::move(body)});
        } else {
            const float seconds = static_cast<float>(clocks / ULA_CLOCK_HZ);
            body.resize(4);
            std::memcpy(body.data(), &seconds, 4);
            chunks_.push_back({0x0116, std::move(body)});
        }
        written_ += clocks;
    }

    double written() const { return written_; }
    bool baud_300() const { return baud_300_; }

    std::vector<UefChunk> finish() {
        flush_carrier();
        flush_bytes();
        flush_bits();
        return std::move(chunks_);
    }

private:
    void bit_time(bool one, unsigned mult) {
        written_ += (one ? 4.0 * SHORT_HALF : 2.0 * LONG_HALF) * mult;
    }

    void flush_carrier() {
        while (pending_carrier_ > 0) {
            const size_t n = std::min<size_t>(pending_carrier_, 65535);
            std::vector<uint8_t> body;
            put_u16(body, static_cast<uint32_t>(n));
            chunks_.push_back({0x0110, std::move(body)});
            pending_carrier_ -= n;
        }
    }

    void flush_bytes() {
        if (bytes_.empty()) return;
        if (format_.data_bits == 8 && format_.parity == 'N') {
            chunks_.push_back({0x0100, std::move(bytes_)});
        } else {
            std::vector<uint8_t> body{static_cast<uint8_t>(format_.data_bits), static_cast<uint8_t>(format_.parity), 1};
            body.insert(body.end(), bytes_.begin(), bytes_.end());
            chunks_.push_back({0x0104, std::move(body)});
        }
        bytes_.clear();
    }

    void flush_bits() {
        if (bits_.empty()) return;
        // The first byte is the number of unused bits in the last; the bits
        // follow least significant first.
        const size_t bytes = (bits_.size() + 7) / 8;
        std::vector<uint8_t> body(1 + bytes, 0);
        body[0] = static_cast<uint8_t>(bytes * 8 - bits_.size());
        for (size_t i = 0; i < bits_.size(); ++i) {
            if (bits_[i]) body[1 + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        }
        chunks_.push_back({0x0102, std::move(body)});
        bits_.clear();
    }

    std::vector<UefChunk> chunks_;
    size_t pending_carrier_ = 0;
    std::vector<uint8_t> bytes_;
    Format format_{8, 'N'};
    std::vector<bool> bits_;
    bool baud_300_ = false;
    double written_ = 0.0;
};

// One bit at `mult` cycles a bit (1 at 1200 baud, 4 at 300): mult long cycles
// for 0, 2 x mult short for 1.
std::optional<bool> read_bit(const std::vector<uint8_t>& c, size_t& pos, unsigned mult) {
    if (pos + mult <= c.size() && std::all_of(c.begin() + static_cast<std::ptrdiff_t>(pos),
                                              c.begin() + static_cast<std::ptrdiff_t>(pos + mult),
                                              [](uint8_t x) { return x == LONG; })) {
        pos += mult;
        return false;
    }
    if (pos + 2 * mult <= c.size() && std::all_of(c.begin() + static_cast<std::ptrdiff_t>(pos),
                                                  c.begin() + static_cast<std::ptrdiff_t>(pos + 2 * mult),
                                                  [](uint8_t x) { return x == SHORT; })) {
        pos += 2 * mult;
        return true;
    }
    return std::nullopt;
}

// A frame starting at `pos` in this format and baud: its value and length.
std::optional<std::pair<uint8_t, size_t>> try_frame(const std::vector<uint8_t>& c, size_t pos,
                                                    const Format& format, bool baud_300) {
    const unsigned mult = baud_300 ? 4 : 1;
    size_t p = pos;
    const auto start = read_bit(c, p, mult);
    if (!start || *start) return std::nullopt;
    unsigned value = 0;
    unsigned ones = 0;
    for (unsigned i = 0; i < format.data_bits; ++i) {
        const auto b = read_bit(c, p, mult);
        if (!b) return std::nullopt;
        value |= (*b ? 1u : 0u) << i;
        ones += *b ? 1 : 0;
    }
    if (format.parity != 'N') {
        const auto b = read_bit(c, p, mult);
        if (!b) return std::nullopt;
        const bool even = ((ones + (*b ? 1 : 0)) & 1) == 0;
        if (even != (format.parity == 'E')) return std::nullopt;
    }
    const auto stop = read_bit(c, p, mult);
    if (!stop || !*stop) return std::nullopt;
    return std::make_pair(static_cast<uint8_t>(value), p - pos);
}

struct Framed {
    uint8_t value;
    size_t length;
    Format format;
    bool baud_300;
};

// The cycles a run of back-to-back frames in one format and baud covers from
// `pos`, carrier between them allowed.
size_t run_cycles(const std::vector<uint8_t>& c, size_t pos, const Format& format, bool baud_300) {
    size_t p = pos;
    while (p < c.size()) {
        const auto f = try_frame(c, p, format, baud_300);
        if (!f) break;
        p += f->second;
        while (p < c.size() && c[p] == SHORT) ++p;
    }
    return p - pos;
}

// A frame at `pos` in the format and baud already in use if it frames there;
// otherwise in the one whose run of frames covers the most cycles (a lone 300
// baud byte frames at 1200 too, as a start bit and three 0s, but its run is
// shorter), the baud in force and the order of FORMATS breaking ties.
std::optional<Framed> frame_at(const std::vector<uint8_t>& c, size_t pos, bool baud_300_in_force,
                               std::optional<std::pair<Format, bool>>& current) {
    // A start bit follows mark, or begins the region.
    if (pos > 0 && c[pos - 1] != SHORT) return std::nullopt;
    if (current) {
        if (const auto f = try_frame(c, pos, current->first, current->second)) {
            return Framed{f->first, f->second, current->first, current->second};
        }
    }
    size_t best = 0;
    std::optional<std::pair<Format, bool>> choice;
    for (const bool baud_300 : {baud_300_in_force, !baud_300_in_force}) {
        for (const Format& format : FORMATS) {
            const size_t covered = run_cycles(c, pos, format, baud_300);
            if (covered > best) {
                best = covered;
                choice = std::make_pair(format, baud_300);
            }
        }
    }
    if (!choice) return std::nullopt;
    current = choice;
    const auto f = try_frame(c, pos, choice->first, choice->second);
    return Framed{f->first, f->second, choice->first, choice->second};
}

// A region's cycles into chunks.
void classify_cycles(const std::vector<uint8_t>& c, ChunkStream& out) {
    size_t i = 0;
    bool explicit_bits = false;
    std::optional<std::pair<Format, bool>> current;  // the format and baud of the data run
    while (i < c.size()) {
        if (c[i] == SHORT) {
            size_t j = i;
            while (j < c.size() && c[j] == SHORT) ++j;
            const size_t run = j - i;
            if (explicit_bits && run < CARRIER_RUN_CYCLES) {
                // Ones among explicit bits; an odd short cycle left over is carrier.
                for (size_t k = 0; k + 1 < run; k += 2) out.bit(true);
                if (run % 2 != 0) {
                    explicit_bits = false;
                    out.carrier(1);
                }
            } else {
                explicit_bits = false;
                out.carrier(run);
            }
            i = j;
            continue;
        }
        if (const auto f = frame_at(c, i, out.baud_300(), current)) {
            explicit_bits = false;
            out.byte(f->value, f->format, f->baud_300);
            i += f->length;
            continue;
        }
        explicit_bits = true;
        current.reset();
        out.bit(false);
        ++i;
    }
}

}  // namespace

std::vector<UefChunk> classify_tape_edges(const EdgeTrack& track) {
    ChunkStream out;
    TapeTime region_end = 0;  // where the last region's last half-cycle ended
    std::vector<uint8_t> halves;
    std::vector<uint8_t> cycles;
    TapeTime last = 0;
    bool any = false;

    auto finish_region = [&](TapeTime last_edge) {
        if (halves.empty()) return;
        // The last half-cycle's length is unknown: it takes its partner's class.
        const uint8_t last_half = halves.back();
        halves.push_back(last_half);
        cycles.clear();
        size_t i = 0;
        while (i + 1 < halves.size()) {
            if (halves[i] == halves[i + 1]) {
                cycles.push_back(halves[i]);
                i += 2;
            } else {
                ++i;  // a lone half-cycle, absorbed
            }
        }
        classify_cycles(cycles, out);
        region_end = last_edge + static_cast<TapeTime>(halves.back() == LONG ? LONG_HALF : SHORT_HALF);
        halves.clear();
    };

    TapeTime region_start = 0;
    size_t region_edges = 0;
    track.for_each([&](const EdgeTrack::Edge& e) {
        if (any && e.time - last <= GAP_CLOCKS) {
            const double d = static_cast<double>(e.time - last);
            halves.push_back(std::fabs(d - SHORT_HALF) <= std::fabs(d - LONG_HALF) ? SHORT : LONG);
            ++region_edges;
        } else {
            // A silence before this edge ends the region before it.
            if (any) {
                if (region_edges > 1) finish_region(last);
                else halves.clear();  // a lone edge, absorbed into the silence
            }
            region_start = e.time;
            out.gap(static_cast<double>(region_start) - static_cast<double>(any ? region_end : 0));
            region_edges = 1;
            any = true;
        }
        last = e.time;
    });
    if (any && region_edges > 1) finish_region(last);
    // The silence after, to the tape's length at the nominal base.
    out.gap(static_cast<double>(track.length()) - out.written());
    return out.finish();
}

std::vector<uint8_t> encode_uef(const std::vector<UefChunk>& metadata, const std::vector<UefChunk>& tape,
                                bool gzip) {
    std::vector<uint8_t> out = {'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', 0, 10, 0};
    auto put = [&](const UefChunk& chunk) {
        put_u16(out, chunk.id);
        const auto n = static_cast<uint32_t>(chunk.body.size());
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(n >> (8 * i)));
        out.insert(out.end(), chunk.body.begin(), chunk.body.end());
    };
    for (const auto& chunk : metadata) put(chunk);
    for (const auto& chunk : tape) put(chunk);
    return gzip ? cassette_detail::deflate_gzip(out) : out;
}

}  // namespace beebium
