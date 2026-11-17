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

// A tape writer for tests: build a tape from primitives (carrier, gaps, bytes,
// the 300/1200 encoding, raw chunks) and write it as a UEF or as a 48 kHz CSW,
// so a test can make the tape it needs the way disc tests make their discs.
//
// The MOS cassette block format (sync &2A, header, header CRC, data, data CRC,
// CRC-16/XMODEM high byte first) and the MOS's leader and gap timings
// (5.1 s before the first block, 0.9 s between, 5.1 s after) follow the
// cassette research findings, sections 2.3 and 2.4.

#include <beebium/cassette/UefTape.hpp>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace beebium::test {

// CRC-16/XMODEM: polynomial &1021, initial value 0, unreflected.
inline uint16_t crc16_xmodem(std::span<const uint8_t> bytes) {
    uint16_t crc = 0;
    for (uint8_t byte : bytes) {
        crc = static_cast<uint16_t>(crc ^ (byte << 8));
        for (int i = 0; i < 8; ++i) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// The bytes of one MOS cassette block: &2A, the header, its CRC, then the data
// and its CRC (absent when the block is empty).
inline std::vector<uint8_t> mos_block(const std::string& name, uint32_t load, uint32_t exec,
                                      uint16_t number, std::span<const uint8_t> data, uint8_t flags,
                                      bool corrupt_data_crc = false) {
    std::vector<uint8_t> header;
    for (char c : name.substr(0, 10)) header.push_back(static_cast<uint8_t>(c));
    header.push_back(0);
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) header.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    put32(load);
    put32(exec);
    header.push_back(static_cast<uint8_t>(number));
    header.push_back(static_cast<uint8_t>(number >> 8));
    header.push_back(static_cast<uint8_t>(data.size()));
    header.push_back(static_cast<uint8_t>(data.size() >> 8));
    header.push_back(flags);
    put32(0);  // spare

    std::vector<uint8_t> block{0x2A};
    block.insert(block.end(), header.begin(), header.end());
    const uint16_t header_crc = crc16_xmodem(header);
    block.push_back(static_cast<uint8_t>(header_crc >> 8));
    block.push_back(static_cast<uint8_t>(header_crc));
    if (!data.empty()) {
        block.insert(block.end(), data.begin(), data.end());
        uint16_t data_crc = crc16_xmodem(data);
        if (corrupt_data_crc) data_crc ^= 0x5A5A;
        block.push_back(static_cast<uint8_t>(data_crc >> 8));
        block.push_back(static_cast<uint8_t>(data_crc));
    }
    return block;
}

struct FileOptions {
    bool short_leader = false;        // 0.62 s before, 0.25 s between (the Welcome tape)
    uint16_t baud = 1200;
    int corrupt_data_crc_block = -1;  // this block's data CRC is wrong
    std::map<int, uint16_t> renumber; // block index -> block number written
};

struct CswOptions {
    unsigned version = 2;        // 1 (v1.01, RLE) or 2
    bool z_rle = true;           // v2 only
    bool initial_high = true;    // matches a UEF's default 180 degree phase
    uint32_t sample_rate = 48000;
};

// A WAV of a tape's signal: each half-cycle between two edges rendered as a
// half of a sine (or a square, or a sine shifted by 90 degrees, which is a
// cosine and still continuous at each edge), at the edge's level. A silence
// (more than 1100 clocks with no edge) follows a nominal 2400 Hz half-cycle.
struct WavOptions {
    enum class Shape { Sine, Square, Shifted90 };
    uint32_t rate = 48000;
    unsigned bits = 16;
    unsigned channels = 1;
    bool is_float = false;
    bool extensible = false;           // WAVE_FORMAT_EXTENSIBLE
    double amplitude = 0.5;
    Shape shape = Shape::Sine;
    double noise = 0.0;                // uniform, peak, added to the signal
    unsigned signal_channel = 0;       // 0 left, 1 right
    double other_amplitude = 0.0;      // the other channel: the signal at this amplitude
};

class TapeBuilder {
public:
    // --- Primitives ---

    TapeBuilder& origin(const std::string& text) {
        ops_.push_back({Op::Origin, 0, 0.0, {text.begin(), text.end()}});
        return *this;
    }
    // Carrier: `cycles` cycles at twice the base frequency (2400 Hz).
    TapeBuilder& carrier(uint32_t cycles) {
        ops_.push_back({Op::Carrier, cycles, 0.0, {}});
        return *this;
    }
    // Silence (a &0116 floating-point gap; one long pulse in a CSW).
    TapeBuilder& gap(double seconds) {
        ops_.push_back({Op::Gap, 0, seconds, {}});
        return *this;
    }
    // Bytes as 8N1 frames (&0100).
    TapeBuilder& bytes_8n1(std::span<const uint8_t> bytes) {
        ops_.push_back({Op::Bytes, 0, 0.0, {bytes.begin(), bytes.end()}});
        return *this;
    }
    // The data encoding from here on: 1200 (1 or 2 cycles a bit) or 300 (4 or 8).
    TapeBuilder& baud(uint16_t rate) {
        ops_.push_back({Op::Baud, rate, 0.0, {}});
        return *this;
    }
    // A raw UEF chunk (UEF output only; a CSW cannot carry it).
    TapeBuilder& raw_chunk(uint16_t id, std::vector<uint8_t> body) {
        ops_.push_back({Op::Raw, id, 0.0, std::move(body)});
        return *this;
    }
    // One MOS block as 8N1 bytes.
    TapeBuilder& block(const std::string& name, uint32_t load, uint32_t exec, uint16_t number,
                       std::span<const uint8_t> data, uint8_t flags, bool corrupt_data_crc = false) {
        const auto bytes = mos_block(name, load, exec, number, data, flags, corrupt_data_crc);
        return bytes_8n1(bytes);
    }

    // --- A whole file, as MOS 1.20 saves it ---

    TapeBuilder& file(const std::string& name, uint32_t load, uint32_t exec,
                      std::span<const uint8_t> data, const FileOptions& options) {
        const uint32_t leader = options.short_leader ? 1488 : 12240;   // 0.62 s / 5.1 s
        const uint32_t between = options.short_leader ? 600 : 2160;    // 0.25 s / 0.9 s
        if (options.baud != 1200) baud(options.baud);
        carrier(leader);
        const size_t blocks = data.empty() ? 1 : (data.size() + 255) / 256;
        for (size_t i = 0; i < blocks; ++i) {
            const size_t from = i * 256;
            const size_t length = data.empty() ? 0 : std::min<size_t>(256, data.size() - from);
            const bool last = i + 1 == blocks;
            const auto renumbered = options.renumber.find(static_cast<int>(i));
            const uint16_t number = renumbered != options.renumber.end()
                ? renumbered->second : static_cast<uint16_t>(i);
            block(name, load, exec, number, data.subspan(from, length), last ? 0x80 : 0x00,
                  static_cast<int>(i) == options.corrupt_data_crc_block);
            carrier(last ? 12240 : between);
        }
        return *this;
    }

    // --- Output ---

    std::vector<uint8_t> uef(bool gzip = false) const {
        std::vector<uint8_t> out = {'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', 0, 10, 0};
        for (const auto& op : ops_) {
            switch (op.kind) {
                case Op::Origin: {
                    std::vector<uint8_t> body = op.bytes;
                    body.push_back(0);
                    chunk(out, 0x0000, body);
                    break;
                }
                case Op::Carrier:
                    for (uint32_t left = op.value; left > 0;) {
                        const uint32_t n = std::min<uint32_t>(left, 65535);
                        chunk(out, 0x0110, {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8)});
                        left -= n;
                    }
                    break;
                case Op::Gap: {
                    const float seconds = static_cast<float>(op.seconds);
                    std::vector<uint8_t> body(4);
                    std::memcpy(body.data(), &seconds, 4);
                    chunk(out, 0x0116, body);
                    break;
                }
                case Op::Bytes:
                    chunk(out, 0x0100, op.bytes);
                    break;
                case Op::Baud:
                    chunk(out, 0x0117, {static_cast<uint8_t>(op.value), static_cast<uint8_t>(op.value >> 8)});
                    break;
                case Op::Raw:
                    chunk(out, static_cast<uint16_t>(op.value), op.bytes);
                    break;
            }
        }
        return gzip ? compress(out, true) : out;
    }

    // At 48 kHz a 2400 Hz half-cycle is exactly 10 samples and a 1200 Hz one
    // exactly 20, so the CSW's edges match the UEF's within a ULA clock.
    std::vector<uint8_t> csw(const CswOptions& options = {}) const {
        std::vector<uint32_t> pulses;
        bool encoding_300 = false;
        const uint32_t short_half = options.sample_rate / 4800;
        const uint32_t long_half = options.sample_rate / 2400;
        auto cycle = [&](uint32_t half) { pulses.push_back(half); pulses.push_back(half); };
        auto bit = [&](bool one) {
            const unsigned cycles = (encoding_300 ? 4u : 1u) * (one ? 2u : 1u);
            for (unsigned i = 0; i < cycles; ++i) cycle(one ? short_half : long_half);
        };
        for (const auto& op : ops_) {
            switch (op.kind) {
                case Op::Carrier:
                    for (uint32_t i = 0; i < op.value; ++i) cycle(short_half);
                    break;
                case Op::Gap:
                    pulses.push_back(static_cast<uint32_t>(op.seconds * options.sample_rate + 0.5));
                    break;
                case Op::Bytes:
                    for (uint8_t value : op.bytes) {
                        bit(false);
                        for (int i = 0; i < 8; ++i) bit(((value >> i) & 1) != 0);
                        bit(true);
                    }
                    break;
                case Op::Baud:
                    encoding_300 = op.value == 300;
                    break;
                case Op::Origin:
                case Op::Raw:
                    break;
            }
        }

        std::vector<uint8_t> data;
        for (uint32_t pulse : pulses) {
            if (pulse <= 255) {
                data.push_back(static_cast<uint8_t>(pulse));
            } else {
                data.push_back(0);
                for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(pulse >> (8 * i)));
            }
        }

        std::vector<uint8_t> out(23);
        std::memcpy(out.data(), "Compressed Square Wave\x1A", 23);
        auto put16 = [&](uint32_t v) { out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8)); };
        auto put32 = [&](uint32_t v) { put16(v & 0xFFFF); put16(v >> 16); };
        const uint8_t flags = options.initial_high ? 0x01 : 0x00;
        if (options.version == 1) {
            out.push_back(1);
            out.push_back(1);
            put16(options.sample_rate);
            out.push_back(1);      // RLE
            out.push_back(flags);
            out.insert(out.end(), {0, 0, 0});
            out.insert(out.end(), data.begin(), data.end());
        } else {
            out.push_back(2);
            out.push_back(0);
            put32(options.sample_rate);
            put32(static_cast<uint32_t>(pulses.size()));
            out.push_back(options.z_rle ? 2 : 1);
            out.push_back(flags);
            out.push_back(0);      // no header extension
            const char application[16] = "beebium-tests";
            out.insert(out.end(), application, application + 16);
            const auto payload = options.z_rle ? compress(data, false) : data;
            out.insert(out.end(), payload.begin(), payload.end());
        }
        return out;
    }

    // The tape as a WAV (see WavOptions).
    std::vector<uint8_t> wav(const WavOptions& options = {}) const { return wav_of_uef(uef(), options); }

    static std::vector<uint8_t> wav_of_uef(const std::vector<uint8_t>& uef_bytes, const WavOptions& o) {
        const double clocks_per_second = 16'000'000.0 / 13.0;
        auto tape = beebium::UefTape::open(uef_bytes, "builder.uef");
        struct Half { double start, length; double sign; };
        std::vector<Half> halves;
        std::vector<std::pair<double, bool>> edges;
        for (auto item = tape->next(); item.type != beebium::TapeItem::Type::End; item = tape->next()) {
            if (item.type == beebium::TapeItem::Type::Edge) edges.emplace_back(static_cast<double>(item.time), item.level);
        }
        const double length = static_cast<double>(*tape->duration());
        constexpr double SHORT_HALF = 16'000'000.0 / 13.0 / 4800.0;
        for (size_t i = 0; i < edges.size(); ++i) {
            double half = i + 1 < edges.size() ? edges[i + 1].first - edges[i].first : SHORT_HALF;
            if (half > 1100.0) half = SHORT_HALF;
            halves.push_back({edges[i].first, half, edges[i].second ? 1.0 : -1.0});
        }

        const auto samples = static_cast<uint64_t>(std::ceil(length * o.rate / clocks_per_second));
        uint32_t seed = 12345;
        auto noise = [&] {
            seed = seed * 1664525u + 1013904223u;
            return (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * o.noise;
        };
        std::vector<uint8_t> data;
        const unsigned bytes_per_sample = o.bits / 8;
        data.reserve(samples * o.channels * bytes_per_sample);
        auto put = [&](double v) {
            v = std::clamp(v, -1.0, 1.0 - 1.0 / 2147483648.0);
            if (o.is_float) {
                const float f = static_cast<float>(v);
                uint8_t b[4];
                std::memcpy(b, &f, 4);
                data.insert(data.end(), b, b + 4);
                return;
            }
            switch (o.bits) {
                case 8: data.push_back(static_cast<uint8_t>(std::lround(v * 127.0) + 128)); break;
                case 16: {
                    const auto s = static_cast<int16_t>(std::lround(v * 32767.0));
                    data.push_back(static_cast<uint8_t>(s));
                    data.push_back(static_cast<uint8_t>(s >> 8));
                    break;
                }
                case 24: {
                    const auto s = static_cast<int32_t>(std::lround(v * 8388607.0));
                    for (int i = 0; i < 3; ++i) data.push_back(static_cast<uint8_t>(s >> (8 * i)));
                    break;
                }
                default: {
                    const auto s = static_cast<int32_t>(std::llround(v * 2147483647.0));
                    for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(s >> (8 * i)));
                    break;
                }
            }
        };
        size_t h = 0;
        for (uint64_t k = 0; k < samples; ++k) {
            const double t = static_cast<double>(k) * clocks_per_second / o.rate;
            while (h + 1 < halves.size() && halves[h + 1].start <= t) ++h;
            double v = 0.0;
            if (!halves.empty() && t >= halves[h].start && t < halves[h].start + halves[h].length) {
                const double u = (t - halves[h].start) / halves[h].length;
                switch (o.shape) {
                    case WavOptions::Shape::Sine: v = std::sin(std::numbers::pi * u); break;
                    case WavOptions::Shape::Square: v = 1.0; break;
                    case WavOptions::Shape::Shifted90: v = std::cos(std::numbers::pi * u); break;
                }
                v *= halves[h].sign;
            }
            const double signal = v * o.amplitude;
            for (unsigned c = 0; c < o.channels; ++c) {
                put((c == o.signal_channel ? signal : v * o.other_amplitude) + noise());
            }
        }
        return wav_file(data, o);
    }

    static std::vector<uint8_t> wav_file(const std::vector<uint8_t>& data, const WavOptions& o) {
        std::vector<uint8_t> out;
        auto u16 = [&](uint32_t v) { out.push_back(static_cast<uint8_t>(v)); out.push_back(static_cast<uint8_t>(v >> 8)); };
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        auto tag = [&](const char* s) { out.insert(out.end(), s, s + 4); };
        const uint32_t fmt_length = o.extensible ? 40 : 16;
        tag("RIFF");
        u32(static_cast<uint32_t>(4 + 8 + fmt_length + 8 + data.size()));
        tag("WAVE");
        tag("fmt ");
        u32(fmt_length);
        const uint16_t format = o.is_float ? 3 : 1;
        u16(o.extensible ? 0xFFFE : format);
        u16(o.channels);
        u32(o.rate);
        const unsigned block = o.channels * o.bits / 8;
        u32(o.rate * block);
        u16(block);
        u16(o.bits);
        if (o.extensible) {
            u16(22);
            u16(o.bits);
            u32(o.channels == 2 ? 3 : 4);
            u16(format);
            static const uint8_t GUID_TAIL[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                  0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
            out.insert(out.end(), GUID_TAIL, GUID_TAIL + 14);
        }
        tag("data");
        u32(static_cast<uint32_t>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }

    static void write(const std::filesystem::path& filepath, const std::vector<uint8_t>& bytes) {
        std::ofstream file(filepath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("could not write " + filepath.string());
    }

private:
    struct Op {
        enum Kind { Origin, Carrier, Gap, Bytes, Baud, Raw } kind;
        uint32_t value;
        double seconds;
        std::vector<uint8_t> bytes;
    };

    static void chunk(std::vector<uint8_t>& out, uint16_t id, const std::vector<uint8_t>& body) {
        out.push_back(static_cast<uint8_t>(id));
        out.push_back(static_cast<uint8_t>(id >> 8));
        const auto length = static_cast<uint32_t>(body.size());
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(length >> (8 * i)));
        out.insert(out.end(), body.begin(), body.end());
    }

    // Deflate as a gzip member or a zlib stream.
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input, bool gzip) {
        z_stream stream{};
        if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, gzip ? 16 + MAX_WBITS : MAX_WBITS, 8,
                         Z_DEFAULT_STRATEGY) != Z_OK) {
            throw std::runtime_error("deflateInit2 failed");
        }
        std::vector<uint8_t> out(deflateBound(&stream, static_cast<uLong>(input.size())) + 32);
        stream.next_in = const_cast<Bytef*>(input.data());
        stream.avail_in = static_cast<uInt>(input.size());
        stream.next_out = out.data();
        stream.avail_out = static_cast<uInt>(out.size());
        if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
            deflateEnd(&stream);
            throw std::runtime_error("deflate failed");
        }
        out.resize(stream.total_out);
        deflateEnd(&stream);
        return out;
    }

    std::vector<Op> ops_;
};

}  // namespace beebium::test
