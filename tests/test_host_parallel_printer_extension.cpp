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

// No machine on the project has a parallel port or a USB printer, so these
// tests drive the sink through stand-ins that honour the same device-file
// contract: a regular file on every platform (open, write, close), and on
// POSIX a FIFO, whose missing reader looks to open() and write() as an absent
// or switched-off printer looks to the lp and usblp drivers.

#include <catch2/catch_test_macros.hpp>

#include "HostParallelPrinterExtension.hpp"
#include "HostParallelPrinterSink.hpp"
#include "PrinterDevice.hpp"
#ifdef BEEBIUM_BUILD_SERVICE
#include "HostParallelPrinterDispatcher.hpp"
#include "host_parallel_printer.pb.h"
#endif

#include "test_temp_paths.hpp"

#include <beebium/UserViaPeripheral.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include "extension_ui.pb.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace beebium;
using beebium::test::ScopedTempDir;
using namespace std::chrono_literals;

namespace {

std::string read_file(const std::filesystem::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void create_empty_file(const std::filesystem::path& filepath) {
    std::ofstream(filepath, std::ios::binary).flush();
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

printer::HostParallelPrinterSink::Options sink_options(
    const std::filesystem::path& device_path, std::chrono::milliseconds reconnect = 50ms) {
    printer::HostParallelPrinterSink::Options options;
    options.device = device_path.string();
    options.reconnect = reconnect;
    return options;
}

#ifndef _WIN32
// The read end of a FIFO, opened without blocking so that it exists before
// any writer does. Closing it is how the printer goes away.
class FifoReader {
public:
    explicit FifoReader(const std::filesystem::path& fifo_path)
        : fd_(::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) {}
    ~FifoReader() { close(); }

    FifoReader(const FifoReader&) = delete;
    FifoReader& operator=(const FifoReader&) = delete;

    bool is_open() const { return fd_ >= 0; }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // Everything readable now, without waiting.
    std::string drain() {
        char buf[256];
        while (fd_ >= 0) {
            const auto got = ::read(fd_, buf, sizeof(buf));
            if (got <= 0) break;
            received_.append(buf, static_cast<std::size_t>(got));
        }
        return received_;
    }

private:
    int fd_ = -1;
    std::string received_;
};
#endif

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

TEST_CASE("A bare LPT name is opened through the Win32 device namespace",
          "[printer-port][host-parallel-printer]") {
    CHECK(printer::win32_device_path("LPT1") == "\\\\.\\LPT1");
    CHECK(printer::win32_device_path("lpt2") == "\\\\.\\lpt2");
    CHECK(printer::win32_device_path("\\\\.\\LPT1") == "\\\\.\\LPT1");
    CHECK(printer::win32_device_path("\\\\.\\USBPRINT#Vid_04b8") == "\\\\.\\USBPRINT#Vid_04b8");
    CHECK(printer::win32_device_path("LPT") == "LPT");
    CHECK(printer::win32_device_path("LPT1x") == "LPT1x");
    CHECK(printer::win32_device_path("C:\\Temp\\out.prn") == "C:\\Temp\\out.prn");
}

TEST_CASE("list_printer_devices enumerates without error and names each device",
          "[printer-port][host-parallel-printer]") {
    // No machine on the project has a printer device, so this is usually
    // empty; whatever is found must be well formed.
    const auto devices = printer::list_printer_devices();
    for (const auto& device : devices) {
        INFO(device.path);
        CHECK_FALSE(device.name.empty());
#ifdef _WIN32
        CHECK(device.path.rfind("\\\\?\\", 0) == 0);
#else
        CHECK(std::filesystem::exists(device.path));
#endif
    }
}

TEST_CASE("HostParallelPrinterSink opens the device, goes online and writes the bytes in order",
          "[printer-port][host-parallel-printer]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";
    create_empty_file(device_path);

    {
        printer::HostParallelPrinterSink sink(sink_options(device_path));
        UserViaPeripheral peripheral;
        peripheral.attach_printer_port_device(sink.receiver());

        REQUIRE(wait_until(peripheral, [&] { return sink.status().open; }));
        CHECK(sink.receiver().is_online());
        CHECK(sink.status().device == device_path.string());

        strobe_string(peripheral, "HELLO\r");
        REQUIRE(wait_until(peripheral, [&] { return sink.status().bytes_sent == 6; }));
        CHECK(sink.status().bytes_received == 6);
    }
    // Read once the sink has closed the device: on Windows it holds it unshared.
    CHECK(read_file(device_path) == "HELLO\r");
}

TEST_CASE("HostParallelPrinterSink is offline until the device exists, then writes the held byte",
          "[printer-port][host-parallel-printer]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";

    {
        printer::HostParallelPrinterSink sink(sink_options(device_path));
        UserViaPeripheral peripheral;
        peripheral.attach_printer_port_device(sink.receiver());

        REQUIRE(wait_until(peripheral, [&] { return sink.open_attempts() >= 1; }));
        CHECK_FALSE(sink.status().open);
        CHECK_FALSE(sink.receiver().is_online());
        strobe_byte(peripheral, 'Z');
        CHECK(sink.status().bytes_received == 0);

        create_empty_file(device_path);
        REQUIRE(wait_until(peripheral, [&] { return sink.status().bytes_sent == 1; }));
        CHECK(sink.status().open);
        CHECK(sink.status().bytes_received == 1);
    }
    CHECK(read_file(device_path) == "Z");
}

TEST_CASE("HostParallelPrinterSink with reconnect 0 makes one attempt and never retries",
          "[printer-port][host-parallel-printer]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";

    printer::HostParallelPrinterSink sink(sink_options(device_path, 0ms));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    REQUIRE(wait_until(peripheral, [&] { return sink.open_attempts() == 1; }));
    create_empty_file(device_path);
    // A stillness check: with retries every 50 ms the device would be open
    // well inside this window.
    std::this_thread::sleep_for(300ms);
    CHECK(sink.open_attempts() == 1);
    CHECK_FALSE(sink.status().open);
}

#ifndef _WIN32
TEST_CASE("HostParallelPrinterSink follows a FIFO's reader: absent is offline, present is online",
          "[printer-port][host-parallel-printer][fifo]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto fifo_path = temp_dir / "lp0";
    REQUIRE(::mkfifo(fifo_path.c_str(), 0600) == 0);

    printer::HostParallelPrinterSink sink(sink_options(fifo_path));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    // No reader: open() fails with ENXIO, so the printer is offline.
    REQUIRE(wait_until(peripheral, [&] { return sink.open_attempts() >= 1; }));
    CHECK_FALSE(sink.status().open);
    CHECK_FALSE(sink.receiver().is_online());

    auto reader = std::make_unique<FifoReader>(fifo_path);
    REQUIRE(reader->is_open());
    REQUIRE(wait_until(peripheral, [&] { return sink.status().open; }));
    CHECK(sink.receiver().is_online());

    strobe_string(peripheral, "AB");
    REQUIRE(wait_until(peripheral, [&] { return reader->drain() == "AB"; }));

    // The reader goes away. The sink notices when it next writes (EPIPE), or
    // sooner where poll() reports the error on an idle FIFO (Linux), closes
    // the device and goes offline. Either way the byte is kept: held by the
    // receiver, or dequeued and not yet written.
    reader.reset();
    strobe_byte(peripheral, 'C');
    REQUIRE(wait_until(peripheral, [&] { return !sink.status().open; }));
    CHECK_FALSE(sink.receiver().is_online());

    // A new reader: the sink reopens and the byte arrives.
    reader = std::make_unique<FifoReader>(fifo_path);
    REQUIRE(reader->is_open());
    REQUIRE(wait_until(peripheral, [&] { return reader->drain() == "C"; }));
    CHECK(sink.status().open);
    CHECK(sink.status().bytes_received == 3);
    CHECK(sink.status().bytes_sent == 3);
}

TEST_CASE("HostParallelPrinterSink survives a write to a FIFO whose reader has gone",
          "[printer-port][host-parallel-printer][fifo]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto fifo_path = temp_dir / "lp0";
    REQUIRE(::mkfifo(fifo_path.c_str(), 0600) == 0);

    // Write directly through the device shim, with no chance to notice the
    // reader leaving first: the write fails (EPIPE) and does not raise a
    // SIGPIPE that would end the process.
    printer::PrinterDevice::prepare_writer_thread();
    FifoReader reader(fifo_path);
    REQUIRE(reader.is_open());
    std::string error;
    auto device = printer::PrinterDevice::open(fifo_path.string(), error);
    REQUIRE(device);
    reader.close();

    const uint8_t byte = 'X';
    const auto result = device->write(&byte, 1, 20ms);
    CHECK(result.failed);
    CHECK(result.bytes == 0);
    CHECK_FALSE(result.error.empty());
}
#endif

TEST_CASE("HostParallelPrinterExtension attaches a receiver to the printer port",
          "[printer-port][host-parallel-printer]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";
    create_empty_file(device_path);

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    HostParallelPrinterExtension ext;
    ext.set_config({{"device", device_path.string()}});
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "printer-port");

    ext.init(ctx);
    REQUIRE(port.is_occupied());
    REQUIRE(ext.sink() != nullptr);
    CHECK(ext.sink()->options().buffer == 256);
    CHECK(ext.sink()->options().reconnect == 1000ms);

    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().open; }));
    strobe_string(peripheral, "HI\r");
    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().bytes_sent == 3; }));

    ext.shutdown();
    CHECK(ext.sink() == nullptr);
    CHECK(read_file(device_path) == "HI\r");
}

TEST_CASE("HostParallelPrinterExtension requires a device and checks its options",
          "[printer-port][host-parallel-printer]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    SECTION("No device") {
        HostParallelPrinterExtension ext;
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
        CHECK_FALSE(port.is_occupied());
    }

    SECTION("A device that does not exist yet is accepted: the printer is offline") {
        HostParallelPrinterExtension ext;
        ext.set_config({{"device", device_path.string()}, {"reconnect_ms", "0"}});
        ext.init(ctx);
        CHECK(port.is_occupied());
        CHECK_FALSE(ext.sink()->status().open);
        ext.shutdown();
    }

    SECTION("buffer and reconnect_ms") {
        HostParallelPrinterExtension ext;
        ext.set_config({{"device", device_path.string()}, {"buffer", "16"},
                        {"reconnect_ms", "250"}});
        ext.init(ctx);
        CHECK(ext.sink()->receiver().options().queue_depth == 16);
        CHECK(ext.sink()->options().reconnect == 250ms);
        ext.shutdown();
    }

    SECTION("buffer must be a positive integer") {
        HostParallelPrinterExtension ext;
        ext.set_config({{"device", device_path.string()}, {"buffer", "0"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("reconnect_ms must be a non-negative integer") {
        HostParallelPrinterExtension ext;
        ext.set_config({{"device", device_path.string()}, {"reconnect_ms", "soon"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }
}

TEST_CASE("HostParallelPrinterUi shows the device, whether it is open and the counts",
          "[printer-port][host-parallel-printer][ui]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    HostParallelPrinterExtension ext;
    REQUIRE(ext.ui() != nullptr);

    SECTION("Before init the panel still builds") {
        View view;
        ext.ui()->build_view(&view);
        REQUIRE(view.root().control_case() == Control::kGroup);
        REQUIRE(view.root().group().controls_size() == 3);
        CHECK(view.root().group().controls(0).label().text() == "(unset)");
    }

    SECTION("After init, open") {
        create_empty_file(device_path);
        ext.set_config({{"device", device_path.string()}});
        ext.init(ctx);
        REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().open; }));
        strobe_string(peripheral, "AB");
        REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().bytes_sent == 2; }));

        View view;
        ext.ui()->build_view(&view);
        const auto& group = view.root().group();
        REQUIRE(group.controls_size() == 3);
        CHECK(group.controls(0).id() == "device");
        CHECK(group.controls(0).label().text() == device_path.string());
        CHECK(group.controls(1).id() == "state");
        CHECK(group.controls(1).label().text() == "Open");
        CHECK(group.controls(2).id() == "bytes");
        CHECK(group.controls(2).label().text() == "2 bytes received, 2 sent");
        ext.shutdown();
    }

    SECTION("After init, not open") {
        ext.set_config({{"device", device_path.string()}, {"reconnect_ms", "0"}});
        ext.init(ctx);
        View view;
        ext.ui()->build_view(&view);
        CHECK(view.root().group().controls(1).label().text() == "Not open");
        ext.shutdown();
    }
}

#ifdef BEEBIUM_BUILD_SERVICE
TEST_CASE("HostParallelPrinterDispatcher serves GetStatus",
          "[printer-port][host-parallel-printer][rpc]") {
    ScopedTempDir temp_dir("beebium-host-printer-");
    const auto device_path = temp_dir / "printer";
    create_empty_file(device_path);

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    HostParallelPrinterExtension ext;
    ext.set_config({{"device", device_path.string()}});
    ext.init(ctx);

    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    auto* dispatcher = dispatchers[0];
    CHECK(dispatcher->service_name() == "HostParallelPrinter");

    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().open; }));
    strobe_string(peripheral, "HI\r");
    REQUIRE(wait_until(peripheral, [&] { return ext.sink()->status().bytes_sent == 3; }));

    TestRpcContext rpc_ctx;
    std::string response;
    {
        HostParallelPrinterGetStatusRequest req;
        auto status = dispatcher->invoke("GetStatus", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        HostParallelPrinterStatus reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK(reply.device() == device_path.string());
        CHECK(reply.open());
        CHECK(reply.bytes_received() == 3);
        CHECK(reply.bytes_sent() == 3);
    }
    {
        HostParallelPrinterListDevicesRequest req;
        auto status =
            dispatcher->invoke("ListDevices", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        HostParallelPrinterDeviceList reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK(static_cast<std::size_t>(reply.devices_size()) ==
              printer::list_printer_devices().size());
    }
    {
        auto status = dispatcher->invoke("SetOnline", "", response, rpc_ctx);
        CHECK_FALSE(status.is_ok());
    }
    ext.shutdown();
}
#endif
