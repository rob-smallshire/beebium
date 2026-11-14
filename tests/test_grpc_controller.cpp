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

// ControllerService over a Model B assembled from the command line
// (docs/discussion/controller-service-specification.md, tests 9 to 12).

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/server/ServerMain.hpp>
#include <beebium/service/ControllerService.hpp>
#include <beebium/service/PeripheralExtensionService.hpp>
#include <beebium/service/ProtocolFingerprint.hpp>
#include <beebium/service/Server.hpp>

#include "controller.grpc.pb.h"
#include "peripheral_extension.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace beebium;
using MachineType = ModelB;

namespace {

bool roms_available() {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dir / "bbc-basic_2.rom");
}

// A Model B assembled from `beebium start <options>`, served in process with
// ControllerService and PeripheralExtensionService.
class ServedMachine {
public:
    explicit ServedMachine(std::initializer_list<const char*> options) {
        server::RomPaths::set_rom_directory(BEEBIUM_ROM_DIR);
        std::vector<const char*> argv = {"beebium", "start"};
        argv.insert(argv.end(), options.begin(), options.end());
        std::vector<std::string> args(argv.begin(), argv.end());
        std::vector<char*> raw;
        for (auto& a : args) raw.push_back(a.data());
        raw.push_back(nullptr);
        auto parsed = server::parse_start_arguments<MachineType>(
            static_cast<int>(args.size()), raw.data(), 2, config_);
        REQUIRE_FALSE(parsed.has_value());

        auto outcome = server::assemble_machine<MachineType>(config_, /*with_audio=*/false);
        REQUIRE(outcome.assembled);
        assembled_ = std::move(outcome.assembled);

        controller_service_ =
            std::make_unique<service::ControllerServiceImpl>(assembled_->controller_registry);
        peripheral_service_ = std::make_unique<service::PeripheralExtensionServiceImpl>(
            assembled_->extension_registry);
        std::vector<grpc::Service*> services = {controller_service_.get(),
                                                peripheral_service_.get()};
        server_ = std::make_unique<service::Server<MachineType>>(assembled_->machine,
                                                                 "127.0.0.1", 0);
        server_->start({}, {}, false, {}, nullptr, services);

        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(server_->port()),
                                           grpc::InsecureChannelCredentials());
        controllers_ = ControllerService::NewStub(channel);
        peripherals_ = PeripheralExtensionService::NewStub(channel);
    }

    ~ServedMachine() {
        server_->stop();
        assembled_->extension_registry.shutdown();
    }

    ControllerService::Stub& controllers() { return *controllers_; }
    PeripheralExtensionService::Stub& peripherals() { return *peripherals_; }

    ListControllersResponse list() {
        grpc::ClientContext ctx;
        ListControllersResponse response;
        REQUIRE(controllers().ListControllers(&ctx, {}, &response).ok());
        return response;
    }

    GetControlsResponse get(const std::string& controller_id) {
        grpc::ClientContext ctx;
        GetControlsRequest request;
        request.set_controller_id(controller_id);
        GetControlsResponse response;
        REQUIRE(controllers().GetControls(&ctx, request, &response).ok());
        return response;
    }

    grpc::Status set(const SetControlsRequest& request) {
        grpc::ClientContext ctx;
        SetControlsResponse response;
        return controllers().SetControls(&ctx, request, &response);
    }

private:
    server::ServerConfig<MachineType> config_;
    std::unique_ptr<server::AssembledMachine<MachineType>> assembled_;
    std::unique_ptr<service::ControllerServiceImpl> controller_service_;
    std::unique_ptr<service::PeripheralExtensionServiceImpl> peripheral_service_;
    std::unique_ptr<service::Server<MachineType>> server_;
    std::unique_ptr<ControllerService::Stub> controllers_;
    std::unique_ptr<PeripheralExtensionService::Stub> peripherals_;
};

void add_axis(SetControlsRequest& request, const char* id, uint32_t axis) {
    auto* value = request.add_values();
    value->set_id(id);
    value->set_axis(axis);
}

void add_button(SetControlsRequest& request, const char* id, bool pressed) {
    auto* value = request.add_values();
    value->set_id(id);
    value->set_pressed(pressed);
}

// The values in `response` as "id=axis" or "id=pressed|released", in order.
std::vector<std::string> values_of(const GetControlsResponse& response) {
    std::vector<std::string> result;
    for (const auto& value : response.values()) {
        if (value.value_case() == ControlValue::kAxis) {
            result.push_back(value.id() + "=" + std::to_string(value.axis()));
        } else if (value.value_case() == ControlValue::kPressed) {
            result.push_back(value.id() + "=" + (value.pressed() ? "pressed" : "released"));
        } else {
            result.push_back(value.id() + "=?");
        }
    }
    return result;
}

const std::vector<std::string> DEFAULTS = {
    "stick1.x=32768", "stick1.y=32768", "fire1=released",
    "stick2.x=32768", "stick2.y=32768", "fire2=released",
};

std::string read_text(const std::filesystem::path& filepath) {
    std::ifstream file(filepath);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("ListControllers on a Model B with no joystick is empty", "[grpc][controller]") {
    if (!roms_available()) SKIP("ROMs not available");
    ServedMachine served({});
    CHECK(served.list().controllers_size() == 0);
}

TEST_CASE("ListControllers lists the joystick by its extension instance id",
          "[grpc][controller]") {
    if (!roms_available()) SKIP("ROMs not available");
    ServedMachine served({"--joystick"});

    grpc::ClientContext ctx;
    ListExtensionsResponse extensions;
    REQUIRE(served.peripherals().ListExtensions(&ctx, {}, &extensions).ok());
    std::string instance_id;
    for (const auto& ext : extensions.extensions()) {
        if (ext.name() == "joystick") instance_id = ext.id();
    }
    REQUIRE_FALSE(instance_id.empty());

    const auto response = served.list();
    REQUIRE(response.controllers_size() == 1);
    const auto& controller = response.controllers(0);
    CHECK(controller.controller_id() == instance_id);
    CHECK(controller.display_name() == "Joystick");

    struct Expected {
        const char* id;
        const char* display_name;
        const char* group;
        ControlType kind;
    };
    const Expected expected[] = {
        {"stick1.x", "Stick 1 X", "Stick 1", AXIS},
        {"stick1.y", "Stick 1 Y", "Stick 1", AXIS},
        {"fire1", "Fire 1", "Stick 1", BUTTON},
        {"stick2.x", "Stick 2 X", "Stick 2", AXIS},
        {"stick2.y", "Stick 2 Y", "Stick 2", AXIS},
        {"fire2", "Fire 2", "Stick 2", BUTTON},
    };
    REQUIRE(controller.controls_size() == static_cast<int>(std::size(expected)));
    for (int i = 0; i < controller.controls_size(); ++i) {
        INFO("control " << i);
        const auto& control = controller.controls(i);
        CHECK(control.id() == expected[i].id);
        CHECK(control.display_name() == expected[i].display_name);
        CHECK(control.group() == expected[i].group);
        CHECK(control.kind() == expected[i].kind);
    }
}

TEST_CASE("ListControllers lists the Voltmace Delta 14B/1 adapter's two handsets",
          "[grpc][controller][voltmace_delta_14b]") {
    if (!roms_available()) SKIP("ROMs not available");
    ServedMachine served({"--voltmace-delta-14b"});
    const auto response = served.list();
    REQUIRE(response.controllers_size() == 1);
    const auto& controller = response.controllers(0);
    CHECK(controller.controller_id() == "voltmace-delta-14b");
    CHECK(controller.display_name() == "Voltmace Delta 14B/1 adapter");
    REQUIRE(controller.controls_size() == 28);
    int rear = 0;
    int side = 0;
    for (int i = 0; i < controller.controls_size(); ++i) {
        const auto& control = controller.controls(i);
        INFO("control " << i << " " << control.id());
        const int handset = i < 14 ? 1 : 2;
        const int index = i % 14;
        const std::string prefix = "handset" + std::to_string(handset) + ".";
        if (index < 2) {
            CHECK(control.id() == prefix + (index == 0 ? "x" : "y"));
            CHECK(control.kind() == AXIS);
        } else {
            CHECK(control.id() == prefix + "key" + std::to_string(index - 2));
            CHECK(control.kind() == BUTTON);
        }
        if (control.group() == "Handset 1 (rear)") ++rear;
        if (control.group() == "Handset 2 (side)") ++side;
    }
    CHECK(rear == 14);
    CHECK(side == 14);
}

TEST_CASE("SetControls then GetControls reads back what was set", "[grpc][controller]") {
    if (!roms_available()) SKIP("ROMs not available");
    ServedMachine served({"--joystick"});
    CHECK(values_of(served.get("joystick")) == DEFAULTS);

    SetControlsRequest request;
    request.set_controller_id("joystick");
    add_axis(request, "stick1.x", 0xF000);
    add_axis(request, "stick2.y", 0);
    add_button(request, "fire1", true);
    REQUIRE(served.set(request).ok());

    CHECK(values_of(served.get("joystick")) ==
          std::vector<std::string>{"stick1.x=61440", "stick1.y=32768", "fire1=pressed",
                                   "stick2.x=32768", "stick2.y=0", "fire2=released"});
}

TEST_CASE("SetControls refuses a bad request and applies none of it", "[grpc][controller]") {
    if (!roms_available()) SKIP("ROMs not available");
    ServedMachine served({"--joystick"});

    auto refused = [&](SetControlsRequest request, grpc::StatusCode code,
                       const std::string& message) {
        const auto status = served.set(request);
        CHECK(status.error_code() == code);
        CHECK(status.error_message() == message);
        CHECK(values_of(served.get("joystick")) == DEFAULTS);
    };

    SECTION("an unknown controller") {
        SetControlsRequest request;
        request.set_controller_id("joystick2");
        add_axis(request, "stick1.x", 0);
        refused(request, grpc::StatusCode::NOT_FOUND, "There is no controller named joystick2.");

        grpc::ClientContext ctx;
        GetControlsRequest get;
        get.set_controller_id("joystick2");
        GetControlsResponse response;
        const auto status = served.controllers().GetControls(&ctx, get, &response);
        CHECK(status.error_code() == grpc::StatusCode::NOT_FOUND);
        CHECK(status.error_message() == "There is no controller named joystick2.");
    }
    SECTION("an unknown control after a good one") {
        SetControlsRequest request;
        request.set_controller_id("joystick");
        add_axis(request, "stick1.x", 0x1000);
        add_axis(request, "stick3.x", 0);
        refused(request, grpc::StatusCode::INVALID_ARGUMENT,
                "The controller joystick has no control named stick3.x.");
    }
    SECTION("pressed for an axis") {
        SetControlsRequest request;
        request.set_controller_id("joystick");
        add_button(request, "fire1", true);
        add_button(request, "stick1.x", true);
        refused(request, grpc::StatusCode::INVALID_ARGUMENT,
                "The control stick1.x of controller joystick is an axis, so it takes axis, "
                "not pressed.");
    }
    SECTION("an axis for a button") {
        SetControlsRequest request;
        request.set_controller_id("joystick");
        add_axis(request, "fire2", 1);
        refused(request, grpc::StatusCode::INVALID_ARGUMENT,
                "The control fire2 of controller joystick is a button, so it takes pressed, "
                "not axis.");
    }
    SECTION("an axis above 65535") {
        SetControlsRequest request;
        request.set_controller_id("joystick");
        add_axis(request, "stick1.y", 0x2000);
        add_axis(request, "stick2.x", 65536);
        refused(request, grpc::StatusCode::INVALID_ARGUMENT,
                "The axis value for stick2.x of controller joystick is 65536, above the "
                "largest, 65535.");
    }
    SECTION("a value with neither an axis nor pressed") {
        SetControlsRequest request;
        request.set_controller_id("joystick");
        add_axis(request, "stick1.x", 0x3000);
        request.add_values()->set_id("stick1.y");
        refused(request, grpc::StatusCode::INVALID_ARGUMENT,
                "The value for stick1.y of controller joystick is neither an axis nor pressed.");
    }
}

TEST_CASE("The four protocol fingerprint constants agree", "[grpc][controller][fingerprint]") {
    const std::filesystem::path root(BEEBIUM_SOURCE_DIRPATH);
    const std::string fingerprint(service::PROTOCOL_FINGERPRINT);
    REQUIRE(fingerprint.size() == 64);
    for (const char* relative :
         {"clients/beebium-python-client/src/beebium/client/_proto/protocol_fingerprint.py",
          "clients/beebium-typescript-client/src/protocol_fingerprint.ts",
          "clients/macos/Beebium/Beebium/ProtocolFingerprint.swift"}) {
        INFO(relative);
        const auto text = read_text(root / relative);
        REQUIRE_FALSE(text.empty());
        CHECK(text.find(fingerprint) != std::string::npos);
    }
}
