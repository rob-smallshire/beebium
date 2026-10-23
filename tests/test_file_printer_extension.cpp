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


#include <catch2/catch_test_macros.hpp>

#include "FilePrinterExtension.hpp"
#include "FilePrinterSink.hpp"
#ifdef BEEBIUM_BUILD_SERVICE
#include "FilePrinterDispatcher.hpp"
#include "file_printer.pb.h"
#endif

#include "test_temp_paths.hpp"

#include <beebium/UserViaPeripheral.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include "extension_ui.pb.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

using namespace beebium;
using beebium::test::ScopedTempDir;
using beebium::test::unique_temp_path;
using namespace std::chrono_literals;

namespace {

std::string read_file(const std::filesystem::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Strobe one byte through the composite's port A half: present the data,
// pulse /STROBE low for two ticks as the VIA does, then tick until the
// receiver's acknowledge pulse has passed.
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

void strobe_string(UserViaPeripheral& peripheral, const std::string& text) {
    for (unsigned char c : text) {
        strobe_byte(peripheral, c);
    }
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

// The sink thread writes asynchronously; wait, bounded, for the count.
bool wait_for_written(printer::FilePrinterSink& sink, uint64_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (sink.status().bytes_written >= count) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

}  // namespace

TEST_CASE("FilePrinterExtension attaches a receiver to the printer port and spools to the file",
          "[printer-port][file-printer]") {
    ScopedTempDir temp_dir("beebium-file-printer-");
    const auto out_filepath = temp_dir / "printout.txt";

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    FilePrinterExtension ext;
    ext.set_config({{"path", out_filepath.string()}});
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "printer-port");

    ext.init(ctx);
    REQUIRE(port.is_occupied());
    REQUIRE(std::filesystem::exists(out_filepath));
    REQUIRE(ext.sink() != nullptr);

    strobe_string(peripheral, "HELLO\r");
    REQUIRE(wait_for_written(*ext.sink(), 6));
    CHECK(ext.sink()->status().bytes_received == 6);
    CHECK(read_file(out_filepath) == "HELLO\r");

    ext.shutdown();
    CHECK(ext.sink() == nullptr);
    CHECK(read_file(out_filepath) == "HELLO\r");
}

TEST_CASE("FilePrinterExtension requires a path and checks its options",
          "[printer-port][file-printer]") {
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    SECTION("No path") {
        FilePrinterExtension ext;
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
        CHECK_FALSE(port.is_occupied());
    }

    SECTION("Unknown line ending") {
        ScopedTempDir temp_dir("beebium-file-printer-");
        FilePrinterExtension ext;
        ext.set_config({{"path", (temp_dir / "out.txt").string()}, {"line_ending", "cr"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("Buffer must be a positive integer") {
        ScopedTempDir temp_dir("beebium-file-printer-");
        FilePrinterExtension ext;
        ext.set_config({{"path", (temp_dir / "out.txt").string()}, {"buffer", "0"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("Unwritable path") {
        FilePrinterExtension ext;
        ext.set_config({{"path", (unique_temp_path("beebium-no-such-dir-") / "out.txt").string()}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
        CHECK_FALSE(port.is_occupied());
    }
}

TEST_CASE("FilePrinterSink translates the Beeb's line ends", "[printer-port][file-printer]") {
    ScopedTempDir temp_dir("beebium-file-printer-");
    const auto out_filepath = temp_dir / "out.txt";

    auto spool = [&](printer::LineEnding line_ending, const std::string& text) {
        printer::FilePrinterSink::Options options;
        options.path = out_filepath;
        options.line_ending = line_ending;
        UserViaPeripheral peripheral;
        {
            printer::FilePrinterSink sink(options);
            peripheral.attach_printer_port_device(sink.receiver());
            strobe_string(peripheral, text);
            REQUIRE(wait_for_written(sink, 1));
            // The destructor drains and flushes the rest.
        }
        return read_file(out_filepath);
    };

    CHECK(spool(printer::LineEnding::Raw, "A\rB\r\nC\n") == "A\rB\r\nC\n");
    CHECK(spool(printer::LineEnding::Lf, "A\rB\r\nC\n") == "A\nB\nC\n");
    CHECK(spool(printer::LineEnding::CrLf, "A\rB\r\nC\n") == "A\r\nB\r\nC\n");
}

TEST_CASE("FilePrinterSink offline holds the byte until set online", "[printer-port][file-printer]") {
    ScopedTempDir temp_dir("beebium-file-printer-");
    printer::FilePrinterSink::Options options;
    options.path = temp_dir / "out.txt";
    options.online = false;
    printer::FilePrinterSink sink(options);
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.receiver());

    CHECK_FALSE(sink.status().online);
    strobe_string(peripheral, "Z");
    CHECK(sink.status().bytes_received == 0);

    sink.set_online(true);
    CHECK(sink.status().online);
    uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
    for (int i = 0; i < 12; ++i) {
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    }
    REQUIRE(wait_for_written(sink, 1));
    CHECK(sink.status().bytes_received == 1);
}

TEST_CASE("FilePrinterUi shows the path, the counts and an Online toggle that sets the state",
          "[printer-port][file-printer][ui]") {
    ScopedTempDir temp_dir("beebium-file-printer-");
    const auto out_filepath = temp_dir / "printout.txt";

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    FilePrinterExtension ext;
    REQUIRE(ext.ui() != nullptr);

    SECTION("Before init the panel still builds") {
        View view;
        ext.ui()->build_view(&view);
        REQUIRE(view.root().control_case() == Control::kGroup);
        REQUIRE(view.root().group().controls_size() == 3);
        CHECK(view.root().group().controls(0).label().text() == "(unset)");
    }

    SECTION("After init") {
        ext.set_config({{"path", out_filepath.string()}});
        ext.init(ctx);
        strobe_string(peripheral, "AB");
        REQUIRE(wait_for_written(*ext.sink(), 2));

        View view;
        ext.ui()->build_view(&view);
        const auto& group = view.root().group();
        REQUIRE(group.controls_size() == 3);
        CHECK(group.controls(0).id() == "path");
        CHECK(group.controls(0).label().text() == out_filepath.string());
        CHECK(group.controls(1).id() == "bytes");
        CHECK(group.controls(1).label().text() == "2 bytes received, 2 written");
        CHECK(group.controls(2).id() == "online");
        REQUIRE(group.controls(2).control_case() == Control::kToggle);
        CHECK(group.controls(2).toggle().label() == "Online");
        CHECK(group.controls(2).toggle().value());

        DispatchRequest request;
        request.set_control_id("online");
        request.set_bool_value(false);
        ext.ui()->handle_event(request);
        CHECK_FALSE(ext.sink()->status().online);

        View after;
        ext.ui()->build_view(&after);
        CHECK_FALSE(after.root().group().controls(2).toggle().value());
    }
}

#ifdef BEEBIUM_BUILD_SERVICE
TEST_CASE("FilePrinterDispatcher serves GetStatus and SetOnline", "[printer-port][file-printer][rpc]") {
    ScopedTempDir temp_dir("beebium-file-printer-");
    const auto out_filepath = temp_dir / "printout.txt";

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    FilePrinterExtension ext;
    ext.set_config({{"path", out_filepath.string()}});
    ext.init(ctx);

    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    auto* dispatcher = dispatchers[0];
    CHECK(dispatcher->service_name() == "FilePrinter");

    strobe_string(peripheral, "HI\r");
    REQUIRE(wait_for_written(*ext.sink(), 3));

    TestRpcContext rpc_ctx;
    std::string response;
    {
        FilePrinterGetStatusRequest req;
        auto status = dispatcher->invoke("GetStatus", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        FilePrinterStatus reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK(reply.path() == out_filepath.string());
        CHECK(reply.bytes_received() == 3);
        CHECK(reply.bytes_written() == 3);
        CHECK(reply.online());
    }
    {
        FilePrinterSetOnlineRequest req;
        req.set_online(false);
        auto status = dispatcher->invoke("SetOnline", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        FilePrinterStatus reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK_FALSE(reply.online());
        CHECK_FALSE(ext.sink()->status().online);
    }
    {
        auto status = dispatcher->invoke("Nope", "", response, rpc_ctx);
        CHECK_FALSE(status.is_ok());
    }
}
#endif
