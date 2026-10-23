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

#ifndef BEEBIUM_EXT_TCP_PRINTER_SINK_HPP
#define BEEBIUM_EXT_TCP_PRINTER_SINK_HPP

#include <beebium/printer/CentronicsReceiver.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beebium::net {
class TcpClientSerialPort;
}

namespace beebium::printer {

// Sends bytes from a CentronicsReceiver to a raw TCP printer port, such as a
// JetDirect printer on port 9100 or a printer emulator. The byte stream is
// all that crosses: no framing and no protocol. Anything the peer sends back
// is read only to notice the connection closing, and discarded.
//
// The receiver's online state follows the connection. While not connected it
// is offline, so the guest waits for /ACK as it would for a switched-off
// printer and the strobed byte is held; on connect it goes online and the
// held byte is accepted. On a write error or end of stream the sink goes
// offline, closes the socket and, unless reconnect is zero, tries again every
// reconnect interval. Bytes already acknowledged to the guest stay queued
// across a reconnect and are sent once connected again.
//
// Two threads of its own: a connection thread owns the socket's lifecycle and
// reads it to notice the close, and a writer thread drains the receiver into
// the socket. The emulation thread touches only the receiver. The destructor
// stops and joins both.
class TcpPrinterSink {
public:
    struct Options {
        std::string host = "localhost";
        std::uint16_t port = 9100;
        std::size_t buffer = 256;  // receiver queue depth in bytes
        // Wait between connection attempts; zero makes one attempt only.
        std::chrono::milliseconds reconnect{1000};
        // Called on the sink's threads when the connection changes or bytes
        // have been sent, for a panel that wants to redraw.
        std::function<void()> on_activity;
    };

    struct Status {
        std::string endpoint;  // host:port, an IPv6 host in brackets
        bool connected = false;
        uint64_t bytes_received = 0;
        uint64_t bytes_sent = 0;
    };

    explicit TcpPrinterSink(Options options);
    ~TcpPrinterSink();

    TcpPrinterSink(const TcpPrinterSink&) = delete;
    TcpPrinterSink& operator=(const TcpPrinterSink&) = delete;

    // The device to attach to the printer port.
    CentronicsReceiver& receiver() { return receiver_; }

    const Options& options() const { return options_; }

    // Safe from any thread.
    Status status() const;

    // Connection attempts completed so far, successful or not.
    int connect_attempts() const { return connect_attempts_.load(std::memory_order_acquire); }

private:
    static CentronicsReceiver::Options receiver_options(const Options& options);

    void connection_loop();
    void writer_loop();
    bool send_pending(net::TcpClientSerialPort& port);

    std::shared_ptr<net::TcpClientSerialPort> current_port() const;
    void go_online(std::shared_ptr<net::TcpClientSerialPort> port);
    void go_offline(const std::shared_ptr<net::TcpClientSerialPort>& port);
    // Wait up to `timeout` for stop or `wake`, whichever comes first.
    void wait_for(std::chrono::milliseconds timeout, const std::function<bool()>& wake);
    void notify();
    void notify_activity();

    const Options options_;
    const std::string endpoint_;
    CentronicsReceiver receiver_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<int> connect_attempts_{0};

    // The connection thread swaps the socket in and out; the writer takes a
    // reference to write through.
    mutable std::mutex port_mutex_;
    std::shared_ptr<net::TcpClientSerialPort> port_;

    // Wakes the threads on stop and on connect.
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;

    std::vector<uint8_t> pending_;  // writer thread only: dequeued, not yet sent

    std::thread connection_thread_;
    std::thread writer_thread_;
};

}  // namespace beebium::printer

#endif  // BEEBIUM_EXT_TCP_PRINTER_SINK_HPP
