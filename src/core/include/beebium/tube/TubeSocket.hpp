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

#include "Coprocessor.hpp"
#include "TubeHostBackend.hpp"
#include "TubeUla.hpp"

#include <cassert>
#include <cstdint>
#include <memory>

namespace beebium {

// Null-object backend for an empty Tube socket (no second processor attached).
//
// Reads return the last bus value (2MHz open bus capacitance). Writes are
// ignored. No interrupts. Reset is a no-op. This is the default state of
// the socket before a second processor is attached.
//
// The bus value pointer is stored indirectly (pointer-to-pointer) so that
// set_last_bus_value_ptr() on the socket takes effect without reconstructing
// this object.
class EmptyTubeBackend : public TubeHostBackend {
public:
    explicit EmptyTubeBackend(const uint8_t* const* bus_value_ptr)
        : bus_value_ptr_(bus_value_ptr) {}

    uint8_t host_read(uint8_t) override {
        return (bus_value_ptr_ && *bus_value_ptr_) ? **bus_value_ptr_ : 0xFF;
    }
    uint8_t host_peek(uint8_t) const override {
        return (bus_value_ptr_ && *bus_value_ptr_) ? **bus_value_ptr_ : 0xFF;
    }
    void host_write(uint8_t, uint8_t) override {}
    bool hirq() const override { return false; }
    void reset() override {}

private:
    const uint8_t* const* bus_value_ptr_;
};

// Models the Tube connector on the underside of the BBC Micro motherboard.
//
// All BBC Micros have the Tube connector (active low active-select at &FEE0-&FEE7),
// but the Tube ULA and second processor are optional add-ons. This class follows
// the same empty/populated socket pattern as DiscControllerSocket and EconetSocket.
//
// The socket delegates all register access, IRQ queries, and reset to a
// TubeHostBackend. Three implementations exist:
//
//   EmptyTubeBackend  -- no second processor (reads return bus value)
//   TubeUla           -- in-process model (both host and parasite sides)
//
// The register offsets use 3 address bits (A0-A2), mirrored across &FEE0-&FEFF.
// The hardware policy registers this with Mirror<0x07>.
class TubeSocket {
public:
    TubeSocket()
        : backend_(std::make_unique<EmptyTubeBackend>(&last_bus_value_ptr_))
    {}

    // --- Configuration ---

    // Enable in in-process mode: both host and parasite sides are modelled
    // by a TubeUla. Useful for single-process testing where parasite_write/
    // parasite_read are called directly on the TubeUla.
    void enable() {
        backend_ = std::make_unique<TubeUla>();
    }

    // Disable the Tube socket (detach second processor).
    // Reverts to empty-socket behaviour.
    void disable() {
        installed_backend_ = nullptr;
        backend_ = std::make_unique<EmptyTubeBackend>(&last_bus_value_ptr_);
    }

    // Install an externally-owned backend (used by extensions).
    // The extension owns the backend's lifetime and must keep it alive while
    // installed. Call disable() or install_backend(nullptr) to detach.
    void install_backend(TubeHostBackend* backend) {
        installed_backend_ = backend;
        if (!backend) {
            disable();
        }
    }

    bool enabled() const {
        return installed_backend_ != nullptr
            || dynamic_cast<EmptyTubeBackend*>(backend_.get()) == nullptr;
    }

    // Set pointer to the MemoryMap's last_bus_value for open bus emulation.
    // The Tube address range (&FEE0-&FEFF) is on the 2MHz bus, so when empty
    // the data bus retains its previous value (capacitance).
    void set_last_bus_value_ptr(const uint8_t* ptr) {
        last_bus_value_ptr_ = ptr;
    }

    // --- MemoryMappedDevice interface ---

    uint8_t read(uint16_t offset) {
        // Reads complete immediately. The Tube ULA does not generate
        // read-side bus stretches: an empty R3 P-to-H returns stale
        // latch data, matching real hardware (and B2, BeebEm, jsbeeb,
        // and B-Em). Only writes to full registers can stretch.
        return active_backend()->host_read(static_cast<uint8_t>(offset));
    }

    // Side-effect-free read for debugger inspection.
    uint8_t peek(uint16_t offset) const {
        return active_backend()->host_peek(static_cast<uint8_t>(offset));
    }

    void write(uint16_t offset, uint8_t value) {
        active_backend()->host_write(static_cast<uint8_t>(offset), value);
    }

    // --- Bus stretching ---

    // Complete any write that was deferred by bus_stretch_cancel.
    // Called by Machine::run() after resume. See TubeHostBackend for details.
    void complete_pending_write() {
        active_backend()->complete_pending_write();
    }

    // Returns true if the last host access could not complete because
    // the target register was full (write) or empty (read). Only
    // meaningful in in-process mode (TubeUla).
    bool stretched() const {
        return active_backend()->stretched();
    }

    // --- IrqSource interface (satisfies IrqSource concept) ---
    //
    // Named irq_pending() to satisfy the generic IrqSource concept used by
    // IrqAggregator. This adapts the Tube-specific HIRQ signal to the
    // machine-level IRQ aggregation framework.

    bool irq_pending() const {
        return active_backend()->hirq();
    }

    // --- Reset ---

    // Reset the Tube subsystem: ULA backend (FIFOs, control flags) and the
    // coprocessor if one is installed. This models the BBC's RST line
    // propagating through the Tube cable: when the host is reset (power-on
    // or Break), the second processor resets too. Without resetting the
    // coprocessor here it would resume whatever it was doing before Break --
    // typically blocked in a Tube R2 OSRDCH wait -- so the host's post-reset
    // banner sequence has no respondent and the user sees a blank screen.
    void reset() {
        active_backend()->reset();
        if (coprocessor_) {
            coprocessor_->reset();
        }
    }

    // --- Coprocessor management (host-time-driven, single-threaded) ---

    // Install a coprocessor to be driven in host time from Machine::step().
    // The caller (extension) owns the coprocessor's lifetime and keeps it alive
    // while installed. The clock ratio lives with the coprocessor, not here.
    void install_coprocessor(Coprocessor* coprocessor) {
        coprocessor_ = coprocessor;
    }

    void remove_coprocessor() { coprocessor_ = nullptr; }

    // Run the coprocessor forward to the given host cycle. Called from
    // Machine::step() as its first action, on every path, so the coprocessor's
    // clock runs continuously whatever the host bus is doing. The coprocessor
    // converts host time to its own cycles and handles its own pause state, so
    // this passes host time through unchanged and is a no-op when nothing is
    // installed or when called again at the same host time.
    void run_coprocessor_until(uint64_t host_cycle) {
        if (coprocessor_) {
            coprocessor_->run_until(host_cycle);
        }
    }

    // Check if the host is Tube bus-stretched.
    bool tube_stretched() const {
        return active_backend()->stretched();
    }

    // Attempt to complete a pending stretch operation.
    // Returns true if the stretch cleared (or was not active).
    // Routed through the backend virtual so nothing here casts to TubeUla.
    bool try_complete_tube_stretch() {
        return active_backend()->try_complete_stretch();
    }

    // --- Accessors ---

    // The active backend's read-only diagnostic surface, or nullptr if it
    // offers none. Works whatever the backend: the socket's own in-process
    // TubeUla or one installed by a coprocessor extension. Used by
    // DeviceInspectionService::GetTubeState.
    const TubeInspection* tube_inspection() const {
        return active_backend()->inspection();
    }

    // Access the socket's OWNED in-process TubeUla, used by the enable() test
    // path. Returns nullptr when a coprocessor extension has installed its own
    // backend -- the socket never casts an installed backend to TubeUla.
    TubeUla* tube_ula() {
        return dynamic_cast<TubeUla*>(backend_.get());
    }
    const TubeUla* tube_ula() const {
        return dynamic_cast<const TubeUla*>(backend_.get());
    }

private:
    // Returns the active backend: the installed (extension-owned) backend
    // if set, otherwise the owned backend.
    TubeHostBackend* active_backend() {
        return installed_backend_ ? installed_backend_ : backend_.get();
    }
    const TubeHostBackend* active_backend() const {
        return installed_backend_ ? installed_backend_ : backend_.get();
    }

    std::unique_ptr<TubeHostBackend> backend_;  // owned backend (empty or legacy)
    TubeHostBackend* installed_backend_ = nullptr;  // non-owning, extension-owned
    const uint8_t* last_bus_value_ptr_ = nullptr;

    // Installed coprocessor, driven in host time (single-threaded model).
    // Non-owning: the extension owns its lifetime. The clock ratio and
    // fractional phase live with the coprocessor, not here.
    Coprocessor* coprocessor_ = nullptr;

    // Diagnostic: parasite ticks consumed by the inline read stretch loop.
    // These ticks happen INSIDE a single host CPU cycle (no cycle_count
    // increment, no peripheral ticking). High values indicate the loop
    // is consuming wall-clock time without advancing the host clock.
    uint64_t read_stretch_parasite_ticks_ = 0;

public:
    uint64_t read_stretch_parasite_ticks() const { return read_stretch_parasite_ticks_; }
};

}  // namespace beebium
