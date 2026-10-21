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

#include "DiscControllerInterface.hpp"
#include "DiscDrive.hpp"
#include "Intel8271.hpp"

#include <cstdint>
#include <string_view>

namespace beebium {

// The Acorn 8271 disc interface for the BBC Model B: the original disc
// upgrade, an Intel 8271 in the FDC socket.
//
// Address mapping (within the socket's 32-byte window at &FE80-&FE9F,
// decoded on A2..A0 only, so the eight addresses repeat four times):
//
//   Offset 0: Status (read) / Command (write)
//   Offset 1: Result (read) / Parameter (write)
//   Offset 2: Byte counter MSB (read) / Reset (write)
//   Offset 3: Byte counter LSB (read)
//   Offset 4-7: Data register, reached by DACK (A1:A0 ignored)
//
// There is no drive control latch: drive select, side, motor and head load
// all come from the 8271's own drive output port. The 8271's INT output
// drives the 6502 NMI, for each data byte and for each completion; DRQ is
// not wired, so only non-DMA transfers work.
//
// With an 8271 fitted, &FE80 reads as a status register whose bits 1:0 are
// always 0, which is how Acorn DFS 1.20 tells it from a 1770 board (whose
// write-only latch reads &FF there).
class Intel8271DiscController : public DiscControllerInterface {
public:
    Intel8271DiscController() = default;
    ~Intel8271DiscController() override = default;

    // =========================================================================
    // MemoryMappedDevice interface
    // =========================================================================

    uint8_t read(uint16_t offset) override {
        offset &= 0x07;
        if (offset >= 4) return fdc_.read_data();
        return fdc_.read_register(static_cast<uint8_t>(offset));
    }

    uint8_t peek(uint16_t offset) const override {
        offset &= 0x07;
        if (offset >= 4) return fdc_.peek_data();
        return fdc_.peek_register(static_cast<uint8_t>(offset));
    }

    void write(uint16_t offset, uint8_t value) override {
        offset &= 0x07;
        if (offset >= 4) {
            fdc_.write_data(value);
        } else {
            fdc_.write_register(static_cast<uint8_t>(offset), value);
        }
    }

    // =========================================================================
    // Clock, NMI, reset
    // =========================================================================

    void tick() override { fdc_.tick(); }

    bool nmi_pending() const override { return fdc_.interrupt(); }

    void reset(ResetCause cause) override { fdc_.reset(cause); }

    // =========================================================================
    // Drive management
    // =========================================================================

    void attach_drive(int drive_num, DiscDrive* drive) override {
        fdc_.attach_drive(drive_num, drive);
    }

    void detach_drives() override {
        for (int i = 0; i < 2; ++i) {
            if (DiscDrive* drive = fdc_.attached_drive(i)) drive->set_selected(false);
            fdc_.attach_drive(i, nullptr);
        }
    }

    DiscDrive* attached_drive(int drive_num) const override {
        return fdc_.attached_drive(drive_num);
    }

    // =========================================================================
    // Identification
    // =========================================================================

    std::string_view name() const override { return "Acorn 8271"; }

    std::string_view fdc_chip() const override { return fdc_.name(); }

    // =========================================================================
    // Configuration
    // =========================================================================

    // The 8271 has no spin-up wait of its own: software loads the head and
    // polls for ready. There is nothing to disable.
    void set_spin_up_delay_enabled(bool) override {}
    bool spin_up_delay_enabled() const override { return false; }

    // Direct access to the chip for testing
    Intel8271& fdc() { return fdc_; }
    const Intel8271& fdc() const { return fdc_; }

private:
    Intel8271 fdc_;
};

} // namespace beebium
