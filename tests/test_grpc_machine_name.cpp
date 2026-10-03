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

// Machine names as templates through SystemService (#153): set, preview,
// list, and a rendering that follows a placeholder's value.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/NamePlaceholderProvider.hpp"
#include "beebium/service/ReannounceLimiter.hpp"
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

class NameFixture {
public:
    explicit NameFixture(std::string name_template, bool fit_econet = true,
                         std::string preset_name = "Station 80 (AUN, Model B)") {
        machine_.reset();
        if (fit_econet) {
            machine_.state().memory.econet_socket.enable(
                80, std::make_unique<beebium::TestBackend>());
        }
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->name_placeholders().add_extension(gizmo_);
        server_->set_launch_preset_name(std::move(preset_name));
        beebium::service::MachineIdentity identity{
            "00000000-0000-4000-8000-000000000153", name_template,
            "ModelB", "BBC Model B", name_template};
        server_->start({}, identity);
        channel_ = grpc::CreateChannel("127.0.0.1:" + std::to_string(server_->port()),
                                       grpc::InsecureChannelCredentials());
        stub_ = beebium::SystemService::NewStub(channel_);
    }

    ~NameFixture() { server_->stop(); }

    beebium::ModelB& machine() { return machine_; }
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

}  // namespace

TEST_CASE("A launch template is rendered; the identity carries both",
          "[grpc][system][name-template]") {
    NameFixture fixture("Station {econet-station} (AUN, Model B)");
    auto identity = fixture.identity();
    CHECK(identity.name() == "Station 80 (AUN, Model B)");
    CHECK(identity.name_template() == "Station {econet-station} (AUN, Model B)");
}

TEST_CASE("A plain name is a template with no placeholders", "[grpc][system][name-template]") {
    NameFixture fixture("My BBC Micro #2");
    auto identity = fixture.identity();
    CHECK(identity.name() == "My BBC Micro #2");
    CHECK(identity.name_template() == "My BBC Micro #2");
}

TEST_CASE("SetMachineName takes a template and reports what it could not substitute",
          "[grpc][system][name-template]") {
    NameFixture fixture("Plain");
    grpc::ClientContext context;
    beebium::SetMachineNameRequest request;
    request.set_name_template("{machine-model} {econet-station} {econet-sttion} {gizmo-colour} {oops");
    beebium::SetMachineNameResponse response;
    REQUIRE(fixture.system().SetMachineName(&context, request, &response).ok());
    CHECK(response.identity().name() ==
          "BBC Model B 80 {econet-sttion} green {oops");
    CHECK(response.identity().name_template() ==
          "{machine-model} {econet-station} {econet-sttion} {gizmo-colour} {oops");
    CHECK(strings(response.report().unknown_keys()) == std::vector<std::string>{"econet-sttion"});
    CHECK(strings(response.report().malformed()) == std::vector<std::string>{"{oops"});
    CHECK(fixture.identity().name() == response.identity().name());
}

TEST_CASE("PreviewMachineName renders without changing anything",
          "[grpc][system][name-template]") {
    NameFixture fixture("Original", /*fit_econet=*/false);
    grpc::ClientContext context;
    beebium::PreviewMachineNameRequest request;
    request.set_name_template("{machine-preset} on net [{econet-net}] {{literal}}");
    beebium::PreviewMachineNameResponse response;
    REQUIRE(fixture.system().PreviewMachineName(&context, request, &response).ok());
    CHECK(response.name() == "Station 80 (AUN, Model B) on net [] {literal}");
    CHECK(strings(response.report().inapplicable_keys()) == std::vector<std::string>{"econet-net"});
    CHECK(response.report().unknown_keys().empty());
    CHECK(fixture.identity().name() == "Original");
    CHECK(fixture.identity().name_template() == "Original");
}

TEST_CASE("A blank rendering falls back to the model's name", "[grpc][system][name-template]") {
    NameFixture fixture("{econet-station}", /*fit_econet=*/false);
    CHECK(fixture.identity().name() == "BBC Model B");
    CHECK(fixture.identity().name_template() == "{econet-station}");
}

TEST_CASE("ListNamePlaceholders lists every provider's placeholders with values",
          "[grpc][system][name-template]") {
    NameFixture fixture("Plain");
    grpc::ClientContext context;
    beebium::ListNamePlaceholdersResponse response;
    REQUIRE(fixture.system().ListNamePlaceholders(&context, {}, &response).ok());

    std::vector<std::string> keys;
    for (const auto& p : response.placeholders()) keys.push_back(p.key());
    CHECK(keys == std::vector<std::string>{"machine-model", "machine-preset", "econet-station",
                                           "econet-net", "econet-transport", "gizmo-colour"});
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
    beebium::ListNamePlaceholdersResponse response;
    REQUIRE(fixture.system().ListNamePlaceholders(&context, {}, &response).ok());
    for (const auto& p : response.placeholders()) {
        INFO(p.key());
        if (p.key().rfind("econet-", 0) == 0 || p.key() == "machine-preset") {
            CHECK_FALSE(p.applicable());
            CHECK(p.value().empty());
        }
    }
}

TEST_CASE("The rendered name follows the station in force, at Break",
          "[grpc][system][name-template]") {
    NameFixture fixture("Station {econet-station}");
    auto& socket = fixture.machine().state().memory.econet_socket;
    socket.read_station_id(0);  // the filing system takes its number at boot
    REQUIRE(fixture.identity().name() == "Station 80");

    grpc::ClientContext watch_context;
    auto reader = fixture.system().WatchServerStatus(&watch_context, {});
    beebium::ServerStatusEvent event;
    REQUIRE(reader->Read(&event));
    REQUIRE(event.status() == beebium::SERVER_STATUS_READY);

    // Renumbered in the sidebar: pending until the guest re-reads the links.
    socket.set_station_id(81);
    std::this_thread::sleep_for(beebium::service::kNameRefreshInterval * 2 + 200ms);
    CHECK(fixture.identity().name() == "Station 80");

    // Break: the guest re-reads its number, and the name follows within a
    // refresh interval, as an identity change on the status stream.
    socket.reset();
    socket.read_station_id(0);
    // A backstop, not a measurement: the refresh runs every second.
    const auto backstop = std::chrono::steady_clock::now() + 30s;
    bool renamed = false;
    while (!renamed && std::chrono::steady_clock::now() < backstop && reader->Read(&event)) {
        if (event.status() == beebium::SERVER_STATUS_IDENTITY_CHANGED) {
            CHECK(event.identity().name() == "Station 81");
            CHECK(event.identity().name_template() == "Station {econet-station}");
            renamed = true;
        }
    }
    watch_context.TryCancel();
    CHECK(renamed);
    CHECK(fixture.identity().name() == "Station 81");
}

TEST_CASE("Re-announcements of a changing name are rate limited",
          "[system][name-template][reannounce]") {
    using beebium::service::ReannounceLimiter;
    const auto t0 = ReannounceLimiter::Clock::time_point{};
    ReannounceLimiter limiter(5s);

    CHECK_FALSE(limiter.take(t0));  // nothing requested
    limiter.request();
    CHECK(limiter.take(t0));  // first: at once
    limiter.request();
    CHECK_FALSE(limiter.take(t0 + 1s));  // within the interval: held
    limiter.request();                     // coalesces
    CHECK_FALSE(limiter.take(t0 + 4s));
    CHECK(limiter.take(t0 + 5s));          // one re-announcement when it ends
    CHECK_FALSE(limiter.take(t0 + 20s));   // and only one

    // A direct re-announcement (a rename) satisfies a pending request and
    // restarts the interval.
    limiter.request();
    limiter.note_published(t0 + 21s);
    CHECK_FALSE(limiter.pending());
    limiter.request();
    CHECK_FALSE(limiter.take(t0 + 25s));
    CHECK(limiter.take(t0 + 26s));
}
