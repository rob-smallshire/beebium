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


#ifndef BEEBIUM_EXT_FILE_PRINTER_SINK_HPP
#define BEEBIUM_EXT_FILE_PRINTER_SINK_HPP

#include <beebium/printer/CentronicsReceiver.hpp>
#include <beebium/printer/SinkOptions.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

namespace beebium::printer {

// Spools bytes from a CentronicsReceiver into a file on the host.
//
// Owns the receiver (the PrinterPortDevice the emulation thread talks to) and
// a sink thread that drains it with a timed wait, applies the line-ending
// translation, writes with a std::ofstream and flushes after each drain pass,
// so the file is current whenever the port is quiet. The destructor stops and
// joins the thread after a final drain.
class FilePrinterSink {
public:
    struct Options {
        std::filesystem::path path;          // created or truncated here
        LineEnding line_ending = LineEnding::Raw;
        std::size_t buffer = 256;            // receiver queue depth in bytes
        bool online = true;
        // Called on the sink thread after each drain pass, for a panel that
        // wants to redraw its counters.
        std::function<void()> on_activity;
    };

    struct Status {
        std::string path;
        uint64_t bytes_received = 0;
        uint64_t bytes_written = 0;
        bool online = true;
    };

    explicit FilePrinterSink(Options options)
        : options_(std::move(options))
        , receiver_(receiver_options(options_))
        , translator_(options_.line_ending)
        , file_(options_.path, std::ios::binary | std::ios::trunc) {
        if (!file_) {
            throw std::runtime_error("file-printer: cannot open '" +
                                     options_.path.string() + "' for writing");
        }
        sink_thread_ = std::thread([this] { sink_loop(); });
    }

    ~FilePrinterSink() {
        stop_.store(true, std::memory_order_release);
        if (sink_thread_.joinable()) {
            sink_thread_.join();
        }
    }

    FilePrinterSink(const FilePrinterSink&) = delete;
    FilePrinterSink& operator=(const FilePrinterSink&) = delete;

    // The device to attach to the printer port.
    CentronicsReceiver& receiver() { return receiver_; }

    void set_online(bool online) { receiver_.set_online(online); }

    // Safe from any thread.
    Status status() const {
        return Status{options_.path.string(), receiver_.bytes_received(),
                      bytes_written_.load(std::memory_order_relaxed), receiver_.is_online()};
    }

    const Options& options() const { return options_; }

private:
    static CentronicsReceiver::Options receiver_options(const Options& options) {
        CentronicsReceiver::Options rx;
        rx.queue_depth = options.buffer;
        rx.online = options.online;
        return rx;
    }

    void sink_loop() {
        using namespace std::chrono_literals;
        uint8_t byte = 0;
        while (!stop_.load(std::memory_order_acquire)) {
            if (receiver_.wait_dequeue_timed(byte, 20ms)) {
                drain_from(byte);
            }
        }
        // A final pass for anything strobed before the stop.
        if (receiver_.try_dequeue(byte)) {
            drain_from(byte);
        }
    }

    // Write `first` and everything else queued, then flush once.
    void drain_from(uint8_t first) {
        write_translated(first);
        uint8_t byte = 0;
        while (receiver_.try_dequeue(byte)) {
            write_translated(byte);
        }
        file_.flush();
        if (options_.on_activity) {
            options_.on_activity();
        }
    }

    void write_translated(uint8_t byte) {
        translator_.translate(byte, [this](uint8_t out) { put(out); });
    }

    void put(uint8_t byte) {
        file_.put(static_cast<char>(byte));
        bytes_written_.fetch_add(1, std::memory_order_relaxed);
    }

    Options options_;
    CentronicsReceiver receiver_;
    LineEndingTranslator translator_;  // sink thread only
    std::ofstream file_;
    std::atomic<uint64_t> bytes_written_{0};
    std::atomic<bool> stop_{false};
    std::thread sink_thread_;
};

}  // namespace beebium::printer

#endif  // BEEBIUM_EXT_FILE_PRINTER_SINK_HPP
