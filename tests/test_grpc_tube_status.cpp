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

// TubeService.GetStatus reports the coprocessor plugin attached at launch
// (issue #205).

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/extension/CoprocessorExtension.hpp"
#include "beebium/service/Server.hpp"

#include "tube.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

namespace {

// A coprocessor plugin reduced to the identity the status reports.
class IdentityOnlyCoprocessor : public beebium::CoprocessorExtension {
public:
    IdentityOnlyCoprocessor() { set_config_value("id", "copro-1"); }
    std::span<const std::string_view> attaches_to() const override { return {}; }
    std::span<const std::string_view> provides() const override { return {}; }
    void init(beebium::ExtensionContext&) override {}
    void shutdown() override {}
    beebium::Coprocessor* coprocessor() override { return nullptr; }
    beebium::TubeHostBackend* tube_backend() override { return nullptr; }
    std::string cpu_name() const override { return "65C02"; }
    uint64_t clock_hz() const override { return 3'000'000; }
};

template <typename MachineType>
beebium::GetTubeStatusResponse tube_status(const beebium::CoprocessorExtension* coprocessor) {
    MachineType machine;
    machine.reset();
    beebium::service::Server<MachineType> server(machine, "127.0.0.1", 0);
    server.set_launch_coprocessor(coprocessor);
    server.start({}, {});
    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(server.port()),
                                       grpc::InsecureChannelCredentials());
    auto stub = beebium::TubeService::NewStub(channel);
    grpc::ClientContext context;
    beebium::GetTubeStatusResponse response;
    REQUIRE(stub->GetStatus(&context, beebium::GetTubeStatusRequest{}, &response).ok());
    server.stop();
    return response;
}

} // namespace

TEST_CASE("Tube status reports the attached coprocessor plugin (#205)", "[grpc][tube]") {
    IdentityOnlyCoprocessor coprocessor;
    const auto status = tube_status<beebium::ModelB>(&coprocessor);
    CHECK(status.has_tube_socket());
    CHECK(status.coprocessor_connected());
    CHECK(status.coprocessor_cpu() == "65C02");
    CHECK(status.coprocessor_clock_hz() == 3'000'000);
    CHECK(status.coprocessor_extension_id() == "copro-1");
}

TEST_CASE("Tube status reports no coprocessor when none is attached (#205)", "[grpc][tube]") {
    const auto status = tube_status<beebium::ModelB>(nullptr);
    CHECK(status.has_tube_socket());
    CHECK_FALSE(status.coprocessor_connected());
    CHECK(status.coprocessor_cpu().empty());
    CHECK(status.coprocessor_clock_hz() == 0);
    CHECK(status.coprocessor_extension_id().empty());
}
