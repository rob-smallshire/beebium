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

#include "DiscDrive.hpp"

#include <span>

namespace beebium {

// The write-inactivity flush a disc controller owns.
//
// Once a controller stops writing to disc for a short settle time, every
// attached drive's dirty tracks are written to their host images and synced
// to storage, so a settled save survives a crash. The settle time is
// deliberately far shorter than a motor spin-down and independent of it: the
// flush always completes on the emulation thread long before a safe eject,
// which waits on the motor, could race it for the same disc.
class DiscWriteBack {
public:
    // About 250ms of 1MHz ticks.
    static constexpr int FLUSH_IDLE_TICKS = 250'000;

    // Record that a byte was just written to disc, restarting the settle time.
    void note_write() {
        pending_ = true;
        idle_ticks_ = 0;
    }

    // Advance one 1MHz tick, flushing the drives once writes have settled.
    void tick(std::span<DiscDrive* const> drives) {
        if (!pending_) return;
        if (++idle_ticks_ >= FLUSH_IDLE_TICKS) {
            for (DiscDrive* drive : drives) {
                if (drive) drive->flush_if_dirty(/*sync_to_disk=*/true);
            }
            pending_ = false;
            idle_ticks_ = 0;
        }
    }

private:
    bool pending_ = false;
    int idle_ticks_ = 0;
};

} // namespace beebium
