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

#include "PiconetUi.hpp"

#include "PiconetEconetTransportExtension.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Mode.hpp"
#include "beebium/serial/SerialPortPickerControl.hpp"

#include "extension_ui.pb.h"

#include <algorithm>
#include <string>
#include <vector>

namespace beebium {

namespace {

// Top-level control ids.
constexpr const char* CONTROL_DEVICE_PATH   = "device_path";
constexpr const char* CONTROL_CONNECTED     = "connected";
constexpr const char* CONTROL_ENABLE_ACTION = "enable_action";
constexpr const char* CONTROL_RETRY_ACTION  = "retry_action";

// Sub-control id inside CONTROL_DEVICE_PATH's editor tree. Addressed via
// EditorCommit.field_id on the server side; the frontend reads the same
// id when building the per-sub-control buffer during the popover
// session. Single source of truth -- the field carries the chosen path
// whether the user typed it directly or picked it from the suggestions
// list. The earlier two-control design (Choice + TextInput) created an
// ambiguity about which value would take effect on commit; collapsing
// to one field with inline suggestions makes the source of truth
// unambiguous.
constexpr const char* FIELD_DEVICE_PATH = "device_path_value";

}  // namespace

void PiconetUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();
    group->set_label("Piconet");

    // build_view runs on the gRPC SubscribeView thread. PiconetBackend's
    // mutable state (config_.device_path, serial_, open_error_message_)
    // is mutated on the emulation thread (process_pending_reopen). Read
    // an atomic snapshot once at the top so the rest of build_view sees
    // a consistent view; concurrent direct accesses caused heap
    // corruption near serial_factory_ on Windows. See
    // docs/discussion/grpc-windows-streaming-race.md.
    const PiconetBackend* backend = ext_.backend();
    const auto snap = backend ? backend->ui_snapshot()
                              : PiconetBackend::UiSnapshot{};

    // Device path editor. The anchor is a Label showing the current
    // device_path; the editor body is a Group containing a Choice over
    // enumerated host serial ports and a TextInput for a custom path.
    // Editable only while Piconet is disabled -- see the design doc's
    // "editable gate" section. Per build_view's purity contract we snap
    // the current path, the currently-enumerated ports, and the mode-
    // derived gate; the frontend holds buffers across the popover
    // session and ships an EditorCommit on confirm.
    {
        std::string current_path;
        if (backend) {
            current_path = snap.device_path;
        } else if (auto path = ext_.config_value("device_path");
                   path && !path->empty()) {
            current_path.assign(path->data(), path->size());
        }

        auto* control = group->add_controls();
        control->set_id(CONTROL_DEVICE_PATH);
        auto* modal = control->mutable_modal_editor();

        // Gate: editable unless a serial is open AND the firmware is
        // listening. Mid-session hot-unplug closes the serial -> editable.
        // Mode::Stop / Monitor -> editable. Only the live-talking state
        // (serial open + Listen) is read-only, matching the design.
        const bool serial_open = backend && snap.serial_open;
        const bool listening = backend && snap.listening;
        modal->set_editable(!backend || !serial_open || !listening);
        modal->set_commit_role(ModalEditor_CommitRole_SAVE);
        // Cancel affordance is cheap and makes the editor symmetric on
        // platforms where escape / click-outside isn't obvious. macOS
        // popovers dismiss on click-outside regardless.
        modal->set_show_cancel(true);

        // Anchor: a plain Label showing the current path. Frontends use
        // this as the always-visible element and attach the edit
        // affordance to it when editable.
        {
            auto* anchor = modal->mutable_anchor();
            anchor->set_id(CONTROL_DEVICE_PATH);
            std::string text = "Device: ";
            text.append(current_path.empty() ? std::string("(unset)")
                                             : current_path);
            anchor->mutable_label()->set_text(std::move(text));
        }

        // Editor body: the shared port picker -- an EditableChoice combobox
        // (NSComboBox on macOS, <input list> on the web, ...) over the
        // enumerated ports, or a plain TextInput when none are found. The field
        // is the single source of truth on commit (string_value either way).
        serial::build_port_picker_control(modal->mutable_editor(), FIELD_DEVICE_PATH,
                                          "Serial port", current_path,
                                          "/dev/tty.usbmodem...");
    }

    // Indicator: USB-level health -- is the adapter physically there
    // and the serial port open? Independent of LISTEN/STOP mode. This
    // distinguishes "USB unplugged" (red) from "muted via STOP mode"
    // (green USB OK + grey header Disconnected); both produce zero
    // traffic but the user acts on them differently. When the
    // underlying open() failed at startup, surface the OS error text
    // so the user can fix it (wrong path, permissions, device not
    // present).
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_CONNECTED);
        auto* indicator = control->mutable_indicator();
        const bool serial_open = backend && snap.serial_open;
        indicator->set_state(serial_open ? Indicator_State_OK
                                         : Indicator_State_ERROR);
        if (serial_open) {
            // Adapter physically present and its serial port open. Name the
            // device so the user can confirm which one was selected --
            // especially after a discovery Retry chose it for them.
            indicator->set_text(snap.device_path.empty()
                                    ? std::string("Adapter responsive")
                                    : "Piconet at " + snap.device_path);
        } else if (backend && !snap.open_error_message.empty()) {
            // Backend exists but its serial is not open. The error
            // text comes from the backend itself: populated by
            // process_pending_reopen() when a user-initiated reopen (a
            // manual path change) failed to open the device.
            indicator->set_text("Cannot open device: " +
                                snap.open_error_message);
        } else if (!ext_.discovery_status().empty()) {
            // Discovery ran and found no usable device. Show the concise,
            // GUI-appropriate status (no device_path advice -- that's for
            // the CLI). The verbose diagnostic went to the boot/CLI log.
            indicator->set_text(ext_.discovery_status());
        } else if (!ext_.open_error_message().empty()) {
            indicator->set_text("Cannot open device: " +
                                ext_.open_error_message());
        } else {
            // Backend was constructed but the serial port has since
            // closed (read error / hot-unplug detection in
            // PiconetBackend's reader_loop). Without a recorded error
            // string we can't be more specific.
            indicator->set_text("Adapter offline");
        }
    }

    // Enable / Disable action button. Single Button whose label flips
    // with the current firmware mode. Symmetric with AunUi's
    // Connect/Disconnect button (same UX role: change "is the BBC in
    // the loop?"). Suppressed when there's no live backend OR when the
    // serial port has closed (hot-unplug, read error) -- in either
    // case set_mode would be a no-op because the firmware is
    // unreachable, so the affordance would be misleading. A future
    // Reconnect button would slot in here for both cases.
    if (backend && snap.serial_open) {
        auto* control = group->add_controls();
        control->set_id(CONTROL_ENABLE_ACTION);
        auto* button = control->mutable_button();
        button->set_label(snap.listening ? "Disable" : "Enable");
        button->set_enabled(true);
    } else if (backend) {
        // No serial open: offer Retry, which re-runs discovery and, on
        // success, brings the device up live (Listen) without restarting
        // the machine. Covers both the discovery-found-nothing-at-boot case
        // and a mid-session hot-unplug where the user has since replugged.
        auto* control = group->add_controls();
        control->set_id(CONTROL_RETRY_ACTION);
        auto* button = control->mutable_button();
        button->set_label("Retry");
        button->set_enabled(true);
    }
}

void PiconetUi::handle_event(const DispatchRequest& request) {
    // The framework has already validated extension/control/payload; we
    // only need to dispatch on control_id. Unknown ids reach here only
    // if a future control was added without a matching branch -- silent
    // ignore is the correct fallback (no firmware effect).
    if (request.control_id() == CONTROL_ENABLE_ACTION) {
        if (auto* backend = ext_.backend()) {
            const bool listening = backend->mode() == piconet::Mode::Listen;
            backend->set_mode(listening ? piconet::Mode::Stop
                                        : piconet::Mode::Listen);
            mark_dirty();
        }
        return;
    }

    if (request.control_id() == CONTROL_RETRY_ACTION) {
        // Re-run discovery and, on success, bring the device up live in
        // place (see PiconetEconetTransportExtension::retry_discovery). The
        // probe runs synchronously on this dispatch thread -- bounded by the
        // per-candidate STATUS timeout and the candidate cap -- so the
        // client observes the honest before/after, not a transient state.
        ext_.retry_discovery();
        mark_dirty();
        return;
    }

    if (request.control_id() == CONTROL_DEVICE_PATH) {
        // EditorCommit from the device-path ModalEditor. The framework
        // has already verified the payload is an EditorCommit, the
        // single device_path_value field id is known, and its value
        // variant is string. The TextInput is the single source of
        // truth -- whether the user typed the path or picked it from
        // the suggestions list, the field carries the chosen value.
        auto* backend = ext_.backend();
        if (!backend) {
            // No backend means the startup open failed outright; the
            // user has no live adapter to re-point. A future "Reconnect"
            // affordance would rebuild the backend via the extension;
            // until that exists, silently ignore.
            return;
        }

        std::string new_path;
        for (const auto& field : request.editor_commit().fields()) {
            if (field.field_id() == FIELD_DEVICE_PATH) {
                new_path = field.string_value();
                break;
            }
        }

        if (new_path.empty()) {
            // Empty commit (user opened the popover and saved without
            // a value). Treat as a no-op; mark_dirty so the client
            // redraws and the popover-local buffer state is reset.
            mark_dirty();
            return;
        }

        backend->request_reopen(std::move(new_path));
        mark_dirty();
    }
}

}  // namespace beebium
