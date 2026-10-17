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

// A machine's plain name and its description, a template (#153), through
// SystemService: set, preview, list, and a rendering that follows a
// placeholder's value.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/extension/CoprocessorExtension.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/NamePlaceholderProvider.hpp"
#include "beebium/service/Server.hpp"

#include "system.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

// A test extension contributing one placeholder, to prove the hook end to end.
class GizmoExtension : public beebium::Extension, public beebium::NamePlaceholderProvider {
public:
    GizmoExtension() {
        beebium::ExtensionManifest manifest;
        manifest.name = "gizmo";
        set_manifest(std::move(manifest));
        set_config_value("id", "gizmo-1");
    }
    const beebium::NamePlaceholderProvider* name_placeholders() const override { return this; }
    std::vector<std::string> placeholder_domains() const override { return {"gizmo"}; }
    std::vector<beebium::NamePlaceholderInfo> placeholders() const override {
        return {{"gizmo-colour", "Gizmo colour", "The gizmo's colour.", "Gizmo"}};
    }
    beebium::NamePlaceholderValue placeholder_value(std::string_view) const override {
        return {"green", true};
    }
};

// A coprocessor plugin reduced to the identity it reports: the CPU's name and
// its clock. The name placeholders use nothing else.
class IdentityOnlyCoprocessor : public beebium::CoprocessorExtension {
public:
    IdentityOnlyCoprocessor(std::string cpu_name, uint64_t clock_hz)
        : cpu_name_(std::move(cpu_name)), clock_hz_(clock_hz) {}
    std::span<const std::string_view> attaches_to() const override { return {}; }
    std::span<const std::string_view> provides() const override { return {}; }
    void init(beebium::ExtensionContext&) override {}
    void shutdown() override {}
    beebium::Coprocessor* coprocessor() override { return nullptr; }
    beebium::TubeHostBackend* tube_backend() override { return nullptr; }
    std::string cpu_name() const override { return cpu_name_; }
    uint64_t clock_hz() const override { return clock_hz_; }

private:
    std::string cpu_name_;
    uint64_t clock_hz_;
};

struct Fitted {
    bool fdc = false;
    const beebium::CoprocessorExtension* coprocessor = nullptr;
};

class NameFixture {
public:
    explicit NameFixture(std::string description_template, bool fit_econet = true,
                         std::string preset_name = "Station 80 (AUN, Model B)", Fitted fitted = {},
                         std::string name = "My Beeb") {
        machine_.reset();
        if (fit_econet) {
            machine_.state().memory.econet_socket.enable(
                80, std::make_unique<beebium::TestBackend>());
        }
        if (fitted.fdc) {
            machine_.state().memory.install_acorn_1770();
        }
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->name_placeholders().add_extension(gizmo_);
        server_->set_launch_preset_name(std::move(preset_name));
        server_->set_launch_coprocessor(fitted.coprocessor);
        beebium::service::MachineIdentity identity{
            "00000000-0000-4000-8000-000000000153", std::move(name), "ModelB", "BBC Model B",
            std::move(description_template), {}};
        server_->start({}, identity);
        channel_ = grpc::CreateChannel("127.0.0.1:" + std::to_string(server_->port()),
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::SystemService::NewStub(channel_);
    }

    ~NameFixture() { server_->stop(); }

    beebium::ModelB& machine() { return machine_; }
    beebium::service::Server<beebium::ModelB>& server() { return *server_; }
    beebium::SystemService::Stub& system() { return *stub_; }

    beebium::MachineIdentity identity() {
        grpc::ClientContext context;
        beebium::SystemInfo info;
        REQUIRE(stub_->GetSystemInfo(&context, {}, &info).ok());
        return info.identity();
    }

private:
    GizmoExtension gizmo_;
    beebium::ModelB machine_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::SystemService::Stub> stub_;
};

std::vector<std::string> strings(const google::protobuf::RepeatedPtrField<std::string>& field) {
    return {field.begin(), field.end()};
}

beebium::PreviewMachineDescriptionResponse preview(NameFixture& fixture,
                                                   const std::string& description_template) {
    grpc::ClientContext context;
    beebium::PreviewMachineDescriptionRequest request;
    request.set_description_template(description_template);
    beebium::PreviewMachineDescriptionResponse response;
    REQUIRE(fixture.system().PreviewMachineDescription(&context, request, &response).ok());
    return response;
}

}  // namespace

TEST_CASE("The identity carries a plain name and a rendered description",
          "[grpc][system][name-template]") {
    NameFixture fixture("Station {econet-station} (AUN)");
    auto identity = fixture.identity();
    CHECK(identity.name() == "My Beeb");
    CHECK(identity.description() == "Station 80 (AUN)");
    CHECK(identity.description_template() == "Station {econet-station} (AUN)");
}

TEST_CASE("A name is plain text: braces in it are not placeholders",
          "[grpc][system][name-template]") {
    NameFixture fixture("", true, "Station 80 (AUN, Model B)", {}, "Model {econet-station}");
    auto identity = fixture.identity();
    CHECK(identity.name() == "Model {econet-station}");
    CHECK(identity.description().empty());  // no template, no description
    CHECK(identity.description_template().empty());
}

TEST_CASE("SetMachineName sets a plain name and refuses an empty one",
          "[grpc][system][name-template]") {
    NameFixture fixture("Station {econet-station}");
    {
        grpc::ClientContext context;
        beebium::SetMachineNameRequest request;
        request.set_name("Girton {econet-station}");
        beebium::SetMachineNameResponse response;
        REQUIRE(fixture.system().SetMachineName(&context, request, &response).ok());
        CHECK(response.identity().name() == "Girton {econet-station}");
        CHECK(response.identity().description() == "Station 80");
    }
    grpc::ClientContext context;
    beebium::SetMachineNameRequest request;
    beebium::SetMachineNameResponse response;
    CHECK(fixture.system().SetMachineName(&context, request, &response).error_code() ==
          grpc::StatusCode::INVALID_ARGUMENT);
    CHECK(fixture.identity().name() == "Girton {econet-station}");
}

TEST_CASE("SetMachineDescription takes a template and reports what it could not substitute",
          "[grpc][system][name-template]") {
    NameFixture fixture("Plain");
    grpc::ClientContext context;
    beebium::SetMachineDescriptionRequest request;
    request.set_description_template(
        "{machine-model} {econet-station} {econet-sttion} {gizmo-colour} {oops");
    beebium::SetMachineDescriptionResponse response;
    REQUIRE(fixture.system().SetMachineDescription(&context, request, &response).ok());
    CHECK(response.identity().description() == "BBC Model B 80 {econet-sttion} green {oops");
    CHECK(response.identity().description_template() ==
          "{machine-model} {econet-station} {econet-sttion} {gizmo-colour} {oops");
    CHECK(response.identity().name() == "My Beeb");
    CHECK(strings(response.report().unknown_keys()) == std::vector<std::string>{"econet-sttion"});
    CHECK(strings(response.report().malformed()) == std::vector<std::string>{"{oops"});
    CHECK(fixture.identity().description() == response.identity().description());

    grpc::ClientContext clear_context;
    beebium::SetMachineDescriptionRequest clear;
    REQUIRE(fixture.system().SetMachineDescription(&clear_context, clear, &response).ok());
    CHECK(fixture.identity().description().empty());
}

TEST_CASE("PreviewMachineDescription renders without changing anything",
          "[grpc][system][name-template]") {
    NameFixture fixture("Original", /*fit_econet=*/false);
    auto response = preview(fixture, "{machine-preset} on net [{econet-net}] {{literal}}");
    CHECK(response.description() == "Station 80 (AUN, Model B) on net [] {literal}");
    CHECK(strings(response.report().inapplicable_keys()) == std::vector<std::string>{"econet-net"});
    CHECK(response.report().unknown_keys().empty());
    CHECK(fixture.identity().description() == "Original");
    CHECK(fixture.identity().description_template() == "Original");
}

TEST_CASE("ListDescriptionPlaceholders lists every provider's placeholders with values",
          "[grpc][system][name-template]") {
    NameFixture fixture("Plain");
    grpc::ClientContext context;
    beebium::ListDescriptionPlaceholdersResponse response;
    REQUIRE(fixture.system().ListDescriptionPlaceholders(&context, {}, &response).ok());

    std::vector<std::string> keys;
    for (const auto& p : response.placeholders()) keys.push_back(p.key());
    CHECK(keys == std::vector<std::string>{"machine-model", "machine-preset", "econet-station",
                                           "econet-net", "econet-transport",
                                           "fdc-controller", "coprocessor-cpu",
                                           "coprocessor-clock", "gizmo-colour"});
    for (const auto& p : response.placeholders()) {
        INFO(p.key());
        CHECK_FALSE(p.label().empty());
        CHECK_FALSE(p.description().empty());
        CHECK_FALSE(p.group().empty());
        CHECK(p.insertion() == "{" + p.key() + "}");
    }
    auto find = [&](std::string_view key) {
        return *std::find_if(response.placeholders().begin(), response.placeholders().end(),
                             [&](const auto& p) { return p.key() == key; });
    };
    CHECK(find("machine-model").value() == "BBC Model B");
    CHECK(find("machine-preset").value() == "Station 80 (AUN, Model B)");
    CHECK(find("econet-station").value() == "80");
    CHECK(find("econet-station").applicable());
    CHECK(find("econet-net").value() == "0");
    CHECK(find("econet-transport").value().empty());  // fitted, no transport
    CHECK(find("econet-transport").applicable());
    CHECK(find("gizmo-colour").group() == "Gizmo");
}

TEST_CASE("Econet placeholders are not applicable with no Econet fitted",
          "[grpc][system][name-template]") {
    NameFixture fixture("Plain", /*fit_econet=*/false, /*preset_name=*/"");
    grpc::ClientContext context;
    beebium::ListDescriptionPlaceholdersResponse response;
    REQUIRE(fixture.system().ListDescriptionPlaceholders(&context, {}, &response).ok());
    for (const auto& p : response.placeholders()) {
        INFO(p.key());
        if (p.key().rfind("econet-", 0) == 0 || p.key() == "machine-preset" ||
            p.key().rfind("fdc-", 0) == 0 || p.key().rfind("coprocessor-", 0) == 0) {
            CHECK_FALSE(p.applicable());
            CHECK(p.value().empty());
        }
    }
}

TEST_CASE("The rendered description follows the station in force, at Break",
          "[grpc][system][name-template]") {
    NameFixture fixture("Station {econet-station}");
    auto& socket = fixture.machine().state().memory.econet_socket;
    socket.read_station_id(0);  // the filing system takes its number at boot
    REQUIRE(fixture.identity().description() == "Station 80");

    grpc::ClientContext watch_context;
    auto reader = fixture.system().WatchServerStatus(&watch_context, {});
    beebium::ServerStatusEvent event;
    REQUIRE(reader->Read(&event));
    REQUIRE(event.status() == beebium::SERVER_STATUS_READY);

    // Renumbered in the sidebar: pending until the guest re-reads the links.
    socket.set_station_id(81);
    std::this_thread::sleep_for(beebium::service::kDescriptionRefreshInterval * 2 + 200ms);
    CHECK(fixture.identity().description() == "Station 80");

    // Break: the guest re-reads its number, and the description follows within
    // a refresh interval, as an identity change on the status stream. The name
    // does not change.
    socket.reset();
    socket.read_station_id(0);
    // A backstop, not a measurement: the refresh runs every second.
    const auto backstop = std::chrono::steady_clock::now() + 30s;
    bool changed = false;
    while (!changed && std::chrono::steady_clock::now() < backstop && reader->Read(&event)) {
        if (event.status() == beebium::SERVER_STATUS_IDENTITY_CHANGED) {
            CHECK(event.identity().description() == "Station 81");
            CHECK(event.identity().name() == "My Beeb");
            changed = true;
        }
    }
    watch_context.TryCancel();
    CHECK(changed);
    CHECK(fixture.identity().description() == "Station 81");
}

TEST_CASE("fdc-controller names the fitted floppy controller's chip",
          "[grpc][system][name-template][fdc]") {
    NameFixture fixture("Plain", /*fit_econet=*/false, "", Fitted{.fdc = true});
    auto response = preview(fixture, "Model B ({fdc-controller})");
    CHECK(response.description() == "Model B (WD1770)");
    CHECK(response.report().inapplicable_keys().empty());
}

TEST_CASE("fdc-controller is not applicable with no floppy controller",
          "[grpc][system][name-template][fdc]") {
    NameFixture fixture("Plain", /*fit_econet=*/false, "");
    auto response = preview(fixture, "Model B [{fdc-controller}]");
    CHECK(response.description() == "Model B []");
    CHECK(strings(response.report().inapplicable_keys()) ==
          std::vector<std::string>{"fdc-controller"});
}

TEST_CASE("fdc-controller follows a controller removed at runtime",
          "[grpc][system][name-template][fdc]") {
    NameFixture fixture("Plain", /*fit_econet=*/false, "", Fitted{.fdc = true});
    CHECK(preview(fixture, "{fdc-controller}").description() == "WD1770");
    fixture.machine().state().memory.remove_disc_controller();
    CHECK(preview(fixture, "[{fdc-controller}]").description() == "[]");
}

TEST_CASE("coprocessor placeholders come from the coprocessor's own report",
          "[grpc][system][name-template][coprocessor]") {
    IdentityOnlyCoprocessor z80("Z80", 6'000'000);
    NameFixture fixture("Plain", /*fit_econet=*/false, "",
                        Fitted{.coprocessor = &z80});
    auto response = preview(fixture, "{coprocessor-cpu} Copro ({coprocessor-clock})");
    CHECK(response.description() == "Z80 Copro (6 MHz)");
    CHECK(response.report().inapplicable_keys().empty());

    IdentityOnlyCoprocessor fractional("Z80", 3'500'000);
    NameFixture fixture2("Plain", /*fit_econet=*/false, "",
                         Fitted{.coprocessor = &fractional});
    CHECK(preview(fixture2, "{coprocessor-clock}").description() == "3.5 MHz");
}

TEST_CASE("coprocessor placeholders are not applicable with no coprocessor",
          "[grpc][system][name-template][coprocessor]") {
    NameFixture fixture("Plain", /*fit_econet=*/false, "");
    auto response = preview(fixture, "B [{coprocessor-cpu}] [{coprocessor-clock}]");
    CHECK(response.description() == "B [] []");
    CHECK(strings(response.report().inapplicable_keys()) ==
          std::vector<std::string>{"coprocessor-cpu", "coprocessor-clock"});
}

TEST_CASE("The B+ names its built-in WD1770", "[grpc][system][name-template][fdc]") {
    beebium::ModelBPlus machine;
    machine.reset();
    beebium::service::Server<beebium::ModelBPlus> server(machine, "127.0.0.1", 0);
    beebium::service::MachineIdentity identity{"00000000-0000-4000-8000-000000000173", "Plain",
                                               "ModelBPlus", "BBC Model B+", "Plain", {}};
    server.start({}, identity);
    auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(server.port()),
                                       grpc::InsecureChannelCredentials());
    auto stub = beebium::SystemService::NewStub(channel);
    grpc::ClientContext context;
    beebium::PreviewMachineDescriptionRequest request;
    request.set_description_template("B+ ({fdc-controller})");
    beebium::PreviewMachineDescriptionResponse response;
    REQUIRE(stub->PreviewMachineDescription(&context, request, &response).ok());
    CHECK(response.description() == "B+ (WD1770)");
    server.stop();
}
