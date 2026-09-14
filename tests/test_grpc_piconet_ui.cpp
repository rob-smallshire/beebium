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

// End-to-end gRPC test for the Piconet ModalEditor / EditableChoice
// reopen path. Drives a real PiconetEconetTransportExtension wired to
// a PTY-bridged FakePiconetDevice through the real ExtensionUiService
// gRPC server -- the round-trip a real client (macOS, Python, TS) would
// perform: SubscribeView -> Dispatch(EditorCommit) -> emulation tick
// drains the pending reopen -> next pushed View shows the new path.
//
// Closes the coverage gap between the framework dispatcher unit tests
// (test_grpc_extension_ui_service) and the PiconetUi handle_event unit
// tests (test_piconet_ui). Either side could regress the wire-level
// glue (mark_dirty after reopen, view-revision bump on transition,
// validate_editor_commit accepting a string EditorCommit field) without
// either of those suites noticing.

#include <catch2/catch_test_macros.hpp>

#include "PiconetEconetTransportExtension.hpp"
#include "beebium/Machines.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/ExtensionManifest.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/service/ExtensionUiService.hpp"
#include "beebium/service/Server.hpp"

#include "extension_ui.grpc.pb.h"

#include "piconet/FakePiconetDeviceOnSerial.hpp"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Spin up a real ExtensionUiService server with one Piconet extension
// loaded against a PTY-bridged FakePiconetDevice. The fixture exposes:
//   - the gRPC stub (for SubscribeView / Dispatch)
//   - the backend pointer (so tests can drive an emulation tick by
//     calling receive_frame, which is what consumes the pending reopen
//     slot in production at the top of every tick)
//   - the slave_path (so tests can assert the initial / new device path)
class PiconetExtensionUiFixture {
public:
    PiconetExtensionUiFixture() {
        primary_fake_ =
            std::make_unique<beebium::piconet::test::FakePiconetDeviceOnSerial>();
        REQUIRE(primary_fake_->is_open());

        auto ext = std::make_unique<beebium::PiconetEconetTransportExtension>();
        // The plugin loader normally stamps the manifest with name and
        // kind from the .toml; in a direct-construction test we set
        // them by hand so ExtensionUiService::find_extension("piconet")
        // resolves.
        beebium::ExtensionManifest manifest;
        manifest.name = "piconet";
        manifest.cli_name = "piconet";
        manifest.extension_kind = "econet-transport";
        ext->set_manifest(std::move(manifest));
        ext->set_config({{"device_path", primary_fake_->slave_path()}});
        auto backend_owner = ext->create_backend(/*station=*/1);
        REQUIRE(backend_owner != nullptr);
        backend_ = static_cast<beebium::PiconetBackend*>(backend_owner.get());
        REQUIRE(backend_->is_serial_open());
        // Move the backend into the EconetSocket so the machine's tick
        // path could in principle drive it; the test drives ticks
        // explicitly via backend_->receive_frame() rather than running
        // the machine.
        machine_.state().memory.econet_socket.enable(
            /*station=*/1, std::move(backend_owner), /*aun_mode=*/true);

        registry_.add(std::move(ext));

        service_ = std::make_unique<beebium::service::ExtensionUiServiceImpl>(
            registry_, peripheral_registry_);
        services_.push_back(service_.get());

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start(beebium::service::Provenance{},
                       beebium::service::MachineIdentity{},
                       /*enable_advertisement=*/false,
                       /*policy_config=*/{},
                       /*shutdown_callback=*/nullptr,
                       std::span<grpc::Service*>(services_));

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::ExtensionUiService::NewStub(channel_);
    }

    ~PiconetExtensionUiFixture() {
        server_->stop();
    }

    beebium::ExtensionUiService::Stub& stub() { return *stub_; }
    beebium::PiconetBackend& backend() { return *backend_; }
    const std::string& slave_path() const { return primary_fake_->slave_path(); }

    // Block until the backend's mode field reaches `mode` (up to 500ms).
    bool wait_for_mode(beebium::piconet::Mode mode) {
        auto deadline = std::chrono::steady_clock::now() + 500ms;
        while (std::chrono::steady_clock::now() < deadline) {
            if (backend_->mode() == mode) return true;
            std::this_thread::sleep_for(5ms);
        }
        return backend_->mode() == mode;
    }

    // Block until the backend's serial-open state matches `expected`
    // (up to 1s). Used after request_reopen + receive_frame to give
    // the close+join+open sequence time to stabilise.
    bool wait_for_serial_open(bool expected) {
        auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (backend_->is_serial_open() == expected) return true;
            std::this_thread::sleep_for(5ms);
        }
        return backend_->is_serial_open() == expected;
    }

private:
    beebium::ModelB machine_;
    std::unique_ptr<beebium::piconet::test::FakePiconetDeviceOnSerial> primary_fake_;
    beebium::PiconetBackend* backend_ = nullptr;  // non-owning
    beebium::EconetTransportRegistry registry_;
    beebium::ExtensionRegistry peripheral_registry_;
    std::unique_ptr<beebium::service::ExtensionUiServiceImpl> service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionUiService::Stub> stub_;
};

// Pull views off the SubscribeView stream until one whose root group's
// device_path control's anchor label matches `expected_anchor_text`,
// or the deadline passes. Returns the last view read (which may or may
// not match).
beebium::View read_until_anchor_text(
    grpc::ClientReader<beebium::View>& reader,
    const std::string& expected_anchor_text,
    std::chrono::milliseconds deadline_in)
{
    auto deadline = std::chrono::steady_clock::now() + deadline_in;
    beebium::View latest;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!reader.Read(&latest)) break;
        if (latest.root().control_case() != beebium::Control::kGroup) continue;
        for (const auto& child : latest.root().group().controls()) {
            if (child.id() == "device_path" &&
                child.control_case() == beebium::Control::kModalEditor &&
                child.modal_editor().anchor().label().text() ==
                    expected_anchor_text) {
                return latest;
            }
        }
    }
    return latest;
}

}  // namespace

TEST_CASE("Piconet end-to-end: SubscribeView -> EditorCommit -> reopen -> next View",
          "[grpc][piconet][extension-ui][reopen]") {
    PiconetExtensionUiFixture fixture;

    // Mode=Listen on construction; force Stop so the editable gate
    // opens (the editable check is server-side in
    // validate_editor_commit, gated on the ModalEditor.editable bool
    // which PiconetUi computes from mode + is_serial_open). We set
    // mode directly rather than dispatching the Enable button so the
    // test stays focused on the reopen flow.
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    grpc::ClientContext sub_ctx;
    beebium::SubscribeViewRequest sub_req;
    sub_req.set_extension_id("piconet");
    auto reader = fixture.stub().SubscribeView(&sub_ctx, sub_req);

    // Initial push: anchor reflects the original PTY's slave path.
    beebium::View initial;
    REQUIRE(reader->Read(&initial));
    REQUIRE(initial.extension_id() == "piconet");
    REQUIRE(initial.root().control_case() == beebium::Control::kGroup);
    bool found_initial_anchor = false;
    std::uint64_t initial_revision = 0;
    for (const auto& c : initial.root().group().controls()) {
        if (c.id() == "device_path") {
            REQUIRE(c.control_case() == beebium::Control::kModalEditor);
            REQUIRE(c.modal_editor().editable());
            REQUIRE(c.modal_editor().anchor().label().text() ==
                    std::string("Device: ") + fixture.slave_path());
            found_initial_anchor = true;
            initial_revision = initial.view_revision();
        }
    }
    REQUIRE(found_initial_anchor);

    // Spin up a second PTY bridge to act as the reopen target. Same
    // FakePiconetDevice machinery as the original; opening it via the
    // SerialFactory in process_pending_reopen mimics the user pointing
    // the adapter at a different /dev/tty.usbmodem* node.
    beebium::piconet::test::FakePiconetDeviceOnSerial replacement_fake;
    REQUIRE(replacement_fake.is_open());

    // Dispatch the EditorCommit carrying the new path. This is what
    // the macOS frontend sends when the user picks an option in the
    // popover and clicks Save.
    grpc::ClientContext disp_ctx;
    beebium::DispatchRequest disp_req;
    disp_req.set_extension_id("piconet");
    disp_req.set_control_id("device_path");
    disp_req.set_view_revision(initial_revision);
    auto* commit = disp_req.mutable_editor_commit();
    auto* field = commit->add_fields();
    field->set_field_id("device_path_value");
    field->set_string_value(replacement_fake.slave_path());

    beebium::DispatchResponse disp_resp;
    REQUIRE(fixture.stub().Dispatch(&disp_ctx, disp_req, &disp_resp).ok());
    REQUIRE(disp_resp.accepted());
    REQUIRE(disp_resp.error().empty());

    // Drive an emulation tick. In production EconetSocket calls
    // receive_frame on every tick; the pending reopen slot is consumed
    // there, the SerialPort is swapped, and the post-swap mark_dirty
    // (via on_async_state_change_ -> ui_.mark_dirty) bumps the view
    // revision. Without this tick the framework would push only the
    // pre-reopen mark_dirty's View (which still shows the old path).
    (void)fixture.backend().receive_frame();
    REQUIRE(fixture.wait_for_serial_open(true));

    // Drain the SubscribeView stream until we see a view with the new
    // anchor text. The framework may have pushed several intermediate
    // views (handle_event's mark_dirty, then process_pending_reopen's
    // notify_state_changed); we only assert on the final one.
    const std::string expected_anchor =
        std::string("Device: ") + replacement_fake.slave_path();
    auto final_view = read_until_anchor_text(*reader, expected_anchor, 1s);

    // Verify final state: anchor reflects the new path; revision has
    // advanced past the initial; the editable gate stays open because
    // mode is still Stop.
    REQUIRE(final_view.view_revision() > initial_revision);
    REQUIRE(final_view.root().control_case() == beebium::Control::kGroup);
    bool found_final_anchor = false;
    for (const auto& c : final_view.root().group().controls()) {
        if (c.id() == "device_path") {
            REQUIRE(c.control_case() == beebium::Control::kModalEditor);
            REQUIRE(c.modal_editor().anchor().label().text() == expected_anchor);
            REQUIRE(c.modal_editor().editable());
            // The shared port picker is an EditableChoice when host ports are
            // enumerated, or a plain TextInput when none are (e.g. Linux CI);
            // both carry the chosen path as a string value.
            const auto& editor = c.modal_editor().editor();
            REQUIRE((editor.control_case() == beebium::Control::kEditableChoice ||
                     editor.control_case() == beebium::Control::kTextInput));
            const std::string editor_value =
                editor.control_case() == beebium::Control::kEditableChoice
                    ? editor.editable_choice().value()
                    : editor.text_input().value();
            REQUIRE(editor_value == replacement_fake.slave_path());
            found_final_anchor = true;
        }
    }
    REQUIRE(found_final_anchor);

    sub_ctx.TryCancel();
    beebium::View drain;
    while (reader->Read(&drain)) {}
    (void)reader->Finish();
}

namespace {

// Variant fixture: starts with a non-existent device path so
// create_backend produces a closed-state PiconetBackend. The
// ModalEditor is the only path the user has to recover.
class PiconetExtensionUiClosedFixture {
public:
    PiconetExtensionUiClosedFixture() {
        auto ext = std::make_unique<beebium::PiconetEconetTransportExtension>();
        beebium::ExtensionManifest manifest;
        manifest.name = "piconet";
        manifest.cli_name = "piconet";
        manifest.extension_kind = "econet-transport";
        ext->set_manifest(std::move(manifest));
        ext->set_config({{"device_path", std::string("/dev/does-not-exist-piconet")}});

        // create_backend now returns a closed-state backend on open
        // failure (the recovery-via-editor enabling change). The
        // backend exists, has its serial closed, and carries the
        // OS-level error in open_error_message_.
        auto backend_owner = ext->create_backend(/*station=*/1);
        REQUIRE(backend_owner != nullptr);
        backend_ = static_cast<beebium::PiconetBackend*>(backend_owner.get());
        REQUIRE_FALSE(backend_->is_serial_open());
        REQUIRE_FALSE(backend_->open_error_message().empty());

        machine_.state().memory.econet_socket.enable(
            /*station=*/1, std::move(backend_owner), /*aun_mode=*/true);

        registry_.add(std::move(ext));

        service_ = std::make_unique<beebium::service::ExtensionUiServiceImpl>(
            registry_, peripheral_registry_);
        services_.push_back(service_.get());

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start(beebium::service::Provenance{},
                       beebium::service::MachineIdentity{},
                       /*enable_advertisement=*/false,
                       /*policy_config=*/{},
                       /*shutdown_callback=*/nullptr,
                       std::span<grpc::Service*>(services_));

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::ExtensionUiService::NewStub(channel_);
    }

    ~PiconetExtensionUiClosedFixture() {
        server_->stop();
    }

    beebium::ExtensionUiService::Stub& stub() { return *stub_; }
    beebium::PiconetBackend& backend() { return *backend_; }

    bool wait_for_serial_open(bool expected) {
        auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (backend_->is_serial_open() == expected) return true;
            std::this_thread::sleep_for(5ms);
        }
        return backend_->is_serial_open() == expected;
    }

private:
    beebium::ModelB machine_;
    beebium::PiconetBackend* backend_ = nullptr;
    beebium::EconetTransportRegistry registry_;
    beebium::ExtensionRegistry peripheral_registry_;
    std::unique_ptr<beebium::service::ExtensionUiServiceImpl> service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionUiService::Stub> stub_;
};

}  // namespace

TEST_CASE("Piconet recovery: closed-state startup -> EditorCommit -> adapter responsive",
          "[grpc][piconet][extension-ui][reopen][recovery]") {
    PiconetExtensionUiClosedFixture fixture;

    grpc::ClientContext sub_ctx;
    beebium::SubscribeViewRequest sub_req;
    sub_req.set_extension_id("piconet");
    auto reader = fixture.stub().SubscribeView(&sub_ctx, sub_req);

    // Initial View: anchor shows the bad path; Indicator is ERROR
    // with "Cannot open device: ..."; ModalEditor is editable
    // because the serial is closed.
    beebium::View initial;
    REQUIRE(reader->Read(&initial));
    REQUIRE(initial.root().control_case() == beebium::Control::kGroup);

    auto find_control = [](const beebium::View& v, const std::string& id) {
        for (const auto& c : v.root().group().controls()) {
            if (c.id() == id) return &c;
        }
        return static_cast<const beebium::Control*>(nullptr);
    };

    {
        const auto* device = find_control(initial, "device_path");
        REQUIRE(device != nullptr);
        REQUIRE(device->control_case() == beebium::Control::kModalEditor);
        REQUIRE(device->modal_editor().editable());
        REQUIRE(device->modal_editor().anchor().label().text() ==
                "Device: /dev/does-not-exist-piconet");

        const auto* indicator = find_control(initial, "connected");
        REQUIRE(indicator != nullptr);
        REQUIRE(indicator->control_case() == beebium::Control::kIndicator);
        REQUIRE(indicator->indicator().state() == beebium::Indicator_State_ERROR);
        REQUIRE(indicator->indicator().text().find("Cannot open device") !=
                std::string::npos);
    }

    // Spin up a real PTY bridge and dispatch the corrected path.
    beebium::piconet::test::FakePiconetDeviceOnSerial replacement_fake;
    REQUIRE(replacement_fake.is_open());

    grpc::ClientContext disp_ctx;
    beebium::DispatchRequest disp_req;
    disp_req.set_extension_id("piconet");
    disp_req.set_control_id("device_path");
    disp_req.set_view_revision(initial.view_revision());
    auto* commit = disp_req.mutable_editor_commit();
    auto* field = commit->add_fields();
    field->set_field_id("device_path_value");
    field->set_string_value(replacement_fake.slave_path());

    beebium::DispatchResponse disp_resp;
    REQUIRE(fixture.stub().Dispatch(&disp_ctx, disp_req, &disp_resp).ok());
    REQUIRE(disp_resp.accepted());

    (void)fixture.backend().receive_frame();
    REQUIRE(fixture.wait_for_serial_open(true));

    // Drain the stream until we see Indicator OK naming the reopened
    // device ("Piconet at <path>") -- the user-visible signal that the
    // reopen succeeded.
    const std::string expected_indicator =
        "Piconet at " + replacement_fake.slave_path();
    auto deadline = std::chrono::steady_clock::now() + 1s;
    beebium::View latest = initial;
    bool saw_responsive = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!reader->Read(&latest)) break;
        const auto* indicator = find_control(latest, "connected");
        if (indicator &&
            indicator->indicator().state() == beebium::Indicator_State_OK &&
            indicator->indicator().text() == expected_indicator) {
            saw_responsive = true;
            break;
        }
    }
    REQUIRE(saw_responsive);

    // And the anchor reflects the new path.
    const auto* device = find_control(latest, "device_path");
    REQUIRE(device != nullptr);
    REQUIRE(device->modal_editor().anchor().label().text() ==
            std::string("Device: ") + replacement_fake.slave_path());

    sub_ctx.TryCancel();
    beebium::View drain;
    while (reader->Read(&drain)) {}
    (void)reader->Finish();
}
