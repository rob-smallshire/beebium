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

#include "PacingConfig.hpp"
#include "PacingController.hpp"
#include "PlatformSleep.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace beebium {

/// Real-time pacing clock with smooth progress.
///
/// Every tick: one fixed-duration minimum-length OS sleep, then the
/// emulation thread runs a variable number of cycles computed by a
/// deficit controller. The sleep quantum is measured at startup to
/// adapt to the platform.
///
/// Progress is perfectly smooth: every tick is identical in structure
/// (sleep + run). Only the cycle count varies, and it changes gradually.
///
/// Usage:
///   PacingClock clock(config, quantum);
///   clock.start();
///   while (running) {
///       clock.wait_for_tick();
///       uint64_t n = clock.cycles_for_next_tick();
///       machine.run(n);
///       clock.report_cycles(machine.cycle_count());
///   }
///   clock.stop();
///
class PacingClock {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;

    /// @param config    Pacing configuration (target clock rate, speed).
    /// @param quantum   Measured sleep quantum from measure_sleep_quantum().
    /// @param sleeper   Platform-specific sleep implementation (moved in).
    /// @param io_pending Optional I/O pending flag for sub-quantum wakeup.
    PacingClock(const PacingConfig& config, Duration quantum,
                PlatformSleep sleeper,
                std::atomic<bool>* io_pending = nullptr)
        : config_(config)
        , quantum_(quantum)
        , controller_(config.base_clock_hz, quantum.count())
        , sleeper_(std::move(sleeper))
        , io_pending_(io_pending) {
        // The live, runtime-adjustable speed lives in an atomic, seeded from
        // the configured value. config_ holds only immutable pacing parameters
        // thereafter; see speed_multiplier_.
        speed_multiplier_.store(config.speed_multiplier, std::memory_order_relaxed);
    }

    ~PacingClock() { stop(); }

    PacingClock(const PacingClock&) = delete;
    PacingClock& operator=(const PacingClock&) = delete;

    void start() {
        if (running_) return;
        running_ = true;
        paused_ = false;
        tick_ready_ = false;
        start_time_ = Clock::now();
        last_pace_wall_ = start_time_;
        effective_elapsed_ns_ = 0.0;
        baseline_cycles_ = total_cycles_.load(std::memory_order_relaxed);
        last_speed_ = speed_multiplier_.load(std::memory_order_relaxed);
        controller_.reset();
        timer_thread_ = std::thread(&PacingClock::timer_loop, this);
    }

    void request_stop() {
        running_ = false;
        {
            std::lock_guard lock(mutex_);
            tick_ready_ = true;
        }
        cv_.notify_all();
        pause_cv_.notify_all();
    }

    void stop() {
        if (!timer_thread_.joinable()) return;
        request_stop();
        timer_thread_.join();
    }

    bool is_running() const { return running_; }

    /// Report the emulation's current cycle count (after each run).
    void report_cycles(uint64_t total_cycles) {
        total_cycles_.store(total_cycles, std::memory_order_release);
    }

    /// Get cycles to execute in the next tick (deficit controller).
    uint64_t cycles_for_next_tick() {
        double s = speed_multiplier_.load(std::memory_order_relaxed);
        auto now = Clock::now();

        if (s == 0.0) {
            // Unlimited: run flat out. Keep the pacing anchor at "now" so the
            // (discarded) unlimited interval does not skew the next finite
            // window; a real speed change is rebased on return below.
            last_pace_wall_ = now;
            last_speed_ = s;
            return controller_.nominal_cycles();
        }

        // Rebase on any speed change (including leaving unlimited): re-anchor the
        // cycle baseline and the integrated target to "now", so the deficit
        // accounting tracks the new rate from here without a lurch.
        if (s != last_speed_) {
            baseline_cycles_ = total_cycles_.load(std::memory_order_acquire);
            effective_elapsed_ns_ = 0.0;
            last_pace_wall_ = now;
            last_speed_ = s;
            controller_.reset();
        }

        // Integrate the multiplier-scaled wall time. Feeding the controller this
        // stretched time makes its target slope base*s, while the controller
        // itself stays a pure 1x deficit tracker. The cycle count is taken
        // relative to the baseline so target and actual share an origin.
        double dt_ns = static_cast<double>(
            std::chrono::duration_cast<Duration>(now - last_pace_wall_).count());
        last_pace_wall_ = now;
        effective_elapsed_ns_ += s * dt_ns;

        uint64_t cycles =
            total_cycles_.load(std::memory_order_acquire) - baseline_cycles_;
        return controller_.update(static_cast<int64_t>(effective_elapsed_ns_), cycles, s);
    }

    /// Legacy: fixed cycles per tick for non-paced mode.
    uint64_t cycles_per_tick() const {
        return config_.cycles_per_tick();
    }

    /// Block until next tick is ready.
    void wait_for_tick() {
        if (is_unlimited()) return;
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100),
                     [this] { return tick_ready_ || !running_; });
        tick_ready_ = false;
    }

    const PacingConfig& config() const { return config_; }
    Duration quantum() const { return quantum_; }

    /// Set the runtime speed multiplier (0.0 = unlimited). Lock-free: a single
    /// atomic store, observed by the timer and emulation threads on their next
    /// tick. Callable from any thread (e.g. the gRPC service).
    void set_speed_multiplier(double m) {
        speed_multiplier_.store(m, std::memory_order_relaxed);
    }

    double speed_multiplier() const {
        return speed_multiplier_.load(std::memory_order_relaxed);
    }

    /// Publish a throughput sample for monitoring. Called from the emulation
    /// loop once per stats window with the achieved multiplier (actual emulated
    /// rate / base clock) and the estimated ceiling at current host load.
    /// Lock-free: two relaxed atomic stores, read by the gRPC service thread.
    void publish_throughput(double achieved_multiplier,
                            double estimated_max_multiplier) {
        achieved_multiplier_.store(achieved_multiplier, std::memory_order_relaxed);
        estimated_max_multiplier_.store(estimated_max_multiplier,
                                        std::memory_order_relaxed);
    }

    /// Achieved speed multiplier over the most recent stats window (0.0 until
    /// the first window completes).
    double achieved_speed_multiplier() const {
        return achieved_multiplier_.load(std::memory_order_relaxed);
    }

    /// Estimated maximum attainable speed multiplier at current host load
    /// (0.0 = no estimate yet: idle/paused or not yet sampled).
    double estimated_max_speed_multiplier() const {
        return estimated_max_multiplier_.load(std::memory_order_relaxed);
    }

    void pause() {
        std::lock_guard lock(mutex_);
        paused_ = true;
    }

    void resume() {
        {
            std::lock_guard lock(mutex_);
            paused_ = false;
            start_time_ = Clock::now();
            last_pace_wall_ = start_time_;
            effective_elapsed_ns_ = 0.0;
            // Re-anchor the cycle baseline to the current count rather than
            // zeroing it: the emulation keeps reporting its absolute cycle
            // count, so the deficit must be measured relative to here.
            baseline_cycles_ = total_cycles_.load(std::memory_order_relaxed);
            last_speed_ = speed_multiplier_.load(std::memory_order_relaxed);
            controller_.reset();
        }
        pause_cv_.notify_one();
    }

    bool is_paused() const {
        std::lock_guard lock(mutex_);
        return paused_;
    }

    /// Re-anchor the pacing baseline to the current cycle count and wall clock,
    /// discarding any accumulated deficit. Use after emulation (or wall time)
    /// advanced without the pacing clock -- e.g. a debugger reset or a
    /// run_until step sequence -- so it does not run fast to "catch up". Unlike
    /// resume(), this does NOT touch the pause state or notify pause_cv_, so it
    /// is safe to call from the emulation thread without disturbing the timer
    /// thread or the debugger's pause handling.
    void rebase() {
        std::lock_guard lock(mutex_);
        start_time_ = Clock::now();
        last_pace_wall_ = start_time_;
        effective_elapsed_ns_ = 0.0;
        baseline_cycles_ = total_cycles_.load(std::memory_order_relaxed);
        last_speed_ = speed_multiplier_.load(std::memory_order_relaxed);
        controller_.reset();
    }

    struct TimingStats {
        uint64_t ticks_executed;
        uint64_t ticks_io_woken;
        double controller_deficit;
        double controller_integral;  // Always 0 (no integral in deficit mode)
    };

    TimingStats timing_stats() const {
        std::lock_guard lock(mutex_);
        return {
            ticks_executed_,
            ticks_io_woken_,
            controller_.last_deficit(),
            0.0
        };
    }

private:
    /// Whether pacing is currently in unlimited mode. Reads the live atomic,
    /// so it is safe on the timer and emulation threads without locking.
    bool is_unlimited() const {
        return speed_multiplier_.load(std::memory_order_relaxed) == 0.0;
    }

    void timer_loop() {
        while (running_) {
            // Handle pause
            {
                std::unique_lock lock(mutex_);
                pause_cv_.wait(lock, [this] { return !paused_ || !running_; });
                if (!running_) break;
            }

            // Unlimited mode: signal immediately
            if (is_unlimited()) {
                {
                    std::lock_guard lock(mutex_);
                    tick_ready_ = true;
                    ++ticks_executed_;
                }
                cv_.notify_one();
                std::this_thread::yield();
                continue;
            }

            // One fixed-duration sleep. Optionally interruptible by I/O.
            if (io_pending_) {
                // Interruptible: sleep in short intervals, check io_pending
                auto deadline = Clock::now() + quantum_;
                static constexpr auto poll_interval =
                    std::chrono::microseconds(100);
                while (Clock::now() < deadline && running_) {
                    if (io_pending_->load(std::memory_order_relaxed)) {
                        io_pending_->store(false, std::memory_order_relaxed);
                        {
                            std::lock_guard lock(mutex_);
                            ++ticks_io_woken_;
                        }
                        break;
                    }
                    auto remaining = deadline - Clock::now();
                    if (remaining > poll_interval)
                        sleeper_.sleep(poll_interval);
                    else if (remaining > Duration::zero())
                        sleeper_.sleep(std::chrono::duration_cast<Duration>(remaining));
                }
            } else {
                // Non-interruptible: single efficient sleep
                sleeper_.sleep(quantum_);
            }

            // Signal emulation thread
            {
                std::lock_guard lock(mutex_);
                tick_ready_ = true;
                ++ticks_executed_;
            }
            cv_.notify_one();
        }
    }

    // Immutable pacing parameters (base clock, pacing rate). The construction
    // value of speed_multiplier seeds speed_multiplier_ and is not read again.
    PacingConfig config_;
    Duration quantum_;

    // Live, runtime-adjustable speed multiplier (0.0 = unlimited). Written by
    // set_speed_multiplier() from any thread; read lock-free on the hot path.
    std::atomic<double> speed_multiplier_{1.0};

    // Throughput samples published by the emulation loop once per stats window
    // and read lock-free by the gRPC service. 0.0 until the first window.
    std::atomic<double> achieved_multiplier_{0.0};
    std::atomic<double> estimated_max_multiplier_{0.0};

    // Deficit controller
    PacingController controller_;

    // Platform-specific high-resolution sleep
    PlatformSleep sleeper_; // must be after controller_ to match initialiser order

    // Stats
    uint64_t ticks_executed_ = 0;
    uint64_t ticks_io_woken_ = 0;

    // Timing
    Clock::time_point start_time_;
    std::atomic<uint64_t> total_cycles_{0};

    // Variable-rate pacing accounting. Touched only by cycles_for_next_tick on
    // the emulation thread, plus (re)initialised in start()/resume() (the latter
    // under mutex_, ordered before the next tick by the wait_for_tick handshake).
    // effective_elapsed_ns_ integrates speed_multiplier * wall_dt, so feeding it
    // to the 1x deficit controller yields a base*multiplier target;
    // baseline_cycles_ anchors the cycle count at the last (re)base; last_speed_
    // detects a speed change so the next tick rebases.
    Clock::time_point last_pace_wall_{};
    double effective_elapsed_ns_ = 0.0;
    uint64_t baseline_cycles_ = 0;
    double last_speed_ = 1.0;

    // Thread control
    std::atomic<bool> running_{false};
    std::thread timer_thread_;

    // Synchronization
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable pause_cv_;
    bool tick_ready_{false};
    bool paused_{false};

    // I/O pending flag (optional)
    std::atomic<bool>* io_pending_ = nullptr;
};

/// Estimate the maximum attainable speed multiplier from the achieved
/// multiplier and the fraction of wall-clock time spent computing (the rest
/// being sleep/idle slack that could be reclaimed by running faster).
///
/// If the emulator currently runs at `achieved` multiples of real time while
/// computing only `active_fraction` of the time, it could run up to
/// `achieved / active_fraction` before saturating the host -- an upper bound,
/// since per-tick overheads do not scale perfectly with speed.
///
/// Returns 0.0 ("no estimate") when there is nothing to extrapolate from:
/// a non-positive achieved rate or active_fraction (idle/paused). The result
/// is never below `achieved` (active_fraction is clamped to <= 1).
constexpr double estimate_max_speed_multiplier(double achieved_multiplier,
                                               double active_fraction) {
    if (achieved_multiplier <= 0.0 || active_fraction <= 0.0) {
        return 0.0;
    }
    double fraction = active_fraction < 1.0 ? active_fraction : 1.0;
    return achieved_multiplier / fraction;
}

} // namespace beebium
