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

#ifndef BEEBIUM_INDICATORS_INDICATORS_HPP
#define BEEBIUM_INDICATORS_INDICATORS_HPP

#include "beebium/indicators/IndicatorFilter.hpp"
#include "beebium/ThreadName.hpp"
#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace beebium {

// =============================================================================
// Clock Abstractions for Dependency Injection
// =============================================================================

/// Default clock using std::chrono::steady_clock.
/// Used in production.
struct SteadyClock {
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    static time_point now() {
        return std::chrono::steady_clock::now();
    }
};

/// Manually-controlled clock for testing.
/// Allows tests to control time progression without relying on wall-clock time.
struct ManualClock {
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    static time_point now() {
        return current_time_;
    }

    static void set(time_point t) {
        current_time_ = t;
    }

    static void advance(duration d) {
        current_time_ += d;
    }

    static void reset() {
        current_time_ = time_point{};
    }

private:
    static inline time_point current_time_{};
};

/// Event representing an indicator value change.
/// Pushed to the event queue by the emulation thread.
template<typename Clock = SteadyClock>
struct IndicatorEventT {
    uint16_t id;
    uint8_t value;
    typename Clock::time_point timestamp;
};

// Default event type for production
using IndicatorEvent = IndicatorEventT<SteadyClock>;

/// Manages observable hardware state (LEDs, motors) with configurable filtering.
///
/// Architecture:
/// - Emulation thread pushes events to a lock-free ring buffer (non-blocking)
/// - Consumer thread runs at ~50Hz, drains queue, applies filters, updates published state
/// - gRPC clients read published state via atomic reads
///
/// Each indicator has:
/// - A unique string name (e.g., "caps-lock-led", "floppy-0-activity-led")
/// - A filter policy (passthrough, debounce, duty-cycle)
/// - Optional metadata (label, color, shape) for frontend rendering
///
/// Template parameter Clock allows injection of a test clock for deterministic testing.
template<typename Clock = SteadyClock>
class IndicatorsBase {
public:
    using Event = IndicatorEventT<Clock>;

    IndicatorsBase() = default;

    ~IndicatorsBase() {
        stop();
    }

    // Non-copyable, non-movable (owns thread)
    IndicatorsBase(const IndicatorsBase&) = delete;
    IndicatorsBase& operator=(const IndicatorsBase&) = delete;
    IndicatorsBase(IndicatorsBase&&) = delete;
    IndicatorsBase& operator=(IndicatorsBase&&) = delete;

    /// Register an indicator with a filter policy.
    /// Must be called before the first call to start(). Once start() has been
    /// called, the registration window is closed permanently -- subsequent
    /// calls throw std::logic_error, even after stop().
    /// @param name Unique indicator name (e.g., "caps-lock-led")
    /// @param filter Filter policy to apply to updates
    /// @param metadata Key-value pairs for frontend rendering (e.g., {"label", "CAPS LOCK"})
    /// @return ID for efficient updates via set(id, value)
    /// @throws std::logic_error if called after start()
    uint16_t register_indicator(
        const std::string& name,
        std::unique_ptr<IndicatorFilter> filter,
        std::unordered_map<std::string, std::string> metadata = {}) {

        if (ever_started_.load(std::memory_order_acquire)) {
            throw std::logic_error(
                "Indicators::register_indicator(\"" + name +
                "\") called after start(); registration must complete before start()");
        }

        std::unique_lock lock(registry_mutex_);

        uint16_t id = static_cast<uint16_t>(indicators_.size());
        name_to_id_[name] = id;

        IndicatorState state;
        state.name = name;
        state.filter = std::move(filter);
        state.metadata = std::move(metadata);
        state.published_value.store(0, std::memory_order_relaxed);

        indicators_.push_back(std::move(state));

        return id;
    }

    /// Update an indicator by ID (hot path, no string lookup).
    /// Non-blocking; pushes to event queue.
    /// @param id Indicator ID returned by register_indicator()
    /// @param value New value (typically 0 or 255)
    /// @return true if event was queued, false if queue was full (event dropped)
    bool set(uint16_t id, uint8_t value) {
        Event event{id, value, Clock::now()};
        return event_queue_.try_enqueue(event);
    }

    /// Update an indicator by name (convenience, does string lookup).
    /// @param name Indicator name
    /// @param value New value
    /// @return true if event was queued, false if name unknown or queue full
    bool set(const std::string& name, uint8_t value) {
        std::shared_lock lock(registry_mutex_);
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return false;
        }
        return set(it->second, value);
    }

    /// Batch update multiple indicators.
    /// @param updates List of (id, value) pairs
    /// @return true if all events were queued
    bool set(std::initializer_list<std::pair<uint16_t, uint8_t>> updates) {
        bool all_ok = true;
        for (const auto& [id, value] : updates) {
            if (!set(id, value)) {
                all_ok = false;
            }
        }
        return all_ok;
    }

    /// Get the published value of an indicator by name.
    /// Lock-free atomic read.
    uint8_t get(const std::string& name) const {
        std::shared_lock lock(registry_mutex_);
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return 0;
        }
        return indicators_[it->second].published_value.load(std::memory_order_relaxed);
    }

    /// Get all published indicator values.
    std::unordered_map<std::string, uint8_t> values() const {
        std::unordered_map<std::string, uint8_t> result;
        std::shared_lock lock(registry_mutex_);
        for (const auto& state : indicators_) {
            result[state.name] = state.published_value.load(std::memory_order_relaxed);
        }
        return result;
    }

    /// Get metadata for an indicator.
    /// @param name Indicator name
    /// @return Metadata map, or empty map if not found
    std::unordered_map<std::string, std::string> metadata(const std::string& name) const {
        std::shared_lock lock(registry_mutex_);
        auto it = name_to_id_.find(name);
        if (it == name_to_id_.end()) {
            return {};
        }
        return indicators_[it->second].metadata;
    }

    /// Get all registered indicator names.
    std::vector<std::string> names() const {
        std::vector<std::string> result;
        std::shared_lock lock(registry_mutex_);
        result.reserve(indicators_.size());
        for (const auto& state : indicators_) {
            result.push_back(state.name);
        }
        return result;
    }

    /// Get the change sequence number.
    /// Incremented each time any published value changes.
    uint64_t sequence() const {
        return sequence_.load(std::memory_order_acquire);
    }

    /// Start the consumer thread.
    /// Call after all indicators are registered. Closes the registration window
    /// permanently -- subsequent register_indicator() calls will throw.
    void start() {
        ever_started_.store(true, std::memory_order_release);
        if (running_.exchange(true)) {
            return;  // Already running
        }
        consumer_thread_ = std::thread(&IndicatorsBase::consumer_loop, this);
    }

    /// Stop the consumer thread and wait for it to finish.
    void stop() {
        if (!running_.exchange(false)) {
            return;  // Not running
        }
        if (consumer_thread_.joinable()) {
            consumer_thread_.join();
        }
    }

    /// Check if the consumer thread is running.
    bool is_running() const {
        return running_.load(std::memory_order_relaxed);
    }

    /// Process pending events and sample all filters synchronously.
    /// For testing with ManualClock - allows deterministic testing without consumer thread.
    /// @return true if any published value changed
    bool process_pending() {
        auto now = Clock::now();

        // Drain the event queue, dispatch to filters
        Event event;
        while (event_queue_.try_dequeue(event)) {
            std::shared_lock lock(registry_mutex_);
            if (event.id < indicators_.size()) {
                indicators_[event.id].filter->update(event.value, event.timestamp);
            }
        }

        // Sample all filters, update published values
        bool any_changed = false;
        {
            std::shared_lock lock(registry_mutex_);
            for (auto& state : indicators_) {
                uint8_t old_val = state.published_value.load(std::memory_order_relaxed);
                uint8_t new_val = state.filter->sample(now);
                if (old_val != new_val) {
                    state.published_value.store(new_val, std::memory_order_relaxed);
                    any_changed = true;
                }
            }
        }

        if (any_changed) {
            sequence_.fetch_add(1, std::memory_order_release);
        }

        return any_changed;
    }

private:
    struct IndicatorState {
        std::string name;
        std::unique_ptr<IndicatorFilter> filter;
        std::unordered_map<std::string, std::string> metadata;
        std::atomic<uint8_t> published_value{0};

        // IndicatorState needs to be movable for vector storage
        IndicatorState() = default;
        IndicatorState(IndicatorState&& other) noexcept
            : name(std::move(other.name))
            , filter(std::move(other.filter))
            , metadata(std::move(other.metadata))
            , published_value(other.published_value.load()) {}
    };

    void consumer_loop() {
        set_current_thread_name("indicators");
        while (running_.load(std::memory_order_relaxed)) {
            auto now = Clock::now();

            // Drain the event queue, dispatch to filters
            Event event;
            while (event_queue_.try_dequeue(event)) {
                std::shared_lock lock(registry_mutex_);
                if (event.id < indicators_.size()) {
                    indicators_[event.id].filter->update(event.value, event.timestamp);
                }
            }

            // Sample all filters, update published values
            bool any_changed = false;
            {
                std::shared_lock lock(registry_mutex_);
                for (auto& state : indicators_) {
                    uint8_t old_val = state.published_value.load(std::memory_order_relaxed);
                    uint8_t new_val = state.filter->sample(now);
                    if (old_val != new_val) {
                        state.published_value.store(new_val, std::memory_order_relaxed);
                        any_changed = true;
                    }
                }
            }

            if (any_changed) {
                sequence_.fetch_add(1, std::memory_order_release);
            }

            // Sleep for ~20ms (50Hz update rate)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // Indicator registry (read-heavy after initialization)
    std::vector<IndicatorState> indicators_;
    std::unordered_map<std::string, uint16_t> name_to_id_;
    mutable std::shared_mutex registry_mutex_;

    // Lock-free event queue (ring buffer, drops old events when full)
    moodycamel::ReaderWriterQueue<Event> event_queue_{4096};

    // Consumer thread
    std::thread consumer_thread_;
    std::atomic<bool> running_{false};
    // Latches true on the first call to start(); never reset. Guards
    // register_indicator() against post-start registration.
    std::atomic<bool> ever_started_{false};

    // Change tracking
    std::atomic<uint64_t> sequence_{0};
};

// Type alias for production use (backwards compatibility)
using Indicators = IndicatorsBase<SteadyClock>;

// Type alias for testing with manually controlled time
using TestIndicators = IndicatorsBase<ManualClock>;

} // namespace beebium

#endif // BEEBIUM_INDICATORS_INDICATORS_HPP
