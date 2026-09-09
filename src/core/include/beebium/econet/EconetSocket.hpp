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

#include "FourWayHandshake.hpp"
#include "Mc6854.hpp"
#include "NetworkBackend.hpp"
#include "ObservableBackend.hpp"
#include "SpeedGate.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace beebium {

// Models the optional Econet hardware upgrade on the BBC Micro.
//
// On the Model B, this was a set of discrete chip positions: IC93 (MC68B54 ADLC),
// IC97 (NMI gating flip-flop), and supporting passives. On the Master series it
// was a pin header for a carrier board. This class abstracts both.
//
// The socket occupies two non-contiguous address ranges in SHEILA:
//   &FE18-&FE1F: Station ID register (read returns station number, triggers INTOFF)
//   &FEA0-&FEBF: ADLC registers (4 registers, mirrored)
//
// When empty (no Econet hardware fitted):
//   - Station ID region returns 0x00 (slow 1MHz open bus — pull-down resistors)
//   - ADLC region returns the last bus value (fast 2MHz open bus — capacitance)
//   - NMI is never asserted
//   - The DNFS/NFS ROM detects this and skips NFS initialisation
//
// When populated (--station N specified):
//   - Station ID register returns the configured station number
//   - ADLC register access delegates to the Mc6854
//   - NMI gating via INTON/INTOFF flip-flop is active
//   - ADLC is ticked on every 2MHz half-cycle
class EconetSocket {
public:
    EconetSocket() = default;

    // Non-copyable (owns the backend)
    EconetSocket(const EconetSocket&) = delete;
    EconetSocket& operator=(const EconetSocket&) = delete;

    // --- Configuration ---

    // Fit the Econet hardware: sets station number and connects the network backend.
    // When aun_mode is true, a FourWayHandshake decorator is inserted between the
    // ADLC and the backend to bridge AUN's two-way protocol to the four-way
    // handshake that NFS ROMs expect.
    void enable(uint8_t station_id, std::unique_ptr<NetworkBackend> backend,
                bool aun_mode = false, bool requires_real_time = false) {
        // Build the whole chain into locals first (no heavy work under the
        // lifetime lock, and nothing observable to a reader until it is
        // published atomically below).
        std::shared_ptr<NetworkBackend> backend_shared(std::move(backend));
        requires_real_time_ = requires_real_time;
        speed_gated_.store(false, std::memory_order_relaxed);

        // For a transport coupled to a real-time peer (Piconet), insert a
        // SpeedGate decorator that severs the wire when the emulation is not at
        // 1x. Transports that work at any speed (AUN) skip it, keeping the ADLC's
        // per-tick backend polling free of the extra indirection.
        // Observation sits directly above the wire, so it records what
        // actually crossed the transport rather than what the handshake
        // intended -- and below the speed gate, so a gated transport shows as
        // silence, which is what it is. It co-owns the backend so a reader
        // holding it keeps the backend alive too.
        auto observable = std::make_shared<ObservableBackend>(backend_shared);

        NetworkBackend* wire = observable.get();
        std::unique_ptr<SpeedGate> speed_gate;
        if (requires_real_time_) {
            speed_gate = std::make_unique<SpeedGate>(*observable, speed_gated_);
            wire = speed_gate.get();
        }

        std::unique_ptr<FourWayHandshake> handshake;
        std::unique_ptr<Mc6854> adlc;
        if (aun_mode) {
            handshake = std::make_unique<FourWayHandshake>(*wire);
            adlc = std::make_unique<Mc6854>(*handshake);
        } else {
            adlc = std::make_unique<Mc6854>(*wire);
        }

        // Publish. The shared_ptr members are guarded against reader threads;
        // the raw-driven members (adlc_/handshake_/speed_gate_) are safe because
        // the caller parks the emulation thread across enable().
        {
            std::lock_guard<std::mutex> lock(lifetime_mutex_);
            backend_ = std::move(backend_shared);
            observable_ = std::move(observable);
            speed_gate_ = std::move(speed_gate);
            handshake_ = std::move(handshake);
            adlc_ = std::move(adlc);
        }
        station_id_ = station_id;
        enabled_ = true;
        bump_status_sequence();
    }

    // Remove the Econet hardware (return to empty socket state). The caller
    // parks the emulation thread across this (the service wraps it in
    // with_emulation_paused), so tearing down adlc_/handshake_ cannot race the
    // per-cycle tick. A gRPC reader may still hold a co-owning copy of the
    // observable or backend; releasing the socket's own references under the
    // lifetime lock, and letting the objects actually die once the last copy
    // drops, means such a reader never dereferences freed storage.
    void disable() {
        std::shared_ptr<ObservableBackend> observable_drop;
        std::shared_ptr<NetworkBackend> backend_drop;
        {
            std::lock_guard<std::mutex> lock(lifetime_mutex_);
            adlc_.reset();
            handshake_.reset();
            speed_gate_.reset();
            observable_drop = std::move(observable_);
            backend_drop = std::move(backend_);
        }
        // Destroy outside the lock; if a reader holds a copy these persist
        // until it drops, and nothing ticks them once the ADLC is gone.
        observable_drop.reset();
        backend_drop.reset();
        enabled_ = false;
        requires_real_time_ = false;
        speed_gated_.store(false, std::memory_order_relaxed);
        nmi_enable_ff_ = false;
        bump_status_sequence();
    }

    bool enabled() const { return enabled_; }

    // --- Emulation-speed gating ---

    // Whether the active transport must run at real time (1x) -- i.e. it bridges
    // to a real-time peer (Piconet). Mirrors the transport's
    // requires_real_time_pacing(); false for transports that tolerate any speed.
    bool requires_real_time() const { return requires_real_time_; }

    // Whether the wire is currently severed because the emulation speed is not
    // 1x and the transport requires real time. Reflected in EconetStatus so
    // clients can explain why the network went quiet.
    bool gated_by_speed() const {
        return speed_gated_.load(std::memory_order_relaxed);
    }

    // Inform the socket of the current emulation speed multiplier (pushed by the
    // emulation loop). Recomputes the gate and bumps the status sequence on a
    // change so WatchEconetStatus wakes.
    void set_emulation_speed(double speed_multiplier) {
        bool gated = requires_real_time_ && speed_multiplier != 1.0;
        if (speed_gated_.load(std::memory_order_relaxed) != gated) {
            speed_gated_.store(gated, std::memory_order_relaxed);
            bump_status_sequence();
        }
    }

    // Set the station ID without touching the ADLC. Notifies the backend
    // via on_station_id_changed so backends carrying their own station
    // state (e.g. PiconetBackend, where the Piconet device must be told
    // via SET_STATION) can stay in sync.
    //
    // On the Model B this is equivalent to changing the 8 address links.
    // Takes effect on the next read of &FE18 (i.e. on Ctrl-Break when
    // the NFS ROM re-reads the station number).
    void set_station_id(uint8_t station_id) {
        station_id_ = station_id;
        std::shared_ptr<NetworkBackend> backend;
        {
            std::lock_guard<std::mutex> lock(lifetime_mutex_);
            backend = backend_;
        }
        if (backend) {
            backend->on_station_id_changed(station_id);
        }
        bump_status_sequence();
    }

    // Set pointer to the MemoryMap's last_bus_value for open bus emulation
    // in the ADLC region. Must be set before reads if accurate open bus is needed.
    void set_last_bus_value_ptr(const uint8_t* ptr) {
        last_bus_value_ptr_ = ptr;
    }

    // --- Station ID Region (&FE18-&FE1F) ---

    // Read from station ID register. Side-effect: triggers INTOFF (clears NMI enable).
    uint8_t read_station_id(uint16_t /*offset*/) {
        // INTOFF: reading the station ID register clears the NMI enable flip-flop.
        // This happens whether or not Econet hardware is fitted (the address decoding
        // drives the flip-flop reset pin). When empty, the flip-flop doesn't exist,
        // so the side-effect is harmless.
        nmi_enable_ff_ = false;

        if (enabled_) {
            return station_id_;
        }

        // Empty socket: slow 1MHz open bus returns 0x00 (pull-down resistors)
        return 0x00;
    }

    // Write to station ID register (no-op — read-only on real hardware).
    void write_station_id(uint16_t /*offset*/, uint8_t /*value*/) {
        // The station ID register is read-only. Writes are ignored.
        // INTOFF still fires on writes too (address decoding isn't R/W selective).
        nmi_enable_ff_ = false;
    }

    // --- ADLC Region (&FEA0-&FEBF) ---

    // Read from ADLC registers.
    uint8_t read_adlc(uint16_t offset) {
        if (enabled_ && adlc_) {
            uint8_t result = adlc_->read(offset);
            cached_adlc_irq_ = adlc_->irq_output();
            return result;
        }

        // Empty socket: fast 2MHz open bus returns last bus value (capacitance)
        return last_bus_value_ptr_ ? *last_bus_value_ptr_ : 0x00;
    }

    // Write to ADLC registers.
    void write_adlc(uint16_t offset, uint8_t value) {
        if (enabled_ && adlc_) {
            adlc_->write(offset, value);
            cached_adlc_irq_ = adlc_->irq_output();
        }
        // Empty socket: writes fall through (ignored)
    }

    // --- INTON (called when &FE20 is accessed — Video ULA range) ---

    // Sets the NMI enable flip-flop. On real hardware, the Econet hardware
    // taps the same address decode select line as the Video ULA.
    void on_inton() {
        nmi_enable_ff_ = true;
    }

    // --- NMI output ---

    // Returns true when NMI should be asserted to the CPU.
    // Three conditions must all be true:
    //   1. Econet hardware is fitted (enabled_)
    //   2. NMI enable flip-flop is set (INTON fired, INTOFF not yet fired)
    //   3. ADLC IRQ output is active (interrupt condition exists)
    bool nmi_pending() const {
        return enabled_ && nmi_enable_ff_ && cached_adlc_irq_;
    }

    // --- Clock ---

    // 2MHz clock edges — delegates to ADLC when populated, no-op when empty.
    // The handshake is ticked before the ADLC so that timeout-generated frames
    // are available when the ADLC's byte trickle calls receive_frame().
    // Caches the ADLC IRQ output after ticking so nmi_pending() can avoid
    // the unique_ptr dereference on every 2MHz NMI poll.
    void tick_rising() {
        if (enabled_) {
            ++tick_count_;
            if (handshake_) handshake_->tick();
            if (adlc_) {
                adlc_->tick_rising();
                cached_adlc_irq_ = adlc_->irq_output();
            }
        }
    }

    void tick_falling() {
        if (enabled_ && adlc_) {
            adlc_->tick_falling();
            cached_adlc_irq_ = adlc_->irq_output();
        }
    }

    // --- Reset ---

    void reset() {
        if (adlc_) {
            adlc_->hard_reset();
        }
        if (handshake_) {
            handshake_->reset();
        }
        nmi_enable_ff_ = false;
    }

    // --- Accessors for testing ---

    bool nmi_enable_ff() const { return nmi_enable_ff_; }
    uint8_t station_id() const { return station_id_; }

    // Monotonic sequence counter combining socket-level changes (enable,
    // disable, station id) with the active backend's connection-state
    // counter (e.g. AunBackend::set_connected). Used by the
    // WatchEconetStatus RPC poll loop to detect when any status field
    // observable through EconetService may have changed.
    uint64_t status_sequence() const {
        uint64_t seq = status_sequence_.load(std::memory_order_acquire);
        std::shared_ptr<NetworkBackend> backend;
        {
            std::lock_guard<std::mutex> lock(lifetime_mutex_);
            backend = backend_;
        }
        if (backend) seq += backend->backend_status_sequence();
        return seq;
    }

    // Raw accessors, LOCK-FREE by contract. adlc_/handshake_/backend_ are only
    // ever replaced with the emulation thread parked (enable()/disable() run
    // inside with_emulation_paused), so a reader ON THE EMULATION THREAD or with
    // the machine quiesced needs no lock -- and the hot path (Machine::step ->
    // nmi_pending, tick_rising/falling -> adlc_/handshake_) reads the members
    // directly for exactly that reason. Do NOT add a lock here: it would put a
    // mutex on the per-cycle emulation path. Any other thread (a gRPC status
    // handler) must instead take a co-owning copy via backend_shared() /
    // observable(); a raw pointer from here must not outlive the call, since a
    // concurrent DisableEconet can free the pointee. (The remaining gRPC-thread
    // readers of these raw accessors -- EconetService populate_status -- are a
    // Tier 3 item; this branch introduces no new ones.)
    Mc6854* adlc() { return adlc_.get(); }
    const Mc6854* adlc() const { return adlc_.get(); }
    FourWayHandshake* handshake() { return handshake_.get(); }
    const FourWayHandshake* handshake() const { return handshake_.get(); }
    NetworkBackend* backend() { return backend_.get(); }
    const NetworkBackend* backend() const { return backend_.get(); }

    // Co-owning handle to the backend. The caller holds the pointee alive for
    // as long as it keeps the returned shared_ptr, so a read that races
    // DisableEconet is safe. Null when no Econet hardware is fitted.
    std::shared_ptr<NetworkBackend> backend_shared() const {
        std::lock_guard<std::mutex> lock(lifetime_mutex_);
        return backend_;
    }

    // The frame recorder in the backend chain, or nullptr when no Econet
    // hardware is fitted. Read by EconetService to serve SubscribeEconetEvents,
    // which holds it for the life of the stream -- hence a co-owning handle
    // that outlives a concurrent DisableEconet.
    std::shared_ptr<ObservableBackend> observable() {
        std::lock_guard<std::mutex> lock(lifetime_mutex_);
        return observable_;
    }
    std::shared_ptr<const ObservableBackend> observable() const {
        std::lock_guard<std::mutex> lock(lifetime_mutex_);
        return observable_;
    }

    // The top of the backend chain -- what the ADLC talks to. Exposed for
    // tests that want to drive frames through the chain without an ADLC.
    // Lock-free like the other raw accessors: tests drive it single-threaded.
    NetworkBackend* backend_chain_for_test() {
        if (handshake_) return handshake_.get();
        if (speed_gate_) return speed_gate_.get();
        return observable_.get();
    }
    bool aun_mode() const { return handshake_ != nullptr; }

    // Test hook: exposes the lifetime mutex so a test can prove the emulation
    // thread's per-cycle path never locks it (see tests/test_tier2_quiesce.cpp,
    // "Econet hot path takes no lifetime lock"). The mutex guards only the
    // shared_ptr members' copy/assign, never the raw hot-path accessors.
    std::mutex& lifetime_mutex_for_test() { return lifetime_mutex_; }

    uint64_t tick_count() const { return tick_count_; }

    // Diagnostic: how many times has nmi_tx_complete written CR1=&82?
    uint32_t cr1_0x82_write_count() const {
        return adlc_ ? adlc_->cr1_0x82_write_count() : 0;
    }

    // Diagnostic: how many frames has the ADLC received from the backend?
    uint32_t rx_frames_received_count() const {
        return adlc_ ? adlc_->rx_frames_received_count() : 0;
    }

    // Diagnostic: how many times was rx_process_byte blocked by RX_RESET?
    uint32_t rx_blocked_by_reset_count() const {
        return adlc_ ? adlc_->rx_blocked_by_reset_count() : 0;
    }

    // Diagnostic: FourWayHandshake counters
    uint32_t scout_ack_generated_count() const {
        return handshake_ ? handshake_->scout_ack_generated_count() : 0;
    }
    uint32_t tx_frames_from_beeb_count() const {
        return handshake_ ? handshake_->tx_frames_from_beeb_count() : 0;
    }
    uint32_t unexpected_tx_reset_count() const {
        return handshake_ ? handshake_->unexpected_tx_reset_count() : 0;
    }
    uint32_t tx_from_idle_count() const {
        return handshake_ ? handshake_->tx_from_idle_count() : 0;
    }
    int max_handshake_timer_seen() const {
        return handshake_ ? handshake_->max_handshake_timer_seen() : 0;
    }
    uint32_t watchdog_timeout_count() const {
        return handshake_ ? handshake_->watchdog_timeout_count() : 0;
    }
    uint64_t ticks_with_timer_active() const {
        return handshake_ ? handshake_->ticks_with_timer_active() : 0;
    }

    // Diagnostic: stages at each send_frame call
    std::string send_stage_log_string() const {
        if (!handshake_) return "N/A";
        const char* names[] = {"Idle","ScoutSent","ScoutAckRcvd","DataSent",
            "WaitForIdle","ScoutRcvd","ScoutAckSent","DataRcvd","ImmSent","ImmRcvd"};
        std::string result;
        for (uint32_t i = 0; i < handshake_->send_stage_log_count(); ++i) {
            if (i > 0) result += ",";
            int idx = static_cast<int>(handshake_->send_stage_log()[i]);
            result += (idx >= 0 && idx < 10) ? names[idx] : "?";
        }
        return result;
    }

private:
    void bump_status_sequence() {
        status_sequence_.fetch_add(1, std::memory_order_acq_rel);
    }

    // backend_ and observable_ are shared, not unique: a consumer on a gRPC
    // thread (a SubscribeEconetEvents stream holding the observable, a
    // SystemService read of the backend) can be using one while DisableEconet
    // tears the socket down. Handing out a co-owning copy keeps the object
    // alive for that use, so no raw pointer dangles. lifetime_mutex_ guards the
    // shared_ptr MEMBERS themselves (their assignment/reset vs. a reader's
    // copy) -- the emulation thread's per-cycle use goes through adlc_/
    // handshake_, which enable()/disable() only touch with that thread parked
    // (the service wraps them in with_emulation_paused), so the hot path needs
    // no lock here.
    mutable std::mutex lifetime_mutex_;
    std::shared_ptr<NetworkBackend> backend_;
    std::shared_ptr<ObservableBackend> observable_;  // records the wire traffic
    std::unique_ptr<SpeedGate> speed_gate_;  // present only when requires_real_time_
    std::unique_ptr<FourWayHandshake> handshake_;
    std::unique_ptr<Mc6854> adlc_;
    uint8_t station_id_ = 0;
    bool enabled_ = false;
    bool requires_real_time_ = false;
    std::atomic<bool> speed_gated_{false};
    bool cached_adlc_irq_ = false;
    bool nmi_enable_ff_ = false;
    const uint8_t* last_bus_value_ptr_ = nullptr;
    uint64_t tick_count_ = 0;
    std::atomic<uint64_t> status_sequence_{0};
};

}  // namespace beebium
