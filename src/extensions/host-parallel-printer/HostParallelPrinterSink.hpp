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

#ifndef BEEBIUM_EXT_HOST_PARALLEL_PRINTER_SINK_HPP
#define BEEBIUM_EXT_HOST_PARALLEL_PRINTER_SINK_HPP

#include <beebium/printer/CentronicsReceiver.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beebium::printer {

// Writes bytes from a CentronicsReceiver to a printer device on the host,
// such as /dev/usb/lp0 or LPT1, through PrinterDevice. Nothing is read from
// the device and nothing is configured on it.
//
// The receiver's online state follows the open device. While the device
// cannot be opened (absent, or the printer off) the receiver is offline, so
// the guest waits for /ACK as it would for a switched-off printer and the
// strobed byte is held; once the device opens it goes online and the held
// byte is accepted. When a write fails, or the device reports an error while
// idle, the sink goes offline, closes the device and, unless reconnect is
// zero, tries again every reconnect interval. Bytes already acknowledged to
// the guest stay queued across a reopen and are written once open again.
//
// One writer thread of its own owns the device: it opens, writes and closes
// it. The emulation thread touches only the receiver. The destructor stops
// and joins the thread.
class HostParallelPrinterSink {
public:
    struct Options {
        std::string device;        // the host device path
        std::size_t buffer = 256;  // receiver queue depth in bytes
        // Wait between open attempts; zero makes one attempt only.
        std::chrono::milliseconds reconnect{1000};
        // Called on the writer thread when the device opens or closes or
        // bytes have been written, for a panel that wants to redraw.
        std::function<void()> on_activity;
    };

    struct Status {
        std::string device;
        bool open = false;
        uint64_t bytes_received = 0;
        uint64_t bytes_sent = 0;
    };

    explicit HostParallelPrinterSink(Options options);
    ~HostParallelPrinterSink();

    HostParallelPrinterSink(const HostParallelPrinterSink&) = delete;
    HostParallelPrinterSink& operator=(const HostParallelPrinterSink&) = delete;

    // The device to attach to the printer port.
    CentronicsReceiver& receiver() { return receiver_; }

    const Options& options() const { return options_; }

    // Safe from any thread.
    Status status() const;

    // Open attempts completed so far, successful or not.
    int open_attempts() const { return open_attempts_.load(std::memory_order_acquire); }

private:
    static CentronicsReceiver::Options receiver_options(const Options& options);

    void writer_loop();
    // Wait up to `timeout`, or until stopped.
    void wait_for(std::chrono::milliseconds timeout);
    void notify_activity();

    const Options options_;
    CentronicsReceiver receiver_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> open_{false};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<int> open_attempts_{0};
    std::atomic<bool> writer_done_{false};

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;

    std::thread writer_thread_;
};

}  // namespace beebium::printer

#endif  // BEEBIUM_EXT_HOST_PARALLEL_PRINTER_SINK_HPP
