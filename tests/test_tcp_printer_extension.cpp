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

// SocketPlatform.hpp first so <winsock2.h> wins the include race on Windows.
#include <beebium/net/SocketPlatform.hpp>

#include <catch2/catch_test_macros.hpp>

#include "TcpPrinterExtension.hpp"
#include "TcpPrinterSink.hpp"
#ifdef BEEBIUM_BUILD_SERVICE
#include "TcpPrinterDispatcher.hpp"
#include "tcp_printer.pb.h"
#endif

#include <beebium/UserViaPeripheral.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include "extension_ui.pb.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace beebium;
using namespace std::chrono_literals;

namespace {

// A TCP listener on 127.0.0.1 that records everything its clients send, as a
// printer on port 9100 would, and sends nothing back. Binds the given port, or
// an ephemeral one when it is 0. Destroying it closes the client and the
// listening socket, which is how a printer going away looks to the sink.
class RecordingTcpListener {
public:
    explicit RecordingTcpListener(std::uint16_t port = 0) {
        net::ensure_winsock_initialized();
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one), sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        bound_ = ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                 ::listen(listen_fd_, 4) == 0;
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &len);
        port_ = ntohs(bound.sin_port);
        thread_ = std::thread([this] { run(); });
    }

    ~RecordingTcpListener() {
        stop_.store(true);
        ::shutdown(listen_fd_, net::kShutdownBoth);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (client_fd_ != net::kInvalidSocket) {
                ::shutdown(client_fd_, net::kShutdownBoth);
            }
        }
        if (thread_.joinable()) thread_.join();
        net::close_socket(listen_fd_);
    }

    RecordingTcpListener(const RecordingTcpListener&) = delete;
    RecordingTcpListener& operator=(const RecordingTcpListener&) = delete;

    bool bound() const { return bound_; }
    std::uint16_t port() const { return port_; }
    int clients_accepted() const { return clients_accepted_.load(); }

    std::string received() {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_;
    }

private:
    void run() {
        while (!stop_.load()) {
            if (!wait_readable(listen_fd_)) continue;
            net::socket_t c = ::accept(listen_fd_, nullptr, nullptr);
            if (c == net::kInvalidSocket) continue;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                client_fd_ = c;
            }
            ++clients_accepted_;
            record(c);
            std::lock_guard<std::mutex> lock(mutex_);
            net::close_socket(c);
            client_fd_ = net::kInvalidSocket;
        }
    }

    void record(net::socket_t c) {
        char buf[256];
        while (!stop_.load()) {
            if (!wait_readable(c)) continue;
            auto got = ::recv(c, buf, static_cast<int>(sizeof(buf)), 0);
            if (got <= 0) return;
            std::lock_guard<std::mutex> lock(mutex_);
            received_.append(buf, static_cast<std::size_t>(got));
        }
    }

    // True when fd is readable within 50 ms (or has failed, which recv reports).
    static bool wait_readable(net::socket_t fd) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(fd, &rset);
        timeval tv{};
        tv.tv_usec = 50 * 1000;
        return ::select(static_cast<int>(fd) + 1, &rset, nullptr, nullptr, &tv) != 0;
    }

    net::socket_t listen_fd_ = net::kInvalidSocket;
    bool bound_ = false;
    std::uint16_t port_ = 0;
    std::mutex mutex_;
    net::socket_t client_fd_ = net::kInvalidSocket;
    std::string received_;
    std::atomic<int> clients_accepted_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// A port on 127.0.0.1 with nothing listening: bind an ephemeral one and close it.
std::uint16_t unused_port() {
    std::uint16_t port = 0;
    {
        RecordingTcpListener probe;
        port = probe.port();
    }
    return port;
}

// Present the byte, pulse /STROBE low for two ticks as the VIA does, then
// tick past the receiver's acknowledge pulse. This thread stands in for the
// emulation thread.
void strobe_byte(UserViaPeripheral& peripheral, uint8_t byte) {
    uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
    peripheral.update_port_a(byte, 0xFF);
    ca2 = 0;
    peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    ca2 = 1;
    for (int i = 0; i < 12; ++i) {
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    }
}

// Like the MOS, which waits for /ACK before the next byte, a caller strobes a
// string only into an online receiver: offline, each strobe replaces the held
// byte.
void strobe_string(UserViaPeripheral& peripheral, const std::string& text) {
    for (unsigned char c : text) {
        strobe_byte(peripheral, c);
    }
}

// Poll `done` for up to five seconds. While it waits it keeps ticking the
// port, as the VIA would, so a held byte is accepted as soon as the receiver
// is online again.
bool wait_until(UserViaPeripheral& peripheral, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        std::this_thread::sleep_for(2ms);
    }
    return done();
}

printer::TcpPrinterSink::Options sink_options(std::uint16_t port,
                                              std::chrono::milliseconds reconnect = 50ms) {
    printer::TcpPrinterSink::Options options;
    options.host = "127.0.0.1";
    options.port = port;
    options.reconnect = reconnect;
    return options;
}

#ifdef BEEBIUM_BUILD_SERVICE
// A minimal RpcContext for driving a dispatcher directly (no gRPC).
class TestRpcContext final : public RpcContext {
public:
    bool is_cancelled() const override { return false; }
    const std::map<std::string, std::string>& metadata() const override { return metadata_; }
private:
    std::map<std::string, std::string> metadata_;
};
#endif

}  // namespace

TEST_CASE("TcpPrinterSink connects, goes online and sends the bytes in order",
          "[printer-port][tcp-printer]") {
    RecordingTcpListener listener;
    REQUIRE(listener.bound());

    printer::TcpPrinterSink sink(sink_options(listener.port()));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    REQUIRE(wait_until(peripheral, [&] { return sink.status().connected; }));
    CHECK(sink.receiver().is_online());
    CHECK(sink.status().endpoint == "127.0.0.1:" + std::to_string(listener.port()));

    strobe_string(peripheral, "HELLO\r");
    REQUIRE(wait_until(peripheral, [&] { return listener.received() == "HELLO\r"; }));
    REQUIRE(wait_until(peripheral, [&] { return sink.status().bytes_sent == 6; }));
    CHECK(sink.status().bytes_received == 6);
}

TEST_CASE("TcpPrinterSink is offline until the printer is listening, then sends the held byte",
          "[printer-port][tcp-printer]") {
    const std::uint16_t port = unused_port();
    printer::TcpPrinterSink sink(sink_options(port));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    CHECK_FALSE(sink.status().connected);
    CHECK_FALSE(sink.receiver().is_online());
    strobe_byte(peripheral, 'Z');
    CHECK(sink.status().bytes_received == 0);

    RecordingTcpListener listener(port);
    REQUIRE(listener.bound());
    REQUIRE(wait_until(peripheral, [&] { return listener.received() == "Z"; }));
    CHECK(sink.status().connected);
    CHECK(sink.status().bytes_received == 1);
}

TEST_CASE("TcpPrinterSink goes offline when the printer goes away and reconnects when it returns",
          "[printer-port][tcp-printer]") {
    auto listener = std::make_unique<RecordingTcpListener>();
    REQUIRE(listener->bound());
    const std::uint16_t port = listener->port();

    printer::TcpPrinterSink sink(sink_options(port));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    REQUIRE(wait_until(peripheral, [&] { return sink.status().connected; }));
    strobe_string(peripheral, "AB");
    REQUIRE(wait_until(peripheral, [&] { return listener->received() == "AB"; }));

    listener.reset();
    REQUIRE(wait_until(peripheral, [&] { return !sink.status().connected; }));
    CHECK_FALSE(sink.receiver().is_online());

    strobe_byte(peripheral, 'C');
    CHECK(sink.status().bytes_received == 2);

    listener = std::make_unique<RecordingTcpListener>(port);
    REQUIRE(listener->bound());
    REQUIRE(wait_until(peripheral, [&] { return listener->received() == "C"; }));
    CHECK(sink.status().connected);
    CHECK(sink.status().bytes_received == 3);
    CHECK(sink.status().bytes_sent == 3);
}

TEST_CASE("TcpPrinterSink with reconnect 0 makes one attempt and never retries",
          "[printer-port][tcp-printer]") {
    const std::uint16_t port = unused_port();
    printer::TcpPrinterSink sink(sink_options(port, 0ms));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    REQUIRE(wait_until(peripheral, [&] { return sink.connect_attempts() == 1; }));

    RecordingTcpListener listener(port);
    REQUIRE(listener.bound());
    // A stillness check: with retries every 50 ms a client would arrive well
    // inside this window.
    std::this_thread::sleep_for(300ms);
    CHECK(listener.clients_accepted() == 0);
    CHECK(sink.connect_attempts() == 1);
    CHECK_FALSE(sink.status().connected);
}

TEST_CASE("TcpPrinterExtension attaches a receiver to the printer port",
          "[printer-port][tcp-printer]") {
    RecordingTcpListener listener;
    REQUIRE(listener.bound());

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    TcpPrinterExtension ext;
    ext.set_config({{"host", "127.0.0.1"}, {"port", std::to_string(listener.port())}});
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "printer-port");

    ext.init(ctx);
    REQUIRE(port.is_occupied());
    REQUIRE(ext.sink() != nullptr);

    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().connected; }));
    strobe_string(peripheral, "HI\r");
    REQUIRE(wait_until(peripheral, [&] { return listener.received() == "HI\r"; }));

    ext.shutdown();
    CHECK(ext.sink() == nullptr);
}

TEST_CASE("TcpPrinterExtension reads its endpoint and checks its options",
          "[printer-port][tcp-printer]") {
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    // An endpoint nothing listens on: the sink just stays offline.
    const std::string free_port = std::to_string(unused_port());

    SECTION("Defaults to localhost:9100") {
        TcpPrinterExtension ext;
        ext.set_config({{"reconnect_ms", "0"}});
        ext.init(ctx);
        CHECK(ext.sink()->status().endpoint == "localhost:9100");
        ext.shutdown();
    }

    SECTION("url gives host and port, with or without a scheme") {
        TcpPrinterExtension ext;
        ext.set_config({{"url", "socket://127.0.0.1:" + free_port}});
        ext.init(ctx);
        CHECK(ext.sink()->status().endpoint == "127.0.0.1:" + free_port);
        CHECK(ext.sink()->options().reconnect == 1000ms);
        CHECK(ext.sink()->options().buffer == 256);
        ext.shutdown();
    }

    SECTION("buffer and reconnect_ms") {
        TcpPrinterExtension ext;
        ext.set_config({{"host", "127.0.0.1"}, {"port", free_port},
                        {"buffer", "16"}, {"reconnect_ms", "250"}});
        ext.init(ctx);
        CHECK(ext.sink()->options().buffer == 16);
        CHECK(ext.sink()->receiver().options().queue_depth == 16);
        CHECK(ext.sink()->options().reconnect == 250ms);
        ext.shutdown();
    }

    SECTION("url cannot be combined with host or port") {
        TcpPrinterExtension ext;
        ext.set_config({{"url", "127.0.0.1:9100"}, {"port", "9100"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
        CHECK_FALSE(port.is_occupied());
    }

    SECTION("A malformed url") {
        TcpPrinterExtension ext;
        ext.set_config({{"url", "127.0.0.1"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("port must be 1..65535") {
        TcpPrinterExtension ext;
        ext.set_config({{"port", "70000"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("buffer must be a positive integer") {
        TcpPrinterExtension ext;
        ext.set_config({{"port", free_port}, {"buffer", "0"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("reconnect_ms must be a non-negative integer") {
        TcpPrinterExtension ext;
        ext.set_config({{"port", free_port}, {"reconnect_ms", "-1"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }
}

TEST_CASE("TcpPrinterUi shows the endpoint, the connection and the counts",
          "[printer-port][tcp-printer][ui]") {
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    TcpPrinterExtension ext;
    REQUIRE(ext.ui() != nullptr);

    SECTION("Before init the panel still builds") {
        View view;
        ext.ui()->build_view(&view);
        REQUIRE(view.root().control_case() == Control::kGroup);
        REQUIRE(view.root().group().controls_size() == 3);
        CHECK(view.root().group().controls(0).label().text() == "(unset)");
    }

    SECTION("After init, connected") {
        RecordingTcpListener listener;
        REQUIRE(listener.bound());
        const std::string endpoint = "127.0.0.1:" + std::to_string(listener.port());
        ext.set_config({{"url", endpoint}});
        ext.init(ctx);
        REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().connected; }));
        strobe_string(peripheral, "AB");
        REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().bytes_sent == 2; }));

        View view;
        ext.ui()->build_view(&view);
        const auto& group = view.root().group();
        REQUIRE(group.controls_size() == 3);
        CHECK(group.controls(0).id() == "endpoint");
        CHECK(group.controls(0).label().text() == endpoint);
        CHECK(group.controls(1).id() == "connection");
        CHECK(group.controls(1).label().text() == "Connected");
        CHECK(group.controls(2).id() == "bytes");
        CHECK(group.controls(2).label().text() == "2 bytes received, 2 sent");
        ext.shutdown();
    }

    SECTION("After init, not connected") {
        ext.set_config({{"host", "127.0.0.1"}, {"port", std::to_string(unused_port())}});
        ext.init(ctx);
        View view;
        ext.ui()->build_view(&view);
        CHECK(view.root().group().controls(1).label().text() == "Not connected");
        ext.shutdown();
    }
}

#ifdef BEEBIUM_BUILD_SERVICE
TEST_CASE("TcpPrinterDispatcher serves GetStatus", "[printer-port][tcp-printer][rpc]") {
    RecordingTcpListener listener;
    REQUIRE(listener.bound());

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    TcpPrinterExtension ext;
    ext.set_config({{"host", "127.0.0.1"}, {"port", std::to_string(listener.port())}});
    ext.init(ctx);

    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    auto* dispatcher = dispatchers[0];
    CHECK(dispatcher->service_name() == "TcpPrinter");

    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().connected; }));
    strobe_string(peripheral, "HI\r");
    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().bytes_sent == 3; }));

    TestRpcContext rpc_ctx;
    std::string response;
    {
        TcpPrinterGetStatusRequest req;
        auto status = dispatcher->invoke("GetStatus", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        TcpPrinterStatus reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK(reply.endpoint() == "127.0.0.1:" + std::to_string(listener.port()));
        CHECK(reply.connected());
        CHECK(reply.bytes_received() == 3);
        CHECK(reply.bytes_sent() == 3);
    }
    {
        auto status = dispatcher->invoke("SetOnline", "", response, rpc_ctx);
        CHECK_FALSE(status.is_ok());
    }
    ext.shutdown();
}
#endif
