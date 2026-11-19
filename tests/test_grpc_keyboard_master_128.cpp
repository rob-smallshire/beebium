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

// test_grpc_keyboard_master_128.cpp
// KeyboardService on a machine's own keyboard: the Master 128's keypad keys
// are pressed and listed in its catalogue, and not the Model B's; the
// Master's keyboard has no start-up links to get or set.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/service/Server.hpp"

#include "keyboard.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

template <typename MachineType>
class Fixture {
public:
    Fixture() {
        machine_->reset();
        server_ = std::make_unique<beebium::service::Server<MachineType>>(*machine_, "127.0.0.1", 0);
        server_->start({}, {});
        channel_ = grpc::CreateChannel("127.0.0.1:" + std::to_string(server_->port()),
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::KeyboardService::NewStub(channel_);
    }
    ~Fixture() { server_->stop(); }

    MachineType& machine() { return *machine_; }
    beebium::KeyboardService::Stub& stub() { return *stub_; }

    bool key_down(uint32_t ik) {
        grpc::ClientContext context;
        beebium::KeyRequest request;
        request.set_ik_number(ik);
        beebium::KeyResponse response;
        REQUIRE(stub_->KeyDown(&context, request, &response).ok());
        return response.accepted();
    }

    std::vector<std::string> catalogue_names() {
        grpc::ClientContext context;
        beebium::GetAllKeyMappingsRequest request;
        beebium::AllKeyMappingsResponse response;
        REQUIRE(stub_->GetAllKeyMappings(&context, request, &response).ok());
        std::vector<std::string> names;
        for (const auto& m : response.mappings()) names.push_back(m.name());
        return names;
    }

private:
    std::unique_ptr<MachineType> machine_ = std::make_unique<MachineType>();
    std::unique_ptr<beebium::service::Server<MachineType>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::KeyboardService::Stub> stub_;
};

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

}  // namespace

TEST_CASE("KeyboardService presses the Master 128's keypad", "[grpc][keyboard][master128]") {
    Fixture<beebium::Master128> fixture;
    CHECK(fixture.key_down(0x6A));  // Keypad0: row 6, column 10
    CHECK(fixture.machine().state().memory.system_via_peripheral.is_key_pressed(6, 10));
    CHECK(fixture.key_down(0x7C));  // Keypad2: row 7, column 12
    CHECK_FALSE(fixture.key_down(0x1D));  // column 13: no such key
}

TEST_CASE("KeyboardService refuses the keypad on a Model B", "[grpc][keyboard][master128]") {
    Fixture<beebium::ModelB> fixture;
    CHECK_FALSE(fixture.key_down(0x6A));
}

TEST_CASE("KeyboardService lists the keypad in the Master 128's catalogue only",
          "[grpc][keyboard][master128]") {
    Fixture<beebium::Master128> master;
    const auto master_names = master.catalogue_names();
    for (const char* name : {"Keypad0", "Keypad9", "KeypadPlus", "KeypadReturn", "KeypadDelete"}) {
        INFO(name);
        CHECK(contains(master_names, name));
    }
    CHECK(contains(master_names, "Return"));

    Fixture<beebium::ModelB> model_b;
    const auto b_names = model_b.catalogue_names();
    CHECK_FALSE(contains(b_names, "Keypad0"));
    CHECK(contains(b_names, "Return"));
    CHECK(master_names.size() == b_names.size() + 19);
}

TEST_CASE("KeyboardService says the Master 128 has no start-up links",
          "[grpc][keyboard][master128][links]") {
    Fixture<beebium::Master128> fixture;
    {
        grpc::ClientContext context;
        beebium::SetLinksRequest request;
        request.set_value(0x00);
        beebium::SetLinksResponse response;
        const auto status = fixture.stub().SetLinks(&context, request, &response);
        CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
        CHECK(status.error_message().find("*CONFIGURE") != std::string::npos);
    }
    {
        grpc::ClientContext context;
        beebium::GetLinksRequest request;
        beebium::LinksState response;
        CHECK(fixture.stub().GetLinks(&context, request, &response).error_code() ==
              grpc::StatusCode::FAILED_PRECONDITION);
    }
    {
        grpc::ClientContext context;
        beebium::SetStartupScreenModeRequest request;
        request.set_mode(0);
        beebium::SetStartupScreenModeResponse response;
        CHECK(fixture.stub().SetStartupScreenMode(&context, request, &response).error_code() ==
              grpc::StatusCode::FAILED_PRECONDITION);
    }
    {
        grpc::ClientContext context;
        beebium::SetStartupAutoBootRequest request;
        request.set_enabled(true);
        beebium::SetStartupAutoBootResponse response;
        CHECK(fixture.stub().SetStartupAutoBoot(&context, request, &response).error_code() ==
              grpc::StatusCode::FAILED_PRECONDITION);
    }
}
