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

// Test the ScratchRam operations tunnelled through the core's generic
// ExtensionRpc channel: a real server with the ExtensionRpc service over a
// registry holding the scratch-ram extension, driven by serializing each
// request, calling ExtensionRpc.Invoke(service="ScratchRamService"), and
// parsing the reply. Verifies extension-provided APIs are reachable through
// the channel and round-trip-consistent with 6502-side memory access.

#include <catch2/catch_test_macros.hpp>
#include "test_extension_ids.hpp"

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "TestScratchRam.hpp"

#include "scratch_ram.pb.h"
#include "extension_rpc.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <span>
#include <string>

namespace {

class ScratchRamGrpcFixture {
public:
    ScratchRamGrpcFixture() : service_(transports_, registry_) {
        machine_.reset();

        registry_.register_extension_point("1mhz-bus");
        registry_.register_extension(with_default_id(beebium::TestScratchRam::create()));

        beebium::ExtensionContext ctx(&machine_.state().memory.one_mhz_bus());
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

    ~ScratchRamGrpcFixture() {
        server_->stop();
        registry_.shutdown();
    }

    beebium::ModelB& machine() { return machine_; }

    // Serialize `req`, tunnel via ExtensionRpc.Invoke, parse the reply.
    template <typename Req, typename Resp>
    grpc::Status invoke(const std::string& method, const Req& req, Resp* resp) {
        grpc::ClientContext ctx;
        beebium::InvokeRequest ireq;
        ireq.set_service("ScratchRamService");
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

TEST_CASE("ScratchRamService Read returns zero initially", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    beebium::ScratchRamReadRequest request;
    beebium::ScratchRamReadResponse response;
    request.set_offset(0);

    auto status = fixture.invoke("Read", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.value() == 0);
}

TEST_CASE("ScratchRamService Write then Read round-trip", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write via gRPC
    {
        beebium::ScratchRamWriteRequest request;
        beebium::ScratchRamWriteResponse response;
        request.set_offset(3);
        request.set_value(0xAB);
        auto status = fixture.invoke("Write", request, &response);
        REQUIRE(status.ok());
    }

    // Read back via gRPC
    {
        beebium::ScratchRamReadRequest request;
        beebium::ScratchRamReadResponse response;
        request.set_offset(3);
        auto status = fixture.invoke("Read", request, &response);
        REQUIRE(status.ok());
        REQUIRE(response.value() == 0xAB);
    }
}

TEST_CASE("ScratchRamService ReadAll returns all 8 bytes", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write some values via gRPC
    for (uint32_t i = 0; i < 8; ++i) {
        beebium::ScratchRamWriteRequest request;
        beebium::ScratchRamWriteResponse response;
        request.set_offset(i);
        request.set_value(0x10 + i);
        fixture.invoke("Write", request, &response);
    }

    // ReadAll
    beebium::ScratchRamReadAllRequest request;
    beebium::ScratchRamReadAllResponse response;
    auto status = fixture.invoke("ReadAll", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.data().size() == 8);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(static_cast<uint8_t>(response.data()[i]) == 0x10 + i);
    }
}

TEST_CASE("ScratchRamService gRPC write visible to 6502 memory read", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write via gRPC
    {
        beebium::ScratchRamWriteRequest request;
        beebium::ScratchRamWriteResponse response;
        request.set_offset(5);
        request.set_value(0xCD);
        fixture.invoke("Write", request, &response);
    }

    // Read via 6502 memory map (address 0xFC00 + kBaseOffset + 5)
    REQUIRE(fixture.machine().state().memory.read(0xFC00 + beebium::TestScratchRam::kBaseOffset + 5) == 0xCD);
}

TEST_CASE("ScratchRamService 6502 write visible to gRPC read", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    // Write via 6502 memory map (address 0xFC00 + kBaseOffset + 2)
    fixture.machine().state().memory.write(0xFC00 + beebium::TestScratchRam::kBaseOffset + 2, 0xEF);

    // Read via gRPC
    beebium::ScratchRamReadRequest request;
    beebium::ScratchRamReadResponse response;
    request.set_offset(2);
    auto status = fixture.invoke("Read", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.value() == 0xEF);
}

TEST_CASE("ScratchRamService Read rejects invalid offset", "[grpc][extension][scratch-ram]") {
    ScratchRamGrpcFixture fixture;

    beebium::ScratchRamReadRequest request;
    beebium::ScratchRamReadResponse response;
    request.set_offset(8);  // out of range

    auto status = fixture.invoke("Read", request, &response);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}
