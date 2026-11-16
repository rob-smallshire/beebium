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

// POSIX-only: exercises the host-serial extension against a real pty.

#include "HostSerialExtension.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "HostSerialDispatcher.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "host_serial.pb.h"

#include <map>
#endif

#include "extension_ui.pb.h"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/PtyMaster.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

using namespace beebium;

#ifdef BEEBIUM_BUILD_SERVICE
namespace {
// A minimal RpcContext for driving a dispatcher directly (no gRPC), exactly as
// the core's ExtensionRpc service feeds it serialized bytes at runtime.
class TestRpcContext final : public RpcContext {
public:
    bool is_cancelled() const override { return false; }
    const std::map<std::string, std::string>& metadata() const override {
        return metadata_;
    }
private:
    std::map<std::string, std::string> metadata_;
};

// Serialize `req`, invoke the dispatcher, parse the reply into `resp`.
template <typename Req, typename Resp>
RpcStatus call(ExtensionRpcDispatcher& d, std::string_view method,
               const Req& req, Resp* resp) {
    TestRpcContext ctx;
    std::string out;
    RpcStatus status = d.invoke(method, req.SerializeAsString(), out, ctx);
    if (status.is_ok() && resp != nullptr) {
        REQUIRE(resp->ParseFromString(out));
    }
    return status;
}
}  // namespace
#endif

namespace {
// 8N1 at the /64 counter divide, which is what MOS 1.20 uses on RS423: the
// Serial ULA supplies 64 receive and transmit clocks per bit at the selected rate.
constexpr uint8_t CONTROL_8N1 =
    Mc6850::COUNTER_DIVIDE_64 | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);
}  // namespace

TEST_CASE("HostSerialExtension attaches to the serial port (pty mode)",
          "[serial][host-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "pty"}});
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");

    ext.init(ctx);
    CHECK(port.is_occupied());

    // The panel reflects pty mode: heading "PTY Mode", and -- because a pty has
    // no physical line rate -- no baud anywhere (no display line, no editor
    // picker).
    View view;
    ext.ui()->build_view(&view);
    const auto& group = view.root().group();

    bool saw_pty_heading = false;
    bool saw_baud_display = false;
    const Control* device_editor = nullptr;
    for (const auto& control : group.controls()) {
        if (control.id() == "device_heading") {
            CHECK(control.label().text() == "PTY Mode");
            saw_pty_heading = true;
        }
        if (control.id() == "baud_display") {
            saw_baud_display = true;
        }
        if (control.id() == "device") {
            device_editor = &control;
        }
    }
    CHECK(saw_pty_heading);
    CHECK_FALSE(saw_baud_display);

    REQUIRE(device_editor != nullptr);
    for (const auto& field : device_editor->modal_editor().editor().group().controls()) {
        CHECK(field.id() != "baud");
    }

    ext.shutdown();
}

TEST_CASE("HostSerialExtension device mode requires a path",
          "[serial][host-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "device"}});  // no path
    CHECK_THROWS(ext.init(ctx));
    CHECK_FALSE(port.is_occupied());
}

TEST_CASE("HostSerialExtension bridges a transmitted byte to the host device",
          "[serial][host-serial]") {
    // Create a pty; the test holds the master, the extension opens the slave as
    // a device. Bytes the Beeb transmits should appear on the master end.
    auto pty = std::make_unique<serial::PtyMaster>();
    REQUIRE(pty->is_open());
    const std::string slave = pty->slave_path();

    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "device"}, {"path", slave}});
    ext.init(ctx);
    REQUIRE(port.is_occupied());

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // RS423, 19200, carrier
    socket.write_acia(1, 0x5A);                    // transmit 'Z'

    bool got = false;
    uint8_t value = 0;
    for (int attempt = 0; attempt < 20 && !got; ++attempt) {
        for (int i = 0; i < 600; ++i) {
            socket.tick_rising();
            socket.tick_falling();
        }
        std::array<std::uint8_t, 16> buf{};
        serial::ReadResult r = pty->read(std::span<std::uint8_t>(buf.data(), buf.size()));
        if (r.bytes > 0) {
            value = buf[0];
            got = true;
        }
    }
    REQUIRE(got);
    CHECK(value == 0x5A);

    ext.shutdown();
}

#ifdef BEEBIUM_BUILD_SERVICE
TEST_CASE("HostSerial dispatcher reports and re-points the bridge",
          "[serial][host-serial][extension-rpc]") {
    auto pty = std::make_unique<serial::PtyMaster>();
    REQUIRE(pty->is_open());
    const std::string slave = pty->slave_path();

    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    HostSerialExtension ext;
    ext.set_config({{"mode", "device"}, {"path", slave}, {"baud", "9600"}});

    // No dispatcher before init().
    CHECK(ext.rpc_dispatchers().empty());

    ext.init(ctx);
    REQUIRE(port.is_occupied());

    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    REQUIRE(dispatchers[0]->service_name() == "HostSerial");
    auto& dispatcher = *dispatchers[0];

    // GetConfig reflects the launch configuration.
    {
        HostSerialGetConfigRequest req;
        HostSerialConfig resp;
        REQUIRE(call(dispatcher, "GetConfig", req, &resp).is_ok());
        CHECK(resp.mode() == "device");
        CHECK(resp.path() == slave);
        CHECK(resp.baud() == 9600);
        CHECK(resp.serial_open());
    }

    // SetConfig is a partial update: changing only baud keeps the path. The
    // re-point applies on the next emulation tick (has_data()).
    {
        HostSerialSetConfigRequest req;
        req.set_baud(19200);
        HostSerialConfig resp;
        REQUIRE(call(dispatcher, "SetConfig", req, &resp).is_ok());
    }
    ext.endpoint()->has_data();  // drive process_pending_reopen
    {
        HostSerialGetConfigRequest req;
        HostSerialConfig resp;
        REQUIRE(call(dispatcher, "GetConfig", req, &resp).is_ok());
        CHECK(resp.baud() == 19200);
        CHECK(resp.path() == slave);   // kept (absent from the SetConfig diff)
        CHECK(resp.mode() == "device");
    }

    // A runtime switch to pty is rejected.
    {
        HostSerialSetConfigRequest req;
        req.set_mode("pty");
        CHECK(call(dispatcher, "SetConfig", req,
                   static_cast<HostSerialConfig*>(nullptr))
                  .code == kRpcInvalidArgument);
    }

    // An unknown method is UNIMPLEMENTED.
    {
        TestRpcContext bad_ctx;
        std::string out;
        RpcStatus unknown = dispatcher.invoke("Nope", "", out, bad_ctx);
        CHECK_FALSE(unknown.is_ok());
        CHECK(unknown.code == kRpcUnimplemented);
    }

    ext.shutdown();
}
#endif
