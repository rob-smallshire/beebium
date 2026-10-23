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

#include "HostParallelPrinterSink.hpp"

#include "PrinterDevice.hpp"

#include <memory>
#include <utility>

namespace beebium::printer {

using namespace std::chrono_literals;

namespace {

// How long the writer waits for a byte, or for the device to take one, before
// checking for stop again.
constexpr auto kWriterPoll = 20ms;

}  // namespace

HostParallelPrinterSink::HostParallelPrinterSink(Options options)
    : options_(std::move(options)), receiver_(receiver_options(options_)) {
    writer_thread_ = std::thread([this] { writer_loop(); });
}

HostParallelPrinterSink::~HostParallelPrinterSink() {
    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
        stop_.store(true, std::memory_order_release);
    }
    wake_cv_.notify_all();
    // A write may be waiting on a printer that never answers (a synchronous
    // WriteFile on Windows); keep cancelling until the thread is out, since a
    // cancel lands only on a write already in progress.
    while (writer_thread_.joinable() && !writer_done_.load(std::memory_order_acquire)) {
        PrinterDevice::cancel_blocked_write(writer_thread_);
        std::this_thread::sleep_for(kWriterPoll);
    }
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
}

CentronicsReceiver::Options HostParallelPrinterSink::receiver_options(const Options& options) {
    CentronicsReceiver::Options rx;
    rx.queue_depth = options.buffer;
    rx.online = false;  // until the device opens
    return rx;
}

HostParallelPrinterSink::Status HostParallelPrinterSink::status() const {
    return Status{options_.device, open_.load(std::memory_order_acquire),
                  receiver_.bytes_received(), bytes_sent_.load(std::memory_order_relaxed)};
}

void HostParallelPrinterSink::wait_for(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(wake_mutex_);
    wake_cv_.wait_for(lock, timeout, [this] { return stop_.load(std::memory_order_acquire); });
}

void HostParallelPrinterSink::notify_activity() {
    if (options_.on_activity) {
        options_.on_activity();
    }
}

void HostParallelPrinterSink::writer_loop() {
    PrinterDevice::prepare_writer_thread();

    std::unique_ptr<PrinterDevice> device;
    std::vector<uint8_t> pending;  // dequeued, not yet written
    bool attempted = false;

    // Offline before the flag drops, so whoever sees "not open" also sees a
    // receiver that is holding the next byte.
    auto close_device = [&] {
        receiver_.set_online(false);
        open_.store(false, std::memory_order_release);
        device.reset();
        notify_activity();
        if (options_.reconnect.count() > 0) {
            wait_for(options_.reconnect);
        }
    };

    while (!stop_.load(std::memory_order_acquire)) {
        if (!device) {
            if (attempted && options_.reconnect.count() == 0) {
                wait_for(1h);  // never retry: idle until stopped
                continue;
            }
            attempted = true;
            std::string error;
            device = PrinterDevice::open(options_.device, error);
            open_attempts_.fetch_add(1, std::memory_order_acq_rel);
            if (device) {
                receiver_.set_online(true);
                open_.store(true, std::memory_order_release);
                notify_activity();
            } else if (options_.reconnect.count() > 0) {
                wait_for(options_.reconnect);
            }
            continue;
        }

        if (pending.empty()) {
            if (device->has_failed()) {
                close_device();
                continue;
            }
            uint8_t byte = 0;
            if (!receiver_.wait_dequeue_timed(byte, kWriterPoll)) {
                continue;
            }
            pending.push_back(byte);
            while (receiver_.try_dequeue(byte)) {
                pending.push_back(byte);
            }
        }

        const auto result = device->write(pending.data(), pending.size(), kWriterPoll);
        if (result.bytes > 0) {
            pending.erase(pending.begin(),
                          pending.begin() + static_cast<std::ptrdiff_t>(result.bytes));
            bytes_sent_.fetch_add(result.bytes, std::memory_order_relaxed);
            notify_activity();
        }
        if (result.failed) {
            close_device();
        }
    }

    // A final pass for anything strobed before the stop, if the device will
    // take it without waiting.
    uint8_t byte = 0;
    while (receiver_.try_dequeue(byte)) {
        pending.push_back(byte);
    }
    while (device && !pending.empty()) {
        const auto result = device->write(pending.data(), pending.size(), 0ms);
        if (result.failed || result.bytes == 0) break;
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(result.bytes));
        bytes_sent_.fetch_add(result.bytes, std::memory_order_relaxed);
    }
    device.reset();
    writer_done_.store(true, std::memory_order_release);
}

}  // namespace beebium::printer
