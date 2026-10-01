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

// ExtensionUiServiceImpl method definitions, deliberately compiled into
// beebium_extension_ui_proto rather than alongside the rest of the
// service layer. The protoc-generated Beebium_ExtensionUiService::Service
// base class lives in that same shared library (beebium_extension_ui_proto.dll
// on Windows, libbeebium_extension_ui_proto.dylib/.so elsewhere); having
// the handler subclass's vtable + method bodies emitted in the same module
// avoids the cross-DLL closure-list corruption documented in
// docs/discussion/grpc-windows-streaming-race.md (gRPC issue #39198).
//
// All other gRPC services in the system have proto stubs and impls
// statically linked into the same module, so this concern is local to
// ExtensionUiService -- the only service whose proto+grpc stubs live in
// a shared library by design (so plugin DLLs can share the descriptors).

#include "beebium/service/ExtensionUiService.hpp"

#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace beebium::service {

ExtensionUiServiceImpl::ExtensionUiServiceImpl(
    EconetTransportRegistry& transport_registry,
    ExtensionRegistry& peripheral_registry)
    : transport_registry_(transport_registry),
      peripheral_registry_(peripheral_registry) {}

ExtensionUiServiceImpl::~ExtensionUiServiceImpl() = default;

grpc::Status ExtensionUiServiceImpl::SubscribeView(
    grpc::ServerContext* context,
    const ::beebium::SubscribeViewRequest* request,
    grpc::ServerWriter<::beebium::View>* writer)
{
    Extension* ext = find_extension(request->extension_id());
    ExtensionUi* ui = ext ? ext->ui() : nullptr;
    if (!ui) {
        return grpc::Status(
            grpc::StatusCode::NOT_FOUND,
            "Extension '" + request->extension_id() +
            "' not found or has no UI");
    }

    // Initial last_pushed = 0 ensures the first iteration always
    // pushes (ExtensionUi::current_revision() starts at 1).
    std::uint64_t last_pushed = 0;
    const auto interval = std::chrono::milliseconds(50);

    while (!context->IsCancelled()) {
        std::uint64_t curr = ui->current_revision();
        if (curr != last_pushed) {
            ::beebium::View view;
            ui->build_view(&view);
            view.set_extension_id(request->extension_id());
            view.set_view_revision(curr);
            if (!writer->Write(view)) {
                break;  // client disconnected
            }
            last_pushed = curr;
        }
        std::this_thread::sleep_for(interval);
    }
    return grpc::Status::OK;
}

grpc::Status ExtensionUiServiceImpl::Dispatch(
    grpc::ServerContext* /*context*/,
    const ::beebium::DispatchRequest* request,
    ::beebium::DispatchResponse* response)
{
    Extension* ext = find_extension(request->extension_id());
    if (!ext) {
        response->set_accepted(false);
        response->set_error(
            "unknown extension: " + request->extension_id());
        return grpc::Status::OK;
    }
    ExtensionUi* ui = ext->ui();
    if (!ui) {
        response->set_accepted(false);
        response->set_error(
            "extension '" + request->extension_id() + "' has no UI");
        return grpc::Status::OK;
    }

    std::uint64_t curr_rev = ui->current_revision();
    if (request->view_revision() != curr_rev) {
        response->set_accepted(false);
        response->set_error(
            "stale event: client revision " +
            std::to_string(request->view_revision()) +
            ", current " + std::to_string(curr_rev));
        return grpc::Status::OK;
    }

    ::beebium::View view;
    ui->build_view(&view);

    std::unordered_map<std::string, const ::beebium::Control*> ids;
    detail::collect_control_ids(view.root(), ids);
    auto it = ids.find(request->control_id());
    if (it == ids.end()) {
        response->set_accepted(false);
        response->set_error(
            "unknown control id: " + request->control_id());
        return grpc::Status::OK;
    }

    const ::beebium::Control* target = it->second;
    std::string error;
    if (!detail::payload_matches_control(
            *request, target->control_case(), error)) {
        response->set_accepted(false);
        response->set_error(std::move(error));
        return grpc::Status::OK;
    }

    // ModalEditor carries additional structure that Dispatch must
    // validate before running handle_event: editable gate, every
    // EditorCommit field_id resolves to a sub-control, each value
    // variant matches its sub-control's type. All-or-nothing.
    if (target->control_case() == ::beebium::Control::kModalEditor) {
        if (!detail::validate_editor_commit(
                *request, target->modal_editor(), error)) {
            response->set_accepted(false);
            response->set_error(std::move(error));
            return grpc::Status::OK;
        }
    }

    // EditableList carries an event (add/edit/remove/action) whose item id,
    // action id and commit fields Dispatch validates against the current
    // list before handle_event sees it -- the same all-or-nothing guarantee
    // ModalEditor commits get.
    if (target->control_case() == ::beebium::Control::kEditableList) {
        if (!detail::validate_editable_list_event(
                *request, target->editable_list(), error)) {
            response->set_accepted(false);
            response->set_error(std::move(error));
            return grpc::Status::OK;
        }
    }

    // FileReference dispatches a server action id, which must name one of the
    // actions the view offered on that reference.
    if (target->control_case() == ::beebium::Control::kFileReference) {
        if (!detail::validate_file_reference_action(
                *request, target->file_reference(), error)) {
            response->set_accepted(false);
            response->set_error(std::move(error));
            return grpc::Status::OK;
        }
    }

    ui->handle_event(*request);
    response->set_accepted(true);
    return grpc::Status::OK;
}

Extension* ExtensionUiServiceImpl::find_extension(
    const std::string& id) const
{
    // Lookup is by Extension::id(), not manifest name, so that
    // multi-instance extensions (e.g. three SCSI hard discs on the
    // same adapter) are each individually addressable. For
    // single-instance extensions (AUN, Piconet) the auto-assigned id
    // equals the manifest name, so existing clients are unaffected
    // by the wire-level rename of the field from extension_name to
    // extension_id.
    for (const auto& ext : transport_registry_.extensions()) {
        if (ext->id() == id) {
            return ext.get();
        }
    }
    for (auto* ext : peripheral_registry_.extensions()) {
        if (ext->id() == id) {
            return ext;
        }
    }
    return nullptr;
}

}  // namespace beebium::service
