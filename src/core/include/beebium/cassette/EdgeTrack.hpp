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

#include "TapeSignal.hpp"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace beebium {

// The edges of a tape of fixed length, held exactly in tape time and editable
// over any span: the form a tape takes once it has been recorded on.
//
// Edges are kept in blocks. Within a block the levels alternate and each edge
// is the 16-bit difference from the one before, so an edge costs two bytes (a
// fully recorded C90, about 26 million edges, is about 52 MB). A new block
// starts where the level fails to alternate, after a silence longer than a
// 16-bit difference, and every BLOCK_EDGES edges, which bounds a seek's scan.
//
// Recording overwrites forwards from one point: begin_overwrite() splits the
// track there, erase_before() drops the old edges that pass under the head,
// write() appends the new ones, and end_overwrite() joins what is left.
class EdgeTrack {
public:
    static constexpr size_t BLOCK_EDGES = 65536;

    struct Edge {
        TapeTime time = 0;
        bool level = false;
    };

    // A position in the track: the edge at (block, index).
    struct Cursor {
        size_t block = 0;
        size_t index = 0;
        TapeTime time = 0;
    };

    explicit EdgeTrack(TapeTime length) : length_(length) {}

    TapeTime length() const { return length_; }
    size_t size() const { return edges_; }

    // Append an edge after the last; times strictly increase.
    void append(TapeTime time, bool level) { append_to(blocks_, time, level); ++edges_; }

    // --- Reading ---

    Cursor begin() const { return {0, 0, blocks_.empty() ? 0 : blocks_[0].first}; }

    bool valid(const Cursor& c) const { return c.block < blocks_.size(); }

    Edge at(const Cursor& c) const {
        const Block& b = blocks_[c.block];
        return {c.time, b.first_level != ((c.index & 1) != 0)};
    }

    void advance(Cursor& c) const {
        const Block& b = blocks_[c.block];
        if (c.index < b.deltas.size()) {
            c.time += b.deltas[c.index];
            ++c.index;
            return;
        }
        ++c.block;
        c.index = 0;
        if (c.block < blocks_.size()) c.time = blocks_[c.block].first;
    }

    // The first edge at or after `time`.
    Cursor find(TapeTime time) const {
        // The last block starting at or before `time`.
        size_t lo = 0;
        size_t hi = blocks_.size();
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (blocks_[mid].first <= time) lo = mid + 1;
            else hi = mid;
        }
        Cursor c{lo == 0 ? 0 : lo - 1, 0, 0};
        if (c.block >= blocks_.size()) return {blocks_.size(), 0, 0};
        c.time = blocks_[c.block].first;
        while (valid(c) && c.time < time) advance(c);
        return c;
    }

    template <typename F>
    void for_each(F&& f) const {
        for (Cursor c = begin(); valid(c); advance(c)) f(at(c));
    }

    // --- Overwriting ---

    // Split at `time`: edges before it stay, those at or after it wait to be
    // erased or kept.
    void begin_overwrite(TapeTime time) {
        const Cursor c = find(time);
        tail_.clear();
        if (valid(c)) {
            // The rest of the split block, then the blocks after it.
            Block& split = blocks_[c.block];
            Block rest;
            rest.first = c.time;
            rest.first_level = split.first_level != ((c.index & 1) != 0);
            rest.deltas.assign(split.deltas.begin() + static_cast<std::ptrdiff_t>(c.index), split.deltas.end());
            tail_.push_back(std::move(rest));
            for (size_t i = c.block + 1; i < blocks_.size(); ++i) tail_.push_back(std::move(blocks_[i]));
            if (c.index == 0) {
                blocks_.resize(c.block);
            } else {
                split.deltas.resize(c.index - 1);
                blocks_.resize(c.block + 1);
            }
            refresh_last();
        }
        tail_cursor_ = {0, 0, tail_.empty() ? 0 : tail_[0].first};
        overwriting_ = true;
    }

    // Drop the waiting edges before `time`.
    void erase_before(TapeTime time) {
        while (tail_cursor_.block < tail_.size() && tail_cursor_.time < time) {
            const Block& b = tail_[tail_cursor_.block];
            if (tail_cursor_.index < b.deltas.size()) {
                tail_cursor_.time += b.deltas[tail_cursor_.index];
                ++tail_cursor_.index;
            } else {
                ++tail_cursor_.block;
                tail_cursor_.index = 0;
                if (tail_cursor_.block < tail_.size()) tail_cursor_.time = tail_[tail_cursor_.block].first;
            }
            --edges_;
        }
    }

    // A new edge at the head, after every edge already written.
    void write(TapeTime time, bool level) { append(time, level); }

    // Join the waiting edges that were not erased back onto the track.
    void end_overwrite() {
        if (!overwriting_) return;
        overwriting_ = false;
        if (tail_cursor_.block < tail_.size()) {
            // The partly erased block, edge by edge, then whole blocks.
            const Block& b = tail_[tail_cursor_.block];
            TapeTime t = tail_cursor_.time;
            bool level = b.first_level != ((tail_cursor_.index & 1) != 0);
            append_to(blocks_, t, level);
            for (size_t i = tail_cursor_.index; i < b.deltas.size(); ++i) {
                t += b.deltas[i];
                level = !level;
                append_to(blocks_, t, level);
            }
            for (size_t i = tail_cursor_.block + 1; i < tail_.size(); ++i) blocks_.push_back(std::move(tail_[i]));
            refresh_last();
        }
        tail_.clear();
    }

    bool overwriting() const { return overwriting_; }

private:
    struct Block {
        TapeTime first = 0;
        bool first_level = false;
        std::vector<uint16_t> deltas;  // the time from each edge to the next

        TapeTime last() const {
            TapeTime t = first;
            for (uint16_t d : deltas) t += d;
            return t;
        }
    };

    void append_to(std::vector<Block>& blocks, TapeTime time, bool level) {
        if (!blocks.empty()) {
            Block& b = blocks.back();
            const bool last_level = b.first_level != ((b.deltas.size() & 1) != 0);
            const TapeTime gap = time - last_time_;
            if (level != last_level && gap <= std::numeric_limits<uint16_t>::max() &&
                b.deltas.size() + 1 < BLOCK_EDGES) {
                b.deltas.push_back(static_cast<uint16_t>(gap));
                last_time_ = time;
                return;
            }
        }
        Block b;
        b.first = time;
        b.first_level = level;
        blocks.push_back(std::move(b));
        last_time_ = time;
    }

    void refresh_last() { last_time_ = blocks_.empty() ? 0 : blocks_.back().last(); }

    TapeTime length_;
    size_t edges_ = 0;
    std::vector<Block> blocks_;
    TapeTime last_time_ = 0;  // the last edge of blocks_

    bool overwriting_ = false;
    std::vector<Block> tail_;
    Cursor tail_cursor_;
};

}  // namespace beebium
