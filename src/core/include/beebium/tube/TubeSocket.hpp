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
#include <functional>
#include <memory>
#include <utility>

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
//   TubeUla           -- in-process model (both host and coprocessor sides)
//
// The register offsets use 3 address bits (A0-A2), mirrored across &FEE0-&FEFF.
// The hardware policy registers this with Mirror<0x07>.
//
// Host/coprocessor skew contract (docs/tube-coprocessor-contract.md, Step 2).
// Coprocessor time C is the host time run_coprocessor_until() was last called
// with (coprocessor_time()); a single-threaded strategy keeps C <= host time H
// always. Immediately before any host register access C == H, so the ULA
// presents exactly the state of that bus cycle. At every other host cycle
// H - C <= MAX_COPROCESSOR_SKEW; the only observable consequence is interrupt
// latency, bounded by that skew. reset() re-establishes C on the next
// run_coprocessor_until(), and while paused C still advances, so the bound
// holds throughout.
class TubeSocket {
public:
    TubeSocket()
        : backend_(std::make_unique<EmptyTubeBackend>(&last_bus_value_ptr_))
    {}

    // --- Configuration ---

    // Enable in in-process mode: both host and coprocessor sides are modelled
    // by a TubeUla. Useful for single-process testing where coprocessor_write/
    // coprocessor_read are called directly on the TubeUla.
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
        // Sync the coprocessor to the current host time so the access is exact
        // (contract 1): it sees every coprocessor cycle due before this bus
        // cycle and none after it.
        run_coprocessor_until(host_time_);
        if (register_access_observer_) {
            register_access_observer_(host_time_,
                                      static_cast<uint8_t>(offset), /*is_write=*/false);
        }
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
        // Sync the coprocessor to the current host time so the access is exact.
        run_coprocessor_until(host_time_);
        if (register_access_observer_) {
            register_access_observer_(host_time_,
                                      static_cast<uint8_t>(offset), /*is_write=*/true);
        }
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
        // Establish the coprocessor's time origin at the current host time, with
        // zero cycles due. Without this, batching would defer its first
        // run_until until host_cycle() finds it a full MAX_COPROCESSOR_SKEW
        // behind, so a coprocessor installed into a running machine would start
        // up to that many host cycles late -- not a skew (the bound holds
        // thereafter) but a permanent startup phase error of ratio x skew
        // cycles. Running it to host_time_ now pins the origin at install.
        run_coprocessor_until(host_time_);
    }

    void remove_coprocessor() { coprocessor_ = nullptr; }

    // Run the coprocessor forward to the given host cycle. Called from
    // Machine::step() as its first action, on every path, so the coprocessor's
    // clock runs continuously whatever the host bus is doing. The coprocessor
    // converts host time to its own cycles and handles its own pause state, so
    // this passes host time through unchanged and is a no-op when nothing is
    // installed or when called again at the same host time.
    void run_coprocessor_until(uint64_t host_cycle) {
        coprocessor_time_ = host_cycle;
        if (coprocessor_) {
            coprocessor_->run_until(host_cycle);
        }
    }

    // Called by Machine::step() first on every path. Stores host time H, and
    // runs the coprocessor to H only once it has fallen a full
    // MAX_COPROCESSOR_SKEW behind -- so the coprocessor runs in batches of up
    // to that many host cycles rather than every cycle (the Step 3 strategy).
    // Register accesses (read/write above), the Tube-stretch path and every
    // host stop sync exactly via run_coprocessor_until(). A host time earlier
    // than the last (a hard reset zeroes cycle_count) syncs immediately so the
    // coprocessor's rebased clock re-establishes its origin here.
    void host_cycle(uint64_t host_time) {
        host_time_ = host_time;
        if (host_time < coprocessor_time_
            || host_time - coprocessor_time_ >= MAX_COPROCESSOR_SKEW) {
            run_coprocessor_until(host_time);
        }
    }

    // Coprocessor time C: the host time the coprocessor has been run to, i.e.
    // the argument of the last run_coprocessor_until(). See the skew contract
    // in this class's comment. C <= host time in a single-threaded strategy.
    uint64_t coprocessor_time() const { return coprocessor_time_; }

    // Maximum permitted skew (host time - coprocessor_time()) in host cycles at
    // any host cycle that is not a Tube register access; register accesses are
    // exact (skew 0). 8 host cycles = 4 us at 2 MHz. Rationale: the tightest
    // open-loop Tube timing is the type-0/3 NMI transfer (host touches R3 about
    // every 24 us per byte), and 4 us leaves the coprocessor's NMI handler well
    // over half that window. See docs/tube-coprocessor-contract.md (Step 2).
    // The value is intended to be raised in Step 3 against measurements; this
    // is the single place to change it.
    static constexpr uint64_t MAX_COPROCESSOR_SKEW = 8;

    // --- Test-only skew observation (not for production use) ---

    // Observer invoked with (host_time, offset, is_write) immediately before
    // each host Tube register read or write, so a test can assert the skew
    // contract (coprocessor_time() == host_time at every access, given read/
    // write sync first). host_time is the value host_cycle() last stored, which
    // is always the current host time in Step 3. Unset in production.
    using RegisterAccessObserver =
        std::function<void(uint64_t host_time, uint8_t offset, bool is_write)>;
    void set_register_access_observer(RegisterAccessObserver observer) {
        register_access_observer_ = std::move(observer);
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

    // Coprocessor time C: host time of the last run_coprocessor_until() call.
    uint64_t coprocessor_time_ = 0;

    // Host time H, stored by host_cycle() every host cycle. read()/write() sync
    // the coprocessor to it before each access.
    uint64_t host_time_ = 0;

    // Test-only register-access observer. When unset the access path pays only
    // a single null check.
    RegisterAccessObserver register_access_observer_;
};

}  // namespace beebium
