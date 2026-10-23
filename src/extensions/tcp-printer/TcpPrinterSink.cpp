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

#include "TcpPrinterSink.hpp"

#include <beebium/net/TcpClientSerialPort.hpp>

#include <span>
#include <utility>

namespace beebium::printer {

using namespace std::chrono_literals;

namespace {

// Connect timeout for one attempt. Bounds teardown while a connect to an
// unreachable host is in flight.
constexpr auto kConnectTimeout = 2s;

// How long the writer waits for a byte, or for a connection, before checking
// for stop again.
constexpr auto kWriterPoll = 20ms;

// Pause before retrying a write the kernel could not take (EAGAIN).
constexpr auto kWriteRetry = 2ms;

std::string format_endpoint(const std::string& host, std::uint16_t port) {
    const bool ipv6_literal = host.find(':') != std::string::npos;
    return (ipv6_literal ? "[" + host + "]" : host) + ":" + std::to_string(port);
}

}  // namespace

TcpPrinterSink::TcpPrinterSink(Options options)
    : options_(std::move(options))
    , endpoint_(format_endpoint(options_.host, options_.port))
    , receiver_(receiver_options(options_)) {
    connection_thread_ = std::thread([this] { connection_loop(); });
    writer_thread_ = std::thread([this] { writer_loop(); });
}

TcpPrinterSink::~TcpPrinterSink() {
    stop_.store(true, std::memory_order_release);
    notify();
    // The writer goes first, so its final pass can still use an open socket.
    if (writer_thread_.joinable()) writer_thread_.join();
    if (auto port = current_port()) {
        port->close();  // unblock a read() parked in the connection thread
    }
    if (connection_thread_.joinable()) connection_thread_.join();
}

CentronicsReceiver::Options TcpPrinterSink::receiver_options(const Options& options) {
    CentronicsReceiver::Options rx;
    rx.queue_depth = options.buffer;
    rx.online = false;  // until connected
    return rx;
}

TcpPrinterSink::Status TcpPrinterSink::status() const {
    return Status{endpoint_, connected_.load(std::memory_order_acquire),
                  receiver_.bytes_received(), bytes_sent_.load(std::memory_order_relaxed)};
}

std::shared_ptr<net::TcpClientSerialPort> TcpPrinterSink::current_port() const {
    std::lock_guard<std::mutex> lock(port_mutex_);
    return port_;
}

void TcpPrinterSink::go_online(std::shared_ptr<net::TcpClientSerialPort> port) {
    {
        std::lock_guard<std::mutex> lock(port_mutex_);
        port_ = std::move(port);
    }
    receiver_.set_online(true);
    connected_.store(true, std::memory_order_release);
    notify();
    notify_activity();
}

void TcpPrinterSink::go_offline(const std::shared_ptr<net::TcpClientSerialPort>& port) {
    {
        std::lock_guard<std::mutex> lock(port_mutex_);
        if (port_ != port) {
            return;  // the other thread has already dropped it
        }
        port_.reset();
    }
    // Offline before the flag drops, so whoever sees "not connected" also
    // sees a receiver that is holding the next byte.
    receiver_.set_online(false);
    connected_.store(false, std::memory_order_release);
    port->close();
    notify_activity();
}

void TcpPrinterSink::wait_for(std::chrono::milliseconds timeout,
                              const std::function<bool()>& wake) {
    std::unique_lock<std::mutex> lock(wake_mutex_);
    wake_cv_.wait_for(lock, timeout, [&] {
        return stop_.load(std::memory_order_acquire) || (wake && wake());
    });
}

void TcpPrinterSink::notify() {
    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
    }
    wake_cv_.notify_all();
}

void TcpPrinterSink::notify_activity() {
    if (options_.on_activity) {
        options_.on_activity();
    }
}

void TcpPrinterSink::connection_loop() {
    std::vector<std::uint8_t> discard(256);
    bool attempted = false;

    while (!stop_.load(std::memory_order_acquire)) {
        std::shared_ptr<net::TcpClientSerialPort> port = current_port();

        if (!port) {
            if (attempted && options_.reconnect.count() == 0) {
                wait_for(1h, {});  // never retry: idle until stopped
                continue;
            }
            attempted = true;
            auto fresh = std::make_shared<net::TcpClientSerialPort>(options_.host, options_.port,
                                                                    kConnectTimeout);
            connect_attempts_.fetch_add(1, std::memory_order_acq_rel);
            if (stop_.load(std::memory_order_acquire)) break;
            if (fresh->is_open()) {
                go_online(std::move(fresh));
            } else if (options_.reconnect.count() > 0) {
                wait_for(options_.reconnect, {});
            }
            continue;
        }

        // A printer port sends nothing that matters; read only to notice the
        // peer closing. read() waits up to 100 ms.
        const serial::ReadResult r =
            port->read(std::span<std::uint8_t>(discard.data(), discard.size()));
        if (r.error) {
            go_offline(port);
            if (options_.reconnect.count() > 0) {
                wait_for(options_.reconnect, {});
            }
        }
    }
}

void TcpPrinterSink::writer_loop() {
    while (!stop_.load(std::memory_order_acquire)) {
        if (pending_.empty()) {
            uint8_t byte = 0;
            if (!receiver_.wait_dequeue_timed(byte, kWriterPoll)) {
                continue;
            }
            pending_.push_back(byte);
            while (receiver_.try_dequeue(byte)) {
                pending_.push_back(byte);
            }
        }

        std::shared_ptr<net::TcpClientSerialPort> port = current_port();
        if (!port) {
            // Bytes acknowledged before a drop wait here for the reconnect.
            wait_for(kWriterPoll, [this] { return connected_.load(std::memory_order_acquire); });
            continue;
        }
        if (!send_pending(*port)) {
            go_offline(port);
        }
    }

    // A final pass for anything strobed before the stop, if the socket will
    // take it without waiting.
    uint8_t byte = 0;
    while (receiver_.try_dequeue(byte)) {
        pending_.push_back(byte);
    }
    if (auto port = current_port()) {
        std::size_t sent = 0;
        while (sent < pending_.size()) {
            const serial::WriteResult w = port->write(
                std::span<const uint8_t>(pending_.data() + sent, pending_.size() - sent));
            if (w.error || w.bytes == 0) break;
            sent += w.bytes;
        }
        bytes_sent_.fetch_add(sent, std::memory_order_relaxed);
    }
}

// Write everything pending. False on a write error; the unsent remainder stays
// pending for the next connection.
bool TcpPrinterSink::send_pending(net::TcpClientSerialPort& port) {
    std::size_t sent = 0;
    bool ok = true;
    while (sent < pending_.size() && !stop_.load(std::memory_order_acquire)) {
        const serial::WriteResult w = port.write(
            std::span<const uint8_t>(pending_.data() + sent, pending_.size() - sent));
        if (w.error) {
            ok = false;
            break;
        }
        sent += w.bytes;
        if (sent < pending_.size()) {
            std::this_thread::sleep_for(kWriteRetry);  // kernel buffer full
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(sent));
    if (sent > 0) {
        bytes_sent_.fetch_add(sent, std::memory_order_relaxed);
        notify_activity();
    }
    return ok;
}

}  // namespace beebium::printer
