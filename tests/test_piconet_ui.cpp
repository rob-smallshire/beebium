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

// Direct (non-gRPC) tests for PiconetUi. Drives a real
// PiconetEconetTransportExtension wired to a FakePiconetDevice on a PTY,
// then exercises the UI hooks the framework would call: build_view()
// produces the expected three-control tree, and handle_event() for the
// mode_toggle control writes SET_MODE STOP/LISTEN to the firmware fake.
//
// The end-to-end gRPC path is covered separately by the framework's
// own test_grpc_extension_ui_service.cpp -- this file focuses on the
// piconet-specific behaviour without re-testing the framework's
// validation gauntlet.

#include <catch2/catch_test_macros.hpp>

#include "PiconetEconetTransportExtension.hpp"
#include "PiconetUi.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include "extension_ui.pb.h"

#include "piconet/FakePiconetDeviceOnSerial.hpp"

// NOTE: resolves to the PTY bridge on POSIX and the named-pipe bridge
// on Windows. Public API is identical (is_open, slave_path, device).

#include <chrono>
#include <memory>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Spin up the extension wired to a serial-bridged FakePiconetDevice
// (PTY on POSIX, named pipe on Windows). PiconetBackend opens the
// client side via PosixSerialPort / Win32SerialPort and starts its
// reader thread; on construction it sends SET_STATION + SET_MODE LISTEN
// which the fake records on the server side.
class PiconetUiFixture {
public:
    PiconetUiFixture() {
        fake_ = std::make_unique<beebium::piconet::test::FakePiconetDeviceOnSerial>();
        REQUIRE(fake_->is_open());

        ext_ = std::make_unique<beebium::PiconetEconetTransportExtension>();
        ext_->set_config({{"device_path", fake_->slave_path()}});
        backend_owner_ = ext_->create_backend(/*station=*/1);
        REQUIRE(backend_owner_ != nullptr);

        // Wait for the constructor's initial SET_MODE LISTEN to round-trip
        // through the PTY into the fake's mode_ field. Bounded short
        // sleep -- 50ms is comfortably above the pumper thread's polling
        // cadence.
        wait_for_mode(beebium::piconet::Mode::Listen);
    }

    beebium::PiconetEconetTransportExtension& extension() { return *ext_; }
    beebium::piconet::test::FakePiconetDevice& fake_device() { return fake_->device(); }
    const std::string& slave_path() const { return fake_->slave_path(); }
    beebium::PiconetBackend& backend() {
        return *static_cast<beebium::PiconetBackend*>(backend_owner_.get());
    }

    // Block until the fake observes the requested mode, up to 500ms.
    // Returns true if observed, false on timeout.
    bool wait_for_mode(beebium::piconet::Mode mode) {
        auto deadline = std::chrono::steady_clock::now() + 500ms;
        while (std::chrono::steady_clock::now() < deadline) {
            if (fake_->device().mode() == mode) return true;
            std::this_thread::sleep_for(5ms);
        }
        return fake_->device().mode() == mode;
    }

    // Simulate USB hot-unplug by destroying the fake (closes the PTY
    // master), which causes the slave fd PiconetBackend holds to see
    // EOF/HUP on the next read. PiconetBackend's reader thread then
    // closes its serial port, flipping is_serial_open() to false.
    void simulate_hot_unplug() { fake_.reset(); }

    // Block until is_serial_open() returns false, up to 3 seconds.
    // Used after simulate_hot_unplug() to wait for the reader thread
    // to react. Returns true if observed, false on timeout.
    bool wait_for_serial_close() {
        auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!backend().is_serial_open()) return true;
            std::this_thread::sleep_for(20ms);
        }
        return !backend().is_serial_open();
    }

    // Block until ExtensionUi::current_revision() exceeds `before`,
    // up to 1 second. Used after wait_for_serial_close() to give the
    // reader thread's on_async_state_change callback a moment to fire
    // -- close() and the callback are sequential in the same thread
    // but not atomic from the test's perspective.
    bool wait_for_revision_bump(std::uint64_t before) {
        auto* ui = extension().ui();
        if (!ui) return false;
        auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (ui->current_revision() > before) return true;
            std::this_thread::sleep_for(5ms);
        }
        return ui->current_revision() > before;
    }

private:
    std::unique_ptr<beebium::piconet::test::FakePiconetDeviceOnSerial> fake_;
    std::unique_ptr<beebium::PiconetEconetTransportExtension> ext_;
    std::unique_ptr<beebium::NetworkBackend> backend_owner_;
};

}  // namespace

TEST_CASE("PiconetUi build_view (live backend) produces ModalEditor + Indicator + Button",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    beebium::View view;
    ui->build_view(&view);

    const auto& root = view.root();
    REQUIRE(root.id() == "root");
    REQUIRE(root.control_case() == beebium::Control::kGroup);
    REQUIRE(root.group().label() == "Piconet");
    REQUIRE(root.group().controls_size() == 3);

    // Device path is a ModalEditor whose anchor is a Label. Editable
    // is gated off here because the backend is live and listening, so
    // the frontend renders the anchor as read-only.
    const auto& device = root.group().controls(0);
    REQUIRE(device.id() == "device_path");
    REQUIRE(device.control_case() == beebium::Control::kModalEditor);
    const auto& modal = device.modal_editor();
    REQUIRE_FALSE(modal.editable());
    REQUIRE(modal.commit_role() == beebium::ModalEditor_CommitRole_SAVE);
    REQUIRE(modal.show_cancel());

    const auto& anchor = modal.anchor();
    REQUIRE(anchor.control_case() == beebium::Control::kLabel);
    REQUIRE(anchor.label().text() ==
            std::string("Device: ") + fixture.slave_path());

    // Editor body is a single EditableChoice. Frontends render it as
    // the platform's idiomatic combobox; the field is the single
    // source of truth on commit. The previous shape (Choice +
    // TextInput in a Group, then TextInput with suggestions) is
    // superseded -- one primitive captures the "pick from known list
    // OR type your own" intent at the right level of abstraction.
    const auto& editor = modal.editor();
    REQUIRE(editor.id() == "device_path_value");
    // The port control is an EditableChoice dropdown when host ports are
    // enumerated, or a plain TextInput when none are -- depends on the test
    // host. Either way the value and placeholder reflect the current path.
    if (editor.control_case() == beebium::Control::kEditableChoice) {
        REQUIRE(editor.editable_choice().value() == fixture.slave_path());
        REQUIRE(editor.editable_choice().placeholder() == fixture.slave_path());
    } else {
        REQUIRE(editor.control_case() == beebium::Control::kTextInput);
        REQUIRE(editor.text_input().value() == fixture.slave_path());
        REQUIRE(editor.text_input().placeholder() == fixture.slave_path());
    }

    const auto& indicator = root.group().controls(1);
    REQUIRE(indicator.id() == "connected");
    REQUIRE(indicator.control_case() == beebium::Control::kIndicator);
    REQUIRE(indicator.indicator().state() == beebium::Indicator_State_OK);
    // Serial open: the indicator names the connected device.
    REQUIRE(indicator.indicator().text() == "Piconet at " + fixture.slave_path());

    const auto& button = root.group().controls(2);
    REQUIRE(button.id() == "enable_action");
    REQUIRE(button.control_case() == beebium::Control::kButton);
    REQUIRE(button.button().enabled());
    // Constructor put the firmware in LISTEN, so the action that the
    // user can take next is "Disable" (mute).
    REQUIRE(button.button().label() == "Disable");
}

TEST_CASE("PiconetUi build_view (closed-state backend) offers Retry and surfaces OS error",
          "[piconet][ui]") {
    // Construct an extension with a device path that won't open at the
    // POSIX layer. create_backend now returns a closed-state backend
    // (rather than nullptr) so the ModalEditor can call
    // request_reopen() to recover -- the wrong-path-at-startup case
    // that motivated the editor.
    beebium::PiconetEconetTransportExtension ext;
    ext.set_config({{"device_path", "/dev/does-not-exist-piconet"}});
    auto backend = ext.create_backend(/*station=*/1);
    REQUIRE(backend != nullptr);
    REQUIRE_FALSE(static_cast<beebium::PiconetBackend*>(backend.get())
                      ->is_serial_open());
    REQUIRE_FALSE(static_cast<beebium::PiconetBackend*>(backend.get())
                      ->open_error_message().empty());

    auto* ui = ext.ui();
    REQUIRE(ui != nullptr);

    beebium::View view;
    ui->build_view(&view);

    const auto& root = view.root();
    REQUIRE(root.control_case() == beebium::Control::kGroup);
    // Three controls -- ModalEditor + Indicator + Retry. The Enable button
    // is suppressed because the serial port is closed; in its place the
    // Retry button appears so the user can re-run discovery / reopen.
    REQUIRE(root.group().controls_size() == 3);

    const auto& retry = root.group().controls(2);
    REQUIRE(retry.id() == "retry_action");
    REQUIRE(retry.control_case() == beebium::Control::kButton);
    REQUIRE(retry.button().enabled());
    REQUIRE(retry.button().label() == "Retry");

    const auto& device = root.group().controls(0);
    REQUIRE(device.id() == "device_path");
    REQUIRE(device.control_case() == beebium::Control::kModalEditor);
    // Serial isn't open, so the editor is editable -- this is the path
    // the user takes to fix a wrong-at-startup configuration.
    REQUIRE(device.modal_editor().editable());
    const auto& anchor = device.modal_editor().anchor();
    REQUIRE(anchor.control_case() == beebium::Control::kLabel);
    REQUIRE(anchor.label().text() == "Device: /dev/does-not-exist-piconet");

    const auto& indicator = root.group().controls(1);
    REQUIRE(indicator.id() == "connected");
    REQUIRE(indicator.control_case() == beebium::Control::kIndicator);
    REQUIRE(indicator.indicator().state() == beebium::Indicator_State_ERROR);
    // The exact OS error text varies by platform: POSIX strerror(ENOENT)
    // is "No such file or directory"; Windows FormatMessage for
    // ERROR_PATH_NOT_FOUND / ERROR_FILE_NOT_FOUND is "The system cannot
    // find the path specified." / "...file specified.". The test accepts
    // any of these substrings so the assertion stays platform-neutral.
    REQUIRE(indicator.indicator().text().find("Cannot open device") !=
            std::string::npos);
    const auto& text = indicator.indicator().text();
    REQUIRE((text.find("No such file") != std::string::npos ||
             text.find("cannot find the path") != std::string::npos ||
             text.find("cannot find the file") != std::string::npos));
}

TEST_CASE("PiconetUi enable_action toggles mode between LISTEN and STOP",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(ui != nullptr);

    // Constructor leaves us in LISTEN; first dispatch should mute.
    REQUIRE(fixture.backend().mode() == beebium::piconet::Mode::Listen);

    beebium::DispatchRequest req;
    req.set_extension_id("piconet");
    req.set_control_id("enable_action");
    req.set_view_revision(ui->current_revision());
    // Buttons take no payload.

    ui->handle_event(req);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    // Second dispatch should re-enable.
    req.set_view_revision(ui->current_revision());
    ui->handle_event(req);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Listen));
}

TEST_CASE("PiconetUi enable_action label flips with the cached mode",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();

    // After SET_MODE STOP, the next View should show Enable as the
    // available action.
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    beebium::View view;
    ui->build_view(&view);
    const auto& button = view.root().group().controls(2);
    REQUIRE(button.id() == "enable_action");
    REQUIRE(button.button().label() == "Enable");
}

TEST_CASE("PiconetUi enable_action dispatch bumps the view revision",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    auto initial = ui->current_revision();

    beebium::DispatchRequest req;
    req.set_extension_id("piconet");
    req.set_control_id("enable_action");
    req.set_view_revision(initial);

    ui->handle_event(req);
    REQUIRE(ui->current_revision() > initial);
}

TEST_CASE("PiconetBackend is_connected requires both serial open AND mode LISTEN",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    REQUIRE(fixture.backend().is_serial_open());
    // Constructor leaves us in LISTEN with serial open -> connected.
    REQUIRE(fixture.backend().is_connected());

    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));
    // Serial still open, but mode != Listen, so not "connected" in the
    // user-meaningful sense (BBC is muted from the wire).
    REQUIRE(fixture.backend().is_serial_open());
    REQUIRE_FALSE(fixture.backend().is_connected());

    fixture.backend().set_mode(beebium::piconet::Mode::Listen);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Listen));
    REQUIRE(fixture.backend().is_connected());
}

TEST_CASE("PiconetBackend hot-unplug closes serial and updates the UI",
          "[piconet][ui]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    REQUIRE(fixture.backend().is_serial_open());
    REQUIRE(fixture.backend().is_connected());
    const auto pre_unplug_revision = ui->current_revision();

    // Yank the cable: destroying the fake closes the PTY master, the
    // slave PiconetBackend holds sees EOF on the next read, the reader
    // thread closes the serial port and exits.
    fixture.simulate_hot_unplug();
    REQUIRE(fixture.wait_for_serial_close());
    REQUIRE_FALSE(fixture.backend().is_serial_open());
    REQUIRE_FALSE(fixture.backend().is_connected());

    // The reader thread fires the on_async_state_change callback after
    // closing the serial port; PiconetEconetTransportExtension wires
    // it to ui_.mark_dirty(). Without this the framework's poll loop
    // would never push a new View and the panel would stay frozen
    // showing "Adapter responsive" + Disable button forever.
    //
    // close() and the callback are sequential in the reader thread but
    // not atomic from the test's perspective: on a fast scheduler the
    // test wakes up from wait_for_serial_close()'s 20ms poll between
    // those two reader-thread calls, observes is_serial_open() == false
    // already, and races into current_revision() before the callback
    // has fired. Wait separately (with its own deadline) for the
    // revision bump rather than asserting it synchronously.
    REQUIRE(fixture.wait_for_revision_bump(pre_unplug_revision));
    REQUIRE(ui->current_revision() > pre_unplug_revision);

    // The Indicator now reads as ERROR with the "Adapter offline"
    // text (no recorded open_error_message because the open did
    // succeed initially -- the adapter went away later).
    beebium::View view;
    ui->build_view(&view);
    const auto& indicator = view.root().group().controls(1);
    REQUIRE(indicator.id() == "connected");
    REQUIRE(indicator.indicator().state() == beebium::Indicator_State_ERROR);
    REQUIRE(indicator.indicator().text() == "Adapter offline");

    // Three controls: the Enable button is gone (nothing to drive) and the
    // Retry button takes its place so the user can reconnect after replugging.
    REQUIRE(view.root().group().controls_size() == 3);
    const auto& retry = view.root().group().controls(2);
    REQUIRE(retry.id() == "retry_action");
    REQUIRE(retry.button().label() == "Retry");
}

TEST_CASE("PiconetUi ModalEditor editable gate follows the mode + serial state",
          "[piconet][ui][modal_editor]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();

    auto device_control = [&] {
        beebium::View view;
        ui->build_view(&view);
        return view.root().group().controls(0);
    };

    // Live + Listen -> read-only.
    {
        auto d = device_control();
        REQUIRE(d.control_case() == beebium::Control::kModalEditor);
        REQUIRE_FALSE(d.modal_editor().editable());
    }

    // Muted (Mode::Stop) while serial still open -> editable. The user
    // has explicitly disabled the wire so re-pointing it is harmless.
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));
    {
        auto d = device_control();
        REQUIRE(d.modal_editor().editable());
    }

    // Hot-unplug -> serial closes -> editable (user needs to re-point
    // to recover). mode() stays at Stop from the previous step.
    fixture.simulate_hot_unplug();
    REQUIRE(fixture.wait_for_serial_close());
    {
        auto d = device_control();
        REQUIRE(d.modal_editor().editable());
    }
}

TEST_CASE("PiconetUi EditorCommit with the device_path field calls backend request_reopen",
          "[piconet][ui][modal_editor]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();

    // Disable first -- editable gate requires mode != Listen (or the
    // serial closed). We want editable=true for this test.
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    beebium::DispatchRequest req;
    req.set_extension_id("piconet");
    req.set_control_id("device_path");
    req.set_view_revision(ui->current_revision());
    auto* commit = req.mutable_editor_commit();
    auto* field = commit->add_fields();
    field->set_field_id("device_path_value");
    field->set_string_value("/dev/null");

    // Dispatch. request_reopen posts the path to the atomic slot; the
    // next receive_frame() call on the emulation thread consumes it
    // and performs the teardown + reopen.
    ui->handle_event(req);
    (void)fixture.backend().receive_frame();

    REQUIRE(fixture.backend().config().device_path == "/dev/null");
}

TEST_CASE("PiconetUi EditorCommit with empty commit is a safe no-op",
          "[piconet][ui][modal_editor]") {
    PiconetUiFixture fixture;
    auto* ui = fixture.extension().ui();
    fixture.backend().set_mode(beebium::piconet::Mode::Stop);
    REQUIRE(fixture.wait_for_mode(beebium::piconet::Mode::Stop));

    const std::string before_path = fixture.backend().config().device_path;
    const auto before_rev = ui->current_revision();

    beebium::DispatchRequest req;
    req.set_extension_id("piconet");
    req.set_control_id("device_path");
    req.set_view_revision(before_rev);
    // Empty editor_commit: no fields. Should not trigger a reopen.
    req.mutable_editor_commit();

    ui->handle_event(req);
    (void)fixture.backend().receive_frame();

    REQUIRE(fixture.backend().config().device_path == before_path);
    REQUIRE(ui->current_revision() > before_rev);  // mark_dirty was called.
}
