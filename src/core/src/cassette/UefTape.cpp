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

#include "beebium/cassette/UefTape.hpp"

#include "Inflate.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace beebium {

namespace {

std::string chunk_id_text(uint16_t id) {
    char text[8];
    std::snprintf(text, sizeof(text), "&%04X", id);
    return text;
}

uint16_t read_u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t read_u24(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16); }
uint32_t read_u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
float read_f32(const uint8_t* p) {
    // UEF floats are IEEE 754 single precision, little-endian, as every host
    // Beebium builds for is.
    float f;
    std::memcpy(&f, p, sizeof(f));
    return f;
}

std::string read_string(const std::vector<uint8_t>& body, size_t from = 0) {
    std::string s;
    for (size_t i = from; i < body.size() && body[i] != 0; ++i) s.push_back(static_cast<char>(body[i]));
    return s;
}

// True if an &0000 origin string names MakeUEF with a version below 2.4, whose
// &0104 chunks have odd and even parity exchanged.
bool origin_needs_parity_swap(const std::string& origin) {
    const auto at = origin.find("MakeUEF");
    if (at == std::string::npos) return false;
    size_t i = at + 7;
    while (i < origin.size() && !(origin[i] >= '0' && origin[i] <= '9')) ++i;
    if (i >= origin.size()) return false;
    int major = 0;
    while (i < origin.size() && origin[i] >= '0' && origin[i] <= '9') major = major * 10 + (origin[i++] - '0');
    int minor = 0;
    if (i < origin.size() && origin[i] == '.') {
        ++i;
        if (i < origin.size() && origin[i] >= '0' && origin[i] <= '9') minor = origin[i] - '0';
    }
    return major < 2 || (major == 2 && minor < 4);
}

std::string target_machine_text(uint8_t value) {
    switch (value >> 4) {
        case 0: return "BBC Model A";
        case 1: return "Acorn Electron";
        case 2: return "BBC Model B";
        case 3: return "BBC Master";
        case 4: return "Acorn Atom";
        default: return "";
    }
}

// Renders tape chunks into edges, carrying the state from chunk to chunk.
class Renderer {
public:
    Renderer(UefTape::RenderState& state, std::vector<TapeItem>* out) : s_(state), out_(out) {}

    // One half-cycle at `hz` cycles a second. A forced level starts a lone
    // pulse at that level; otherwise the level alternates, or after a gap
    // starts at the phase's level with the first half shortened by the phase.
    void half_cycle(double hz, std::optional<bool> forced_level = std::nullopt) {
        double length = ULA_CLOCK_HZ / (2.0 * hz);
        bool level;
        if (forced_level) {
            level = *forced_level;
            s_.after_gap = false;
        } else if (s_.after_gap) {
            level = s_.phase_degrees >= 180;
            length *= 1.0 - static_cast<double>(s_.phase_degrees % 180) / 180.0;
            s_.after_gap = false;
        } else {
            level = !s_.level;
        }
        emit(TapeItem::edge(static_cast<TapeTime>(s_.exact_time), level));
        s_.level = level;
        s_.exact_time += length;
    }

    void cycle(double hz) {
        half_cycle(hz);
        half_cycle(hz);
    }

    void bit(bool one) {
        const unsigned cycles = (s_.encoding_300 ? 4u : 1u) * (one ? 2u : 1u);
        const double hz = one ? 2.0 * s_.base_hz : s_.base_hz;
        for (unsigned i = 0; i < cycles; ++i) cycle(hz);
    }

    // start, data bits LSB first, parity if any, stop bits, then an extra
    // short cycle if asked.
    void frame(uint8_t value, unsigned data_bits, char parity, unsigned stop_bits, bool extra_short) {
        bit(false);
        unsigned ones = 0;
        for (unsigned i = 0; i < data_bits; ++i) {
            const bool b = ((value >> i) & 1) != 0;
            ones += b ? 1 : 0;
            bit(b);
        }
        if (parity == 'E') bit((ones & 1) != 0);
        if (parity == 'O') bit((ones & 1) == 0);
        for (unsigned i = 0; i < stop_bits; ++i) bit(true);
        if (extra_short) cycle(2.0 * s_.base_hz);
    }

    void gap_seconds(double seconds) {
        if (!(seconds > 0.0)) return;
        s_.exact_time += seconds * ULA_CLOCK_HZ;
        emit(TapeItem::gap(static_cast<TapeTime>(s_.exact_time)));
        s_.after_gap = true;
    }

    UefTape::RenderState& state() { return s_; }

private:
    void emit(const TapeItem& item) {
        if (out_) out_->push_back(item);
    }

    UefTape::RenderState& s_;
    std::vector<TapeItem>* out_;
};

}  // namespace

std::unique_ptr<UefTape> UefTape::open(std::span<const uint8_t> bytes, const std::string& name) {
    std::vector<uint8_t> inflated;
    if (cassette_detail::has_gzip_magic(bytes)) {
        auto result = cassette_detail::inflate_all(bytes, cassette_detail::Container::Gzip);
        if (!result) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " is a damaged gzip file.");
        }
        inflated = std::move(*result);
        bytes = inflated;
    }

    static constexpr char MAGIC[] = "UEF File!";  // ten bytes with its NUL
    if (bytes.size() < 12 || std::memcmp(bytes.data(), MAGIC, 10) != 0) {
        throw TapeImageError(TapeImageError::Kind::UnsupportedFormat, "The file " + name + " is not a UEF tape image.");
    }
    const unsigned minor = bytes[10];
    const unsigned major = bytes[11];
    if (major != 0) {
        throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name + " is a UEF of version " + std::to_string(major) +
                                 "." + std::to_string(minor) + ", which Beebium does not read.");
    }

    std::unique_ptr<UefTape> tape(new UefTape());
    tape->name_ = name;

    size_t pos = 12;
    while (pos < bytes.size()) {
        if (pos + 6 > bytes.size()) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " ends part way through a chunk header.");
        }
        Chunk chunk;
        chunk.id = read_u16(&bytes[pos]);
        const uint32_t length = read_u32(&bytes[pos + 2]);
        pos += 6;
        if (length > bytes.size() - pos) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name + " ends part way through chunk " +
                                     chunk_id_text(chunk.id) + ".");
        }
        chunk.body.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                          bytes.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += length;

        if (chunk.id >= 0x0200) {
            throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name + " contains chunk " + chunk_id_text(chunk.id) +
                                     ", which holds a disc, a ROM or a machine's state rather than "
                                     "tape, so Beebium does not read it.");
        }
        switch (chunk.id) {
            case 0x0000:
                tape->metadata_.origin = read_string(chunk.body);
                if (origin_needs_parity_swap(tape->metadata_.origin)) tape->makeuef_parity_swap_ = true;
                break;
            case 0x0001:
                tape->metadata_.instructions = read_string(chunk.body);
                break;
            case 0x0005:
                if (!chunk.body.empty()) tape->metadata_.target_machine = target_machine_text(chunk.body[0]);
                break;
            case 0x0009:
                tape->metadata_.title = read_string(chunk.body);
                break;
            case 0x0120:
                tape->metadata_.position_markers.push_back(read_string(chunk.body));
                break;
            case 0x0130:
                break;
            case 0x0131:
                if (chunk.body.size() > 2) {
                    tape->metadata_.position_markers.push_back(read_string(chunk.body, 2));
                }
                break;
            case 0x0003: case 0x0006: case 0x0007: case 0x0008: case 0x000A:
            case 0x0101: case 0x0103: case 0x0105:
                break;
            case 0x0100: case 0x0104: case 0x0110: case 0x0111: case 0x0112: case 0x0113:
            case 0x0114: case 0x0115: case 0x0116: case 0x0117:
                tape->chunks_.push_back(std::move(chunk));
                break;
            default:
                std::cerr << "UEF: skipping chunk " << chunk_id_text(chunk.id) << " in " << name << "\n";
                break;
        }
    }

    // Render once to find where each chunk starts and to refuse bad chunks.
    RenderState state;
    tape->chunk_starts_.reserve(tape->chunks_.size());
    for (size_t i = 0; i < tape->chunks_.size(); ++i) {
        tape->chunk_starts_.push_back(state);
        tape->render(i, state, nullptr);
    }
    tape->end_state_ = state;
    return tape;
}

void UefTape::render(size_t index, RenderState& state, std::vector<TapeItem>* out) const {
    const Chunk& chunk = chunks_[index];
    const std::vector<uint8_t>& b = chunk.body;
    Renderer r(state, out);
    auto need = [&](size_t n) {
        if (b.size() < n) {
            throw TapeImageError(TapeImageError::Kind::Unreadable, "The file " + name_ + " has a chunk " + chunk_id_text(chunk.id) +
                                     " that is too short.");
        }
    };

    switch (chunk.id) {
        case 0x0100:
            for (uint8_t value : b) r.frame(value, 8, 'N', 1, false);
            break;

        case 0x0104: {
            need(3);
            const unsigned data_bits = b[0];
            if (data_bits != 7 && data_bits != 8) {
                throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name_ + " has a chunk &0104 with " +
                                         std::to_string(data_bits) +
                                         " data bits a packet, which Beebium does not read.");
            }
            char parity = static_cast<char>(b[1]);
            if (parity != 'N' && parity != 'E' && parity != 'O') {
                throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name_ + " has a chunk &0104 with an unknown parity.");
            }
            if (makeuef_parity_swap_) {
                if (parity == 'E') parity = 'O';
                else if (parity == 'O') parity = 'E';
            }
            const auto stop = static_cast<int8_t>(b[2]);
            const unsigned stop_bits = static_cast<unsigned>(stop < 0 ? -stop : stop);
            for (size_t i = 3; i < b.size(); ++i) {
                r.frame(b[i], data_bits, parity, stop_bits, stop < 0);
            }
            break;
        }

        case 0x0110: {
            need(2);
            const unsigned cycles = read_u16(b.data());
            for (unsigned i = 0; i < cycles; ++i) r.cycle(2.0 * state.base_hz);
            break;
        }

        case 0x0111: {
            need(4);
            const unsigned before = read_u16(b.data());
            const unsigned after = read_u16(b.data() + 2);
            for (unsigned i = 0; i < before; ++i) r.cycle(2.0 * state.base_hz);
            r.frame(0xAA, 8, 'N', 1, false);
            for (unsigned i = 0; i < after; ++i) r.cycle(2.0 * state.base_hz);
            break;
        }

        case 0x0112: {
            need(2);
            r.gap_seconds(read_u16(b.data()) / (2.0 * state.base_hz));
            break;
        }

        case 0x0113: {
            need(4);
            const double hz = read_f32(b.data());
            if (!(hz >= 100.0 && hz <= 10000.0)) {
                std::ostringstream text;
                text << "The file " << name_ << " changes the base frequency to " << hz
                     << " Hz, which is outside 100 to 10000 Hz.";
                throw TapeImageError(TapeImageError::Kind::InvalidImage, text.str());
            }
            state.base_hz = hz;
            break;
        }

        case 0x0114: {
            need(5);
            const uint32_t count = read_u24(b.data());
            const bool first_pulse = b[3] == 'P';
            const bool last_pulse = b[4] == 'P';
            need(5 + (count + 7) / 8);
            for (uint32_t i = 0; i < count; ++i) {
                // Least significant bit first; 1 is a long cycle at the base
                // frequency, 0 a short one at twice it.
                const bool long_cycle = ((b[5 + i / 8] >> (i % 8)) & 1) != 0;
                const double hz = long_cycle ? state.base_hz : 2.0 * state.base_hz;
                if (i == 0 && first_pulse) {
                    r.half_cycle(hz, true);         // a lone high pulse
                } else if (i + 1 == count && last_pulse) {
                    r.half_cycle(hz, false);        // a lone low pulse
                } else {
                    r.cycle(hz);
                }
            }
            break;
        }

        case 0x0115:
            need(2);
            state.phase_degrees = static_cast<uint16_t>(read_u16(b.data()) % 360);
            break;

        case 0x0116:
            need(4);
            r.gap_seconds(read_f32(b.data()));
            break;

        case 0x0117: {
            need(2);
            const unsigned baud = read_u16(b.data());
            if (baud != 300 && baud != 1200) {
                throw TapeImageError(TapeImageError::Kind::InvalidImage, "The file " + name_ + " selects a data encoding of " +
                                         std::to_string(baud) + " baud, which is neither 300 nor 1200.");
            }
            state.encoding_300 = baud == 300;
            break;
        }

        default:
            break;
    }
}

TapeItem UefTape::next() {
    while (buffer_pos_ >= buffer_.size()) {
        if (next_chunk_ >= chunks_.size()) {
            return TapeItem::end(static_cast<TapeTime>(end_state_.exact_time));
        }
        buffer_.clear();
        buffer_pos_ = 0;
        RenderState state = chunk_starts_[next_chunk_];
        render(next_chunk_, state, &buffer_);
        ++next_chunk_;
    }
    return buffer_[buffer_pos_++];
}

void UefTape::seek(TapeTime time) {
    buffer_.clear();
    buffer_pos_ = 0;
    // The last chunk starting at or before `time`.
    size_t index = 0;
    while (index + 1 < chunk_starts_.size() &&
           static_cast<TapeTime>(chunk_starts_[index + 1].exact_time) <= time) {
        ++index;
    }
    if (chunks_.empty() || time >= static_cast<TapeTime>(end_state_.exact_time)) {
        next_chunk_ = chunks_.size();
        return;
    }
    RenderState state = chunk_starts_[index];
    render(index, state, &buffer_);
    next_chunk_ = index + 1;
    while (buffer_pos_ < buffer_.size()) {
        const TapeItem& item = buffer_[buffer_pos_];
        const bool before = item.type == TapeItem::Type::Gap ? item.time <= time : item.time < time;
        if (!before) break;
        ++buffer_pos_;
    }
}

std::optional<TapeTime> UefTape::duration() const {
    return static_cast<TapeTime>(end_state_.exact_time);
}

}  // namespace beebium
