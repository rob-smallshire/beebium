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

#include "RpcSerialDispatcher.hpp"
#include "RpcSerialEndpoint.hpp"
#include "RpcSerialExtension.hpp"

#include <beebium/devices/Mc6850.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRpc.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

using namespace beebium;

namespace {
// 8N1 at the /64 counter divide, which is what MOS 1.20 uses on RS423: the
// Serial ULA supplies 64 receive and transmit clocks per bit at the selected rate.
constexpr uint8_t CONTROL_8N1 =
    Mc6850::COUNTER_DIVIDE_64 | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

// A minimal RpcContext for driving a dispatcher directly in a unit test (no
// gRPC). The dispatcher resolves remote calls to serialized bytes, exactly as
// the core's ExtensionRpc service feeds it at runtime.
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

TEST_CASE("RpcSerialExtension attaches and exposes a dispatcher",
          "[serial][rpc-serial]") {
    SerialSocket socket;
    SerialPort port(socket);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, &port);

    RpcSerialExtension ext;
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "serial-port");

    // No dispatcher before init().
    CHECK(ext.rpc_dispatchers().empty());

    ext.init(ctx);
    CHECK(port.is_occupied());
    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    CHECK(dispatchers[0]->service_name() == "RpcSerial");

    ext.shutdown();
}

TEST_CASE("RpcSerial dispatcher round-trips bytes through the BBC",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    SerialSocket socket;
    SerialPort port(socket);
    port.attach(endpoint);

    RpcSerialDispatcher dispatcher(endpoint);

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);

    // device -> BBC: Send injects a byte the BBC then receives.
    RpcSerialSendRequest send_req;
    send_req.set_data("Z");
    RpcSerialSendResponse send_resp;
    CHECK(call(dispatcher, "Send", send_req, &send_resp).is_ok());
    CHECK(send_resp.accepted() == 1);

    for (int i = 0; i < 6000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == static_cast<uint8_t>('Z'));

    // BBC -> device: transmit a byte, then Receive collects it.
    socket.write_acia(1, 0x5A);
    for (int i = 0; i < 6000; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    RpcSerialReceiveRequest recv_req;
    recv_req.set_max_bytes(0);
    RpcSerialReceiveResponse recv_resp;
    CHECK(call(dispatcher, "Receive", recv_req, &recv_resp).is_ok());
    REQUIRE(recv_resp.data().size() == 1);
    CHECK(static_cast<uint8_t>(recv_resp.data()[0]) == 0x5A);
}

TEST_CASE("RpcSerial dispatcher Send caps the receive queue and reports accepted",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    RpcSerialDispatcher dispatcher(endpoint);

    // Flood beyond the receive capacity with nothing draining it: Send accepts
    // only what fits and reports it -- it never blocks or grows unbounded.
    const std::size_t flood = RpcSerialEndpoint::kRxCapacity + 1000;
    RpcSerialSendRequest req;
    req.set_data(std::string(flood, 'A'));
    RpcSerialSendResponse resp;
    CHECK(call(dispatcher, "Send", req, &resp).is_ok());
    CHECK(resp.accepted() == RpcSerialEndpoint::kRxCapacity);

    // The queue is now full; a further send accepts nothing.
    RpcSerialSendResponse resp2;
    CHECK(call(dispatcher, "Send", req, &resp2).is_ok());
    CHECK(resp2.accepted() == 0);
}

TEST_CASE("RpcSerial dispatcher GetStatus reports the queue depths",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    RpcSerialDispatcher dispatcher(endpoint);

    // Queue some bytes in each direction without draining.
    RpcSerialSendRequest send_req;
    send_req.set_data("abc");  // device -> BBC (rx)
    RpcSerialSendResponse send_resp;
    CHECK(call(dispatcher, "Send", send_req, &send_resp).is_ok());
    endpoint.add_byte(0x10);   // BBC -> device (tx)
    endpoint.add_byte(0x11);

    RpcSerialStatusRequest status_req;
    RpcSerialStatus status;
    CHECK(call(dispatcher, "GetStatus", status_req, &status).is_ok());
    CHECK(status.rx_pending() == 3);
    CHECK(status.tx_pending() == 2);
}

TEST_CASE("RpcSerial dispatcher rejects unknown methods and bad requests",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    RpcSerialDispatcher dispatcher(endpoint);
    TestRpcContext ctx;

    // Unknown method -> UNIMPLEMENTED.
    std::string out;
    RpcStatus unknown = dispatcher.invoke("Nope", "", out, ctx);
    CHECK_FALSE(unknown.is_ok());
    CHECK(unknown.code == kRpcUnimplemented);

    // A request that is not a valid protobuf for the method -> INVALID_ARGUMENT.
    // (A long random byte string is overwhelmingly unlikely to parse as the
    // expected message.)
    std::string garbage(64, '\xff');
    RpcStatus bad = dispatcher.invoke("Send", garbage, out, ctx);
    CHECK_FALSE(bad.is_ok());
    CHECK(bad.code == kRpcInvalidArgument);
}

TEST_CASE("RpcSerialEndpoint bounds tx and signals back-pressure",
          "[serial][rpc-serial]") {
    RpcSerialEndpoint endpoint;
    CHECK(endpoint.accepts_more());

    for (std::size_t i = 0; i < endpoint.tx_back_pressure(); ++i) {
        endpoint.add_byte(0x41);
    }
    CHECK_FALSE(endpoint.accepts_more());  // at the back-pressure mark -> /CTS

    // Beyond the hard cap, bytes are dropped (a real ACIA loses data too) -- the
    // queue never grows without bound.
    for (std::size_t i = 0; i < 1000; ++i) endpoint.add_byte(0x42);
    CHECK(endpoint.tx_pending() <= endpoint.tx_hard_cap());
    CHECK(endpoint.tx_dropped() > 0);

    endpoint.drain();  // client collects everything
    CHECK(endpoint.accepts_more());  // back-pressure released
}
