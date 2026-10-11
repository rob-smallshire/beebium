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

// Tests for the SCSI host-adapter operations tunnelled through the core's
// generic ExtensionRpc channel: a real server with the ExtensionRpc service
// over a registry holding the SCSI extension, driven by serializing each
// request, calling ExtensionRpc.Invoke(service="ScsiHostAdapterService"), and
// parsing the reply -- exactly as the Python/TS clients do over the wire.

#include <catch2/catch_test_macros.hpp>
#include "test_extension_ids.hpp"

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include <AcornScsiHostAdapter.hpp>
#include <ScsiTestDevice.hpp>

#include "scsi_host_adapter.pb.h"
#include "extension_rpc.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <span>
#include <string>

namespace {

class ScsiGrpcFixture {
public:
    ScsiGrpcFixture() : service_(transports_, registry_) {
        machine_.reset();

        registry_.register_extension_point("1mhz-bus");
        auto ext = beebium::AcornScsiHostAdapter::create();
        adapter_ = ext.get();
        registry_.register_extension(with_default_id(std::move(ext)));

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

    ~ScsiGrpcFixture() {
        server_->stop();
        registry_.shutdown();
    }

    beebium::AcornScsiHostAdapter& adapter() { return *adapter_; }

    // Serialize `req`, tunnel via ExtensionRpc.Invoke, parse the reply.
    template <typename Req, typename Resp>
    grpc::Status invoke(const std::string& method, const Req& req, Resp* resp) {
        grpc::ClientContext ctx;
        beebium::InvokeRequest ireq;
        ireq.set_service("ScsiHostAdapterService");
        ireq.set_method(method);
        ireq.set_payload(req.SerializeAsString());
        beebium::InvokeResponse iresp;
        grpc::Status status = stub_->Invoke(&ctx, ireq, &iresp);
        if (status.ok() && resp != nullptr) {
            REQUIRE(resp->ParseFromString(iresp.payload()));
        }
        return status;
    }

    void install_test_device(uint8_t id, bool present = true) {
        auto dev = std::make_unique<beebium::ScsiTestDevice>();
        dev->set_present(present);
        dev->set_description("Test device at ID " + std::to_string(id));
        adapter_->target_registry().install(id, std::move(dev));
        adapter_->target_registry().wire_to_bus(adapter_->bus());
    }

private:
    beebium::ModelB machine_;
    beebium::EconetTransportRegistry transports_;
    beebium::ExtensionRegistry registry_;
    beebium::service::ExtensionRpcServiceImpl service_;
    beebium::AcornScsiHostAdapter* adapter_ = nullptr;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionRpc::Stub> stub_;
};

}  // namespace

TEST_CASE("ScsiHostAdapterService ListTargets returns empty initially",
          "[grpc][scsi]") {
    ScsiGrpcFixture fixture;

    beebium::ListScsiTargetsRequest request;
    beebium::ListScsiTargetsResponse response;

    auto status = fixture.invoke("ListTargets", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.targets_size() == 0);
}

TEST_CASE("ScsiHostAdapterService ListTargets returns installed targets",
          "[grpc][scsi]") {
    ScsiGrpcFixture fixture;
    fixture.install_test_device(0);
    fixture.install_test_device(3, false);  // present=false

    beebium::ListScsiTargetsRequest request;
    beebium::ListScsiTargetsResponse response;

    auto status = fixture.invoke("ListTargets", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.targets_size() == 2);

    REQUIRE(response.targets(0).id() == 0);
    REQUIRE(response.targets(0).present() == true);
    REQUIRE(response.targets(0).device_type() == "test-device");

    REQUIRE(response.targets(1).id() == 3);
    REQUIRE(response.targets(1).present() == false);
}

TEST_CASE("ScsiHostAdapterService GetBusStatus returns BUS_FREE initially",
          "[grpc][scsi]") {
    ScsiGrpcFixture fixture;

    beebium::GetScsiBusStatusRequest request;
    beebium::GetScsiBusStatusResponse response;

    auto status = fixture.invoke("GetBusStatus", request, &response);
    REQUIRE(status.ok());
    REQUIRE(response.phase() == "BUS_FREE");
    REQUIRE(response.selected_target() == 0xFF);
    REQUIRE(response.status_register() == 0x20);  // REQ always set (Acorn adapter quirk)
    REQUIRE(response.irq_pending() == false);
}
