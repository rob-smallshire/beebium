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

#include <beebium/tube/TubeUla.hpp>

namespace beebium {

TubeUla::TubeUla()
{
    reset();
}

void TubeUla::reset()
{
    control_flags_ = 0;
    host_bus_latch_ = 0;
    parasite_bus_latch_ = 0;
    counters_.reset();
    soft_reset();
}

void TubeUla::soft_reset()
{
    // R1: clear latch and FIFO.
    r1_h2p_.data = 0;
    r1_h2p_.available = false;
    r1_h2p_.full = false;
    for (auto& b : r1_p2h_.data) b = 0;
    r1_p2h_.head = 0;
    r1_p2h_.tail = 0;
    r1_p2h_.count = 0;

    // R2: clear latches.
    r2_h2p_.data = 0;
    r2_h2p_.available = false;
    r2_h2p_.full = false;
    r2_p2h_.data = 0;
    r2_p2h_.available = false;
    r2_p2h_.full = false;

    // R3: clear FIFOs. P-to-H gets one dummy byte to prevent spurious PNMI
    // on reset (matches the initial state documented in B2, BeebEm, jsbeeb
    // and B-Em).
    r3_h2p_.data[0] = 0;
    r3_h2p_.data[1] = 0;
    r3_h2p_.head = 0;
    r3_h2p_.tail = 0;
    r3_h2p_.count = 0;
    r3_h2p_.pending = false;
    r3_p2h_.data[0] = 0;
    r3_p2h_.data[1] = 0;
    r3_p2h_.head = 0;
    r3_p2h_.tail = 1;
    r3_p2h_.count = 1;
    r3_p2h_.pending = true;

    // R4: clear latches.
    r4_h2p_.data = 0;
    r4_h2p_.available = false;
    r4_h2p_.full = false;
    r4_p2h_.data = 0;
    r4_p2h_.available = false;
    r4_p2h_.full = false;

    // Interrupt state.
    hirq_ = false;
    pirq_ = false;
    pnmi_condition_ = false;
    pnmi_level_ = false;
    prev_pnmi_ = false;
    pnmi_edge_ = false;

    // Bus stretch state.
    host_stretched_ = false;

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Host-side register access
// ---------------------------------------------------------------------------

uint8_t TubeUla::host_read(uint8_t offset)
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_ & 0x3F;
        if (r1_p2h_.count > 0)
            result |= DATA_AVAILABLE;
        if (!r1_h2p_.full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from P-to-H 24-byte FIFO. Empty reads return the
        // parasite's data bus latch.
        uint8_t count = r1_p2h_.count;
        if (count > 0) {
            uint8_t head = r1_p2h_.head;
            result = r1_p2h_.data[head];
            r1_p2h_.head = (head + 1) % 24;
            --r1_p2h_.count;
            ++counters_.r1_p2h_reads;
        } else {
            result = parasite_bus_latch_;
        }
        break;
    }

    case 2: {
        result = 0x3F;
        if (r2_p2h_.available)
            result |= DATA_AVAILABLE;
        if (!r2_h2p_.full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from P-to-H latch.
        auto was_available = r2_p2h_.available;
        result = r2_p2h_.data;
        if (was_available) {
            r2_p2h_.available = false;
            r2_p2h_.full = false;
            ++counters_.r2_p2h_reads;
            trace_event(0x28, result);  // R2 P2H host-read
        }
        break;
    }

    case 4: {
        result = 0x3F;
        auto flags = control_flags_;
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        if (r3_p2h_.pending || r3_p2h_.count >= threshold)
            result |= DATA_AVAILABLE;
        if (!r3_h2p_.pending)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 5: {
        // R3 data: read from P-to-H register.
        // When the FIFO is empty, return the parasite's data bus latch
        // without stretching. This matches real hardware (and B2, BeebEm,
        // jsbeeb, B-Em): the Tube ULA does not implement any wait mechanism
        // for empty R3 reads. Software coordinates timing via NMI-driven
        // handshaking, not bus stretches. In particular, ANFS's
        // tube_transfer_setup performs two "flush" reads of R3 P-to-H
        // (BIT &FEE5) during transfer initialisation, with the V flag set;
        // these must complete without blocking even when the parasite
        // hasn't yet written any bytes.
        if (r3_p2h_.count > 0) {
            uint8_t head = r3_p2h_.head;
            result = r3_p2h_.data[head];
            r3_p2h_.head = head ^ 1;
            --r3_p2h_.count;
            if (r3_p2h_.count == 0)
                r3_p2h_.pending = false;
            ++counters_.r3_p2h_reads;
            trace_event(0x38, result);  // R3 P2H host-read
        } else {
            result = parasite_bus_latch_;
        }
        break;
    }

    case 6: {
        result = 0x3F;
        if (r4_p2h_.available)
            result |= DATA_AVAILABLE;
        if (!r4_h2p_.full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from P-to-H latch.
        auto was_available = r4_p2h_.available;
        result = r4_p2h_.data;
        if (was_available) {
            r4_p2h_.available = false;
            r4_p2h_.full = false;
            ++counters_.r4_p2h_reads;
            trace_event(0x48, result);  // R4 P2H host-read
        }
        break;
    }
    }

    update_interrupts();
    return result;
}

uint8_t TubeUla::host_peek(uint8_t offset) const
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_ & 0x3F;
        if (r1_p2h_.count > 0)
            result |= DATA_AVAILABLE;
        if (!r1_h2p_.full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 1: {
        uint8_t count = r1_p2h_.count;
        result = (count > 0) ? r1_p2h_.data[r1_p2h_.head] : parasite_bus_latch_;
        break;
    }
    case 2: {
        result = 0x3F;
        if (r2_p2h_.available)
            result |= DATA_AVAILABLE;
        if (!r2_h2p_.full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = r2_p2h_.data;
        break;
    case 4: {
        result = 0x3F;
        auto flags = control_flags_;
        uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
        if (r3_p2h_.pending || r3_p2h_.count >= threshold)
            result |= DATA_AVAILABLE;
        if (!r3_h2p_.pending)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 5: {
        uint8_t count = r3_p2h_.count;
        result = (count > 0) ? r3_p2h_.data[r3_p2h_.head] : parasite_bus_latch_;
        break;
    }
    case 6: {
        result = 0x3F;
        if (r4_p2h_.available)
            result |= DATA_AVAILABLE;
        if (!r4_h2p_.full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = r4_p2h_.data;
        break;
    }

    return result;
}

void TubeUla::host_write(uint8_t offset, uint8_t value)
{
    // Every write, whatever the address, leaves its value on the data bus latch.
    host_bus_latch_ = value;

    switch (offset & 7) {
    case 0: {
        // Control flag register.
        auto flags = control_flags_;
        if (value & FLAG_S) {
            if (value & FLAG_T)
                soft_reset();
            flags |= (value & 0x3F);
        } else {
            flags &= ~(value & 0x3F);
        }
        control_flags_ = flags;
        break;
    }

    case 1: {
        // R1 data: write to H-to-P latch. Bus stretching via deferred write.
        if (r1_h2p_.full) {
            host_stretched_ = true;
            pending_offset_ = offset & 7;
            pending_value_ = value;
            update_interrupts();
            return;
        }
        r1_h2p_.data = value;
        r1_h2p_.available = true;
        r1_h2p_.full = true;
        ++counters_.r1_h2p_writes;
        break;
    }

    case 2:
        break;

    case 3: {
        // R2 data: write to H-to-P latch. No bus stretching.
        r2_h2p_.data = value;
        r2_h2p_.available = true;
        r2_h2p_.full = true;
        ++counters_.r2_h2p_writes;
        trace_event(0x20, value);  // R2 H2P host-write
        break;
    }

    case 4:
        break;

    case 5: {
        // R3 data: write to H-to-P register. Bus stretching via deferred write.
        if (r3_h2p_.count >= 2) {
            host_stretched_ = true;
            pending_offset_ = offset & 7;
            pending_value_ = value;
            update_interrupts();
            return;
        }
        r3_h2p_.data[r3_h2p_.tail] = value;
        r3_h2p_.tail ^= 1;
        ++r3_h2p_.count;
        uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
        if (r3_h2p_.count >= threshold)
            r3_h2p_.pending = true;
        ++counters_.r3_h2p_writes;
        trace_event(0x30, value);  // R3 H2P host-write
        break;
    }

    case 6:
        break;

    case 7: {
        // R4 data: write to H-to-P latch. Bus stretching via deferred write.
        if (r4_h2p_.full) {
            host_stretched_ = true;
            pending_offset_ = offset & 7;
            pending_value_ = value;
            update_interrupts();
            return;
        }
        r4_h2p_.data = value;
        r4_h2p_.available = true;
        r4_h2p_.full = true;
        ++counters_.r4_h2p_writes;
        trace_event(0x40, value);  // R4 H2P host-write
        break;
    }
    }

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Parasite-side register access
// ---------------------------------------------------------------------------

uint8_t TubeUla::parasite_read(uint8_t offset)
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_ & 0x3F;
        if (r1_h2p_.available)
            result |= DATA_AVAILABLE;
        if (r1_p2h_.count < 24)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 1: {
        // R1 data: read from H-to-P latch.
        auto was_available = r1_h2p_.available;
        result = r1_h2p_.data;
        if (was_available) {
            r1_h2p_.available = false;
            r1_h2p_.full = false;
            ++counters_.r1_h2p_reads;
        }
        break;
    }

    case 2: {
        result = 0x3F;
        if (r2_h2p_.available)
            result |= DATA_AVAILABLE;
        if (!r2_p2h_.full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 3: {
        // R2 data: read from H-to-P latch.
        auto was_available = r2_h2p_.available;
        result = r2_h2p_.data;
        if (was_available) {
            r2_h2p_.available = false;
            r2_h2p_.full = false;
            ++counters_.r2_h2p_reads;
            trace_event(0x24, result);  // R2 H2P parasite-read
        }
        break;
    }

    case 4: {
        // R3 parasite status: N F3 1 1 1 1 1 1 (Application Note 004,
        // register organisation table and note 11). Bit 7 is N, "register 3
        // action required", the ungated PNMI condition: H-to-P data ready
        // or P-to-H drained. It provides the synchronisation signal for
        // type 6/7 paired transfers.
        result = 0x3F;
        if (pnmi_condition_)
            result |= DATA_AVAILABLE;
        if (!r3_p2h_.pending)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 5: {
        // R3 data: read from H-to-P register. Empty reads return the host's
        // data bus latch.
        if (r3_h2p_.count > 0) {
            result = r3_h2p_.data[r3_h2p_.head];
            r3_h2p_.head ^= 1;
            --r3_h2p_.count;
            if (r3_h2p_.count == 0)
                r3_h2p_.pending = false;
            ++counters_.r3_h2p_reads;
            trace_event(0x34, result);  // R3 H2P parasite-read
        } else {
            result = host_bus_latch_;
        }
        break;
    }

    case 6: {
        result = 0x3F;
        if (r4_h2p_.available)
            result |= DATA_AVAILABLE;
        if (!r4_p2h_.full)
            result |= SPACE_AVAILABLE;
        break;
    }

    case 7: {
        // R4 data: read from H-to-P latch.
        auto was_available = r4_h2p_.available;
        result = r4_h2p_.data;
        if (was_available) {
            r4_h2p_.available = false;
            r4_h2p_.full = false;
            ++counters_.r4_h2p_reads;
            trace_event(0x44, result);  // R4 H2P parasite-read
        }
        break;
    }
    }

    update_interrupts();
    return result;
}

uint8_t TubeUla::parasite_peek(uint8_t offset) const
{
    uint8_t result = 0;

    switch (offset & 7) {
    case 0: {
        result = control_flags_ & 0x3F;
        if (r1_h2p_.available)
            result |= DATA_AVAILABLE;
        if (r1_p2h_.count < 24)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 1:
        result = r1_h2p_.data;
        break;
    case 2: {
        result = 0x3F;
        if (r2_h2p_.available)
            result |= DATA_AVAILABLE;
        if (!r2_p2h_.full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 3:
        result = r2_h2p_.data;
        break;
    case 4: {
        result = 0x3F;
        if (pnmi_condition_)
            result |= DATA_AVAILABLE;
        if (!r3_p2h_.pending)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 5: {
        uint8_t count = r3_h2p_.count;
        result = (count > 0) ? r3_h2p_.data[r3_h2p_.head] : host_bus_latch_;
        break;
    }
    case 6: {
        result = 0x3F;
        if (r4_h2p_.available)
            result |= DATA_AVAILABLE;
        if (!r4_p2h_.full)
            result |= SPACE_AVAILABLE;
        break;
    }
    case 7:
        result = r4_h2p_.data;
        break;
    }

    return result;
}

void TubeUla::parasite_write(uint8_t offset, uint8_t value)
{
    // Every write, whatever the address, leaves its value on the data bus latch.
    parasite_bus_latch_ = value;

    switch (offset & 7) {
    case 0:
        break;

    case 1: {
        // R1 data: write to P-to-H 24-byte FIFO.
        uint8_t count = r1_p2h_.count;
        if (count < 24) {
            uint8_t tail = r1_p2h_.tail;
            r1_p2h_.data[tail] = value;
            r1_p2h_.tail = (tail + 1) % 24;
            ++r1_p2h_.count;
            ++counters_.r1_p2h_writes;
        }
        break;
    }

    case 2:
        break;

    case 3: {
        // R2 data: write to P-to-H latch. No bus stretching.
        r2_p2h_.data = value;
        r2_p2h_.available = true;
        r2_p2h_.full = true;
        ++counters_.r2_p2h_writes;
        trace_event(0x2C, value);  // R2 P2H parasite-write
        break;
    }

    case 4:
        break;

    case 5: {
        // R3 data: write to P-to-H register.
        if (r3_p2h_.count < 2) {
            r3_p2h_.data[r3_p2h_.tail] = value;
            r3_p2h_.tail ^= 1;
            ++r3_p2h_.count;
            uint8_t threshold = (control_flags_ & FLAG_V) ? 2 : 1;
            if (r3_p2h_.count >= threshold)
                r3_p2h_.pending = true;
            ++counters_.r3_p2h_writes;
            trace_event(0x3C, value);  // R3 P2H parasite-write
        }
        break;
    }

    case 6:
        break;

    case 7: {
        // R4 data: write to P-to-H latch.
        r4_p2h_.data = value;
        r4_p2h_.available = true;
        r4_p2h_.full = true;
        ++counters_.r4_p2h_writes;
        trace_event(0x4C, value);  // R4 P2H parasite-write
        break;
    }
    }

    update_interrupts();
}

// ---------------------------------------------------------------------------
// Interrupt computation
// ---------------------------------------------------------------------------

bool TubeUla::hirq() const
{
    return hirq_;
}

bool TubeUla::pirq() const
{
    return pirq_;
}

bool TubeUla::pnmi() const
{
    return pnmi_level_;
}

bool TubeUla::pnmi_level() const
{
    return pnmi_level_;
}

void TubeUla::update_interrupts()
{
    auto flags = control_flags_;

    hirq_ = (flags & FLAG_Q) && r4_p2h_.available;

    pirq_ = ((flags & FLAG_I) && r1_h2p_.available)
          || ((flags & FLAG_J) && r4_h2p_.available);

    // PNMI condition: always computed regardless of M flag.
    // True when R3 H-to-P has data, OR R3 P-to-H is not pending (empty/drained).
    // The parasite R3 status register bit 7 reflects this condition directly
    // (as per the Tube ULA hardware -- see B2 reference).
    uint8_t threshold = (flags & FLAG_V) ? 2 : 1;
    pnmi_condition_ = (r3_h2p_.pending || r3_h2p_.count >= threshold)
                    || !r3_p2h_.pending;

    // M flag gates whether the PNMI condition reaches the NMI output pin.
    bool new_pnmi = (flags & FLAG_M) ? pnmi_condition_ : false;

    if (new_pnmi && !prev_pnmi_)
        pnmi_edge_ = true;
    if (!new_pnmi)
        pnmi_edge_ = false;
    prev_pnmi_ = new_pnmi;
    pnmi_level_ = new_pnmi;
}

// ---------------------------------------------------------------------------
// Bus stretch deferred-write mechanism
// ---------------------------------------------------------------------------

bool TubeUla::try_complete_stretch()
{
    if (!host_stretched_) return true;

    // Write stretches only. R3/R4/R1 reads do not stretch in this model;
    // the ULA returns stale/latched data immediately, matching hardware.
    switch (pending_offset_) {
    case 1:
        if (r1_h2p_.full) return false;
        break;
    case 5:
        if (r3_h2p_.count >= 2) return false;
        break;
    case 7:
        if (r4_h2p_.full) return false;
        break;
    default:
        break;
    }

    // Condition cleared -- replay the deferred write.
    host_stretched_ = false;  // Prevent re-entry
    host_write(pending_offset_, pending_value_);
    return true;
}

}  // namespace beebium
