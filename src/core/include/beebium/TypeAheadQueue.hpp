// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#include "KeyboardMatrix.hpp"

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

namespace beebium {

// Type-ahead queue for typing strings at machine speed
//
// This class manages a queue of strings to be typed into the emulated BBC Micro.
// It operates as a state machine that is driven by the main emulator loop via
// tick(). Each character is typed with proper timing, including SHIFT handling
// for uppercase letters and shifted symbols.
//
// Thread Safety:
// The enqueue(), empty(), pending_characters(), and clear() methods are
// thread-safe and can be called from any thread (e.g., gRPC service thread).
// The tick() method should only be called from the emulator thread.
//
// Typical usage:
//   1. gRPC service calls enqueue() to add text
//   2. Emulator main loop calls tick() each cycle
//   3. Queue types characters as fast as the machine can handle
//   4. Client polls status via empty()/pending_characters()
//
class TypeAheadQueue {
public:
    // Default key-down and key-up durations, in CPU cycles at 2 MHz.
    //
    // The MOS scans the keyboard at 100 Hz and only registers a key that is
    // stable across two consecutive scans, so each phase must comfortably
    // exceed ~25 ms (measured floor; see tests/test_keyboard_pacing.cpp).
    // 80000 cycles = 40 ms gives ~1.6x margin over that floor while staying
    // well under the ~500 ms auto-repeat threshold. Hold and gap are
    // independent: a too-short hold drops keys, a too-short gap merges
    // repeated keys (the "OPALSS" failure). They are NOT two halves of one
    // budget -- coupling them was the cause of the old unreliability.
    static constexpr size_t DEFAULT_HOLD_CYCLES = 80000;  // 40 ms key-down
    static constexpr size_t DEFAULT_GAP_CYCLES = 80000;   // 40 ms key-up

    explicit TypeAheadQueue(KeyboardMatrix& keyboard);

    // Enqueue a string to type (thread-safe)
    // All characters must be typeable on BBC keyboard.
    // hold_cycles: cycles each key is held down; gap_cycles: cycles released
    // before the next key. Returns false if the string contains unmappable
    // characters.
    bool enqueue(std::string_view text,
                 size_t hold_cycles = DEFAULT_HOLD_CYCLES,
                 size_t gap_cycles = DEFAULT_GAP_CYCLES);

    // Called each machine cycle from the emulator main loop
    // Updates keyboard state based on timing.
    void tick();

    // Check if queue is empty (all strings typed) (thread-safe)
    bool empty() const;

    // Get number of characters remaining to type (thread-safe)
    size_t pending_characters() const;

    // Get number of strings in queue (thread-safe)
    size_t strings_queued() const;

    // Clear the queue, cancelling pending input (thread-safe)
    // Returns number of characters that were pending.
    size_t clear();

    // Monotonic counter bumped whenever the typing status changes: a character
    // consumed, a string started or finished, text enqueued, or the queue
    // cleared. A watcher compares successive values to know when there is
    // something new to report, without polling the status itself.
    uint64_t status_sequence() const {
        return status_sequence_.load(std::memory_order_acquire);
    }

private:
    // State machine states
    enum class State {
        Idle,       // No pending input, waiting for next string
        KeyDown,    // Key pressed (with SHIFT if needed), counting down cycles
        KeyUp       // Key released, counting down cycles before next char
    };

    // Pending entry in the queue
    struct QueueEntry {
        std::string text;
        size_t hold_cycles;
        size_t gap_cycles;
    };

    // Advance to next character or string (called with mutex held)
    void advance_to_next_char();

    // Release current key (and SHIFT if held)
    void release_current_key();

    KeyboardMatrix& keyboard_;

    // Queue of strings to type (protected by mutex)
    mutable std::mutex mutex_;
    std::queue<QueueEntry> queue_;

    // Current typing state (only accessed from emulator thread)
    State state_ = State::Idle;
    std::string current_text_;
    size_t current_index_ = 0;
    size_t current_hold_cycles_ = DEFAULT_HOLD_CYCLES;
    size_t current_gap_cycles_ = DEFAULT_GAP_CYCLES;
    size_t cycle_count_ = 0;

    // Bumped on every observable change; see status_sequence(). Atomic rather
    // than mutex-protected so a watcher can check for changes without
    // contending with the emulation thread on every poll.
    std::atomic<uint64_t> status_sequence_{0};

    // Record that the observable status changed.
    void note_status_change() {
        status_sequence_.fetch_add(1, std::memory_order_release);
    }

    // Current key state
    uint8_t current_ik_number_ = 0;
    bool current_needs_shift_ = false;
};

} // namespace beebium
