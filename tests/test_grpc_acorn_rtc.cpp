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

// Test the AUN-style AcornRtc operations tunnelled through the core's generic
// ExtensionRpc channel: a real server with the ExtensionRpc service over a
// registry holding the RTC extension, driven by serializing each request,
// calling ExtensionRpc.Invoke(service="AcornRtcService"), and parsing the
// reply -- exactly as the Python/TS clients do over the wire.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "AcornRtcExtension.hpp"

#include "acorn_rtc.pb.h"
#include "extension_rpc.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <map>
#include <span>
#include <string>

namespace {

class AcornRtcGrpcFixture {
public:
    // `layout` selects the SAF3019P register-layout variant (by default the
    // manifest's, 7-bit year in R7). `time` seeds the initial clock.
    explicit AcornRtcGrpcFixture(const std::string& layout = "7bit-year-in-r7",
                                 const std::string& time = "1985-06-15T14:30")
        : service_(transports_, registry_) {
        machine_.reset();

        registry_.register_extension_point("user-port");

        auto rtc = std::make_unique<beebium::AcornRtcExtension>();
        rtc->set_config({{"id", "test-rtc"}, {"time", time}, {"layout", layout}});
        registry_.register_extension(std::move(rtc));

        beebium::ExtensionContext ctx(
            nullptr,  // no 1MHz bus needed
            &machine_.state().memory.user_port());
        registry_.resolve_and_init(ctx);

        services_.push_back(&service_);
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start({}, {}, false, {}, nullptr,
                       std::span<grpc::Service*>(services_));

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::ExtensionRpc::NewStub(channel_);
    }

    ~AcornRtcGrpcFixture() {
        server_->stop();
        registry_.shutdown();
    }

    // Serialize `req`, tunnel via ExtensionRpc.Invoke, parse the reply.
    template <typename Req, typename Resp>
    grpc::Status invoke(const std::string& method, const Req& req, Resp* resp) {
        grpc::ClientContext ctx;
        beebium::InvokeRequest ireq;
        ireq.set_service("AcornRtcService");
        ireq.set_method(method);
        ireq.set_payload(req.SerializeAsString());
        beebium::InvokeResponse iresp;
        grpc::Status status = stub_->Invoke(&ctx, ireq, &iresp);
        if (status.ok() && resp != nullptr) {
            REQUIRE(resp->ParseFromString(iresp.payload()));
        }
        return status;
    }

private:
    beebium::ModelB machine_;
    beebium::EconetTransportRegistry transports_;
    beebium::ExtensionRegistry registry_;
    beebium::service::ExtensionRpcServiceImpl service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionRpc::Stub> stub_;
};

}  // namespace

TEST_CASE("AcornRtcService GetTime returns initialised values",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    beebium::GetRtcTimeRequest request;
    beebium::GetRtcTimeResponse response;

    auto status = fixture.invoke("GetTime", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.year() == 1985);
    REQUIRE(response.month() == 6);
    REQUIRE(response.day() == 15);
    REQUIRE(response.hour() == 14);
    REQUIRE(response.minute() == 30);
    REQUIRE(response.iso8601() == "1985-06-15T14:30");
}

TEST_CASE("AcornRtcService SetTime updates registers",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    // Set a new time
    {
        beebium::SetRtcTimeRequest request;
        beebium::SetRtcTimeResponse response;
        request.set_iso8601("1990-12-25T08:00");
        auto status = fixture.invoke("SetTime", request, &response);
        REQUIRE(status.ok());
    }

    // Verify it took effect
    {
        beebium::GetRtcTimeRequest request;
        beebium::GetRtcTimeResponse response;
        auto status = fixture.invoke("GetTime", request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.year() == 1990);
        REQUIRE(response.month() == 12);
        REQUIRE(response.day() == 25);
        REQUIRE(response.hour() == 8);
        REQUIRE(response.minute() == 0);
    }
}

TEST_CASE("AcornRtcService SetTime rejects out-of-range year",
          "[grpc][extension][acorn-rtc]") {
    // The Acorn original (4-bit-year) layout spans 1981-1996, so 2026 is out
    // of range.
    AcornRtcGrpcFixture fixture("4bit-year-in-r1");

    beebium::SetRtcTimeRequest request;
    beebium::SetRtcTimeResponse response;
    request.set_iso8601("2026-04-02T10:00");

    auto status = fixture.invoke("SetTime", request, &response);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("AcornRtcService SetTime accepts a contemporary year under the "
          "7-bit-year layout",
          "[grpc][extension][acorn-rtc]") {
    // The 7bit-year-in-r7 layout spans 1981-2099, so SetTime must respect the
    // active layout's range rather than a fixed 1981-2000 cap.
    AcornRtcGrpcFixture fixture("7bit-year-in-r7");

    {
        beebium::SetRtcTimeRequest request;
        beebium::SetRtcTimeResponse response;
        request.set_iso8601("2026-04-02T10:00");
        auto status = fixture.invoke("SetTime", request, &response);
        REQUIRE(status.ok());
    }
    {
        beebium::GetRtcTimeRequest request;
        beebium::GetRtcTimeResponse response;
        auto status = fixture.invoke("GetTime", request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.year() == 2026);
        REQUIRE(response.month() == 4);
        REQUIRE(response.day() == 2);
    }
}

TEST_CASE("AcornRtcService SetTime still rejects a year beyond the active "
          "layout's range",
          "[grpc][extension][acorn-rtc]") {
    // 2150 exceeds even the 7-bit-year layout's 1981-2099 range.
    AcornRtcGrpcFixture fixture("7bit-year-in-r7");

    beebium::SetRtcTimeRequest request;
    beebium::SetRtcTimeResponse response;
    request.set_iso8601("2150-01-01T00:00");

    auto status = fixture.invoke("SetTime", request, &response);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("AcornRtcService GetRegisters returns 8 values",
          "[grpc][extension][acorn-rtc]") {
    // The Acorn original layout, whose year lives in register 1.
    AcornRtcGrpcFixture fixture("4bit-year-in-r1");

    beebium::GetRtcRegistersRequest request;
    beebium::GetRtcRegistersResponse response;

    auto status = fixture.invoke("GetRegisters", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.registers_size() == 8);

    // Verify counter register values (BCD encoded)
    REQUIRE(response.registers(0) == 0x06);  // month = June
    REQUIRE(response.registers(2) == 0x15);  // day = 15
    REQUIRE(response.registers(4) == 0x14);  // hour = 14
    REQUIRE(response.registers(6) == 0x30);  // minute = 30
    // Year register: 1985 - 1981 = 4
    REQUIRE(response.registers(1) == 0x04);
}

TEST_CASE("AcornRtcService SetRegister applies wrapping",
          "[grpc][extension][acorn-rtc]") {
    AcornRtcGrpcFixture fixture;

    // Write BCD 0x71 to register 7 (minute alarm, wrapping test)
    {
        beebium::SetRtcRegisterRequest request;
        beebium::SetRtcRegisterResponse response;
        request.set_register_index(7);
        request.set_bcd_value(0x71);
        auto status = fixture.invoke("SetRegister", request, &response);
        REQUIRE(status.ok());
    }

    // Read back and verify wrapping: 0x71 (113) wraps to 13 (0x0D)
    {
        beebium::GetRtcRegistersRequest request;
        beebium::GetRtcRegistersResponse response;
        auto status = fixture.invoke("GetRegisters", request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.registers(7) == 13);  // raw byte 13 = 0x0D
    }
}
