// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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


#ifndef BEEBIUM_PRINTER_CENTRONICS_RECEIVER_HPP
#define BEEBIUM_PRINTER_CENTRONICS_RECEIVER_HPP

#include "beebium/extension/PrinterPortDevice.hpp"

#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace beebium::printer {

// The receiving end of a Centronics handshake, as a PrinterPortDevice.
//
// Every printer-like sink needs the same behaviour at the connector, so it
// lives here once: latch the data on the falling edge of /STROBE, queue the
// byte, and after ack_delay_ticks drive /ACK low for ack_pulse_ticks. The
// defaults, 4 and 4 microseconds, are in the range of an Epson FX-80.
//
// The queue is bounded. When it is full the latched byte is held and the
// acknowledge is deferred until the consumer has made room, so a slow sink
// stalls the guest (which is waiting for /ACK) and never the host, and no
// byte is lost. While offline nothing is accepted and /ACK stays high, which
// is what a switched-off or absent printer looks like.
//
// Threads: the emulation thread is the producer through
// update_control_lines(); the sink's own thread is the consumer through
// try_dequeue() and wait_dequeue_timed(). set_online() and the counters may
// be used from any thread. The emulation thread never blocks.
class CentronicsReceiver : public PrinterPortDevice {
public:
    struct Options {
        std::size_t queue_depth = 256;   // bytes the receiver buffers
        uint32_t ack_delay_ticks = 4;    // strobe to /ACK falling, in VIA ticks
        uint32_t ack_pulse_ticks = 4;    // /ACK low time, in VIA ticks
        bool online = true;
    };

    CentronicsReceiver() : CentronicsReceiver(Options{}) {}

    explicit CentronicsReceiver(Options options)
        : options_(options)
        , online_(options.online)
        , queue_(options.queue_depth == 0 ? 1 : options.queue_depth) {}

    const Options& options() const { return options_; }

    // -- Producer side: the port A half of the User VIA composite ----------

    uint8_t update_control_lines(uint8_t strobe, uint8_t data) override {
        const bool falling_edge = (strobe_level_ != 0) && (strobe == 0);
        strobe_level_ = strobe;

        if (falling_edge && online_.load(std::memory_order_relaxed)) {
            // A strobe while a byte is still held replaces it: the Beeb has
            // no BUSY line, and well-behaved software waits for /ACK first.
            held_byte_ = data;
            holding_ = true;
        }

        if (holding_ && offer_held_byte()) {
            holding_ = false;
            ticks_since_accept_ = 0;
            acknowledging_ = true;
        }

        uint8_t ack = 1;
        if (acknowledging_) {
            const uint32_t t = ticks_since_accept_;
            if (t >= options_.ack_delay_ticks &&
                t < options_.ack_delay_ticks + options_.ack_pulse_ticks) {
                ack = 0;
            }
            if (t + 1 >= options_.ack_delay_ticks + options_.ack_pulse_ticks) {
                acknowledging_ = false;
            }
            ++ticks_since_accept_;
        }
        return ack;
    }

    // -- Any thread -------------------------------------------------------

    void set_online(bool online) { online_.store(online, std::memory_order_relaxed); }
    bool is_online() const { return online_.load(std::memory_order_relaxed); }

    // Bytes accepted into the queue since construction.
    uint64_t bytes_received() const { return bytes_received_.load(std::memory_order_relaxed); }

    // Bytes currently queued and not yet taken by the consumer.
    std::size_t bytes_queued() const { return queued_.load(std::memory_order_acquire); }

    // -- Consumer side: the sink thread -------------------------------------

    bool try_dequeue(uint8_t& byte) {
        if (!queue_.try_dequeue(byte)) {
            return false;
        }
        queued_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    template <typename Rep, typename Period>
    bool wait_dequeue_timed(uint8_t& byte, std::chrono::duration<Rep, Period> timeout) {
        if (!queue_.wait_dequeue_timed(byte, timeout)) {
            return false;
        }
        queued_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

private:
    // Enqueue the held byte if the bounded queue has room. The depth is
    // enforced with queued_ rather than the queue's own block size, which is
    // rounded up to a power of two.
    bool offer_held_byte() {
        if (queued_.load(std::memory_order_acquire) >= options_.queue_depth) {
            return false;
        }
        if (!queue_.try_enqueue(held_byte_)) {
            return false;
        }
        queued_.fetch_add(1, std::memory_order_acq_rel);
        bytes_received_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    Options options_;
    std::atomic<bool> online_;
    moodycamel::BlockingReaderWriterQueue<uint8_t> queue_;
    std::atomic<std::size_t> queued_{0};
    std::atomic<uint64_t> bytes_received_{0};

    // Emulation-thread state
    uint8_t strobe_level_ = 1;
    uint8_t held_byte_ = 0;
    bool holding_ = false;
    bool acknowledging_ = false;
    uint32_t ticks_since_accept_ = 0;
};

}  // namespace beebium::printer

#endif  // BEEBIUM_PRINTER_CENTRONICS_RECEIVER_HPP
