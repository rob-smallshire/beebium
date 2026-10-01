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

#ifndef BEEBIUM_SERVICE_EXTENSION_UI_SERVICE_HPP
#define BEEBIUM_SERVICE_EXTENSION_UI_SERVICE_HPP

// Server-driven Extension UI gRPC service.
//
// Reads from EconetTransportRegistry and ExtensionRegistry (peripheral)
// to discover extensions; routes SubscribeView streams and Dispatch
// events to the matching ExtensionUi. Owns view-revision plumbing and
// the dispatch validation gauntlet (extension exists, control id is
// known, payload type matches the control type, view revision is
// current). Extensions only see validated events.

#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionUi.hpp"

#include "extension_ui.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace beebium::service {

namespace detail {

// Walk the View's Control tree, populating (id -> Control*) so the
// Dispatch validator can look up an addressed control by id and check
// both its type and its underlying message.
//
// Groups descend into their children. ModalEditors deliberately do NOT
// descend into their anchor or editor sub-trees: the ModalEditor itself
// is the only dispatch target at the top level, and its editor sub-
// controls are addressed only via EditorCommit.fields.field_id (which
// uses a separate per-ModalEditor lookup; see collect_editor_fields).
// Keeping editor sub-controls out of the top-level map also prevents
// id collisions between two ModalEditors' sub-trees from shadowing
// each other.
inline void collect_control_ids(
    const ::beebium::Control& control,
    std::unordered_map<std::string, const ::beebium::Control*>& out)
{
    out[control.id()] = &control;
    if (control.control_case() == ::beebium::Control::kGroup) {
        for (const auto& child : control.group().controls()) {
            collect_control_ids(child, out);
        }
    }
}

// Walk a ModalEditor's editor sub-tree, populating (field_id -> Control*)
// for EditorCommit field validation. Descends into Groups; does not
// descend into nested ModalEditors (those are their own atomic units).
inline void collect_editor_fields(
    const ::beebium::Control& editor_root,
    std::unordered_map<std::string, const ::beebium::Control*>& out)
{
    out[editor_root.id()] = &editor_root;
    if (editor_root.control_case() == ::beebium::Control::kGroup) {
        for (const auto& child : editor_root.group().controls()) {
            collect_editor_fields(child, out);
        }
    }
}

inline const char* control_type_name(::beebium::Control::ControlCase c) noexcept {
    switch (c) {
        case ::beebium::Control::kLabel:          return "Label";
        case ::beebium::Control::kIndicator:      return "Indicator";
        case ::beebium::Control::kToggle:         return "Toggle";
        case ::beebium::Control::kButton:         return "Button";
        case ::beebium::Control::kChoice:         return "Choice";
        case ::beebium::Control::kTextInput:      return "TextInput";
        case ::beebium::Control::kGroup:          return "Group";
        case ::beebium::Control::kModalEditor:    return "ModalEditor";
        case ::beebium::Control::kEditableChoice: return "EditableChoice";
        case ::beebium::Control::kEditableList:   return "EditableList";
        case ::beebium::Control::kFileReference:  return "FileReference";
        case ::beebium::Control::CONTROL_NOT_SET: return "(unset)";
    }
    return "(unknown)";
}

// Verify that the Dispatch payload variant matches the addressed
// control's type. Read-only controls (Label, Indicator, Group) are not
// dispatchable. On mismatch, populates `error` and returns false.
inline bool payload_matches_control(
    const ::beebium::DispatchRequest& req,
    ::beebium::Control::ControlCase ctrl_case,
    std::string& error)
{
    using PC = ::beebium::DispatchRequest::PayloadCase;
    using CC = ::beebium::Control::ControlCase;

    PC actual = req.payload_case();
    PC expected = PC::PAYLOAD_NOT_SET;
    bool dispatchable = true;

    switch (ctrl_case) {
        case CC::kToggle:         expected = PC::kBoolValue;     break;
        case CC::kTextInput:      expected = PC::kStringValue;   break;
        case CC::kEditableChoice: expected = PC::kStringValue;   break;
        case CC::kChoice:         expected = PC::kIndexValue;    break;
        case CC::kButton:         expected = PC::PAYLOAD_NOT_SET; break;
        case CC::kModalEditor:    expected = PC::kEditorCommit;  break;
        case CC::kEditableList:   expected = PC::kEditableListEvent; break;
        case CC::kFileReference:  expected = PC::kFileActionId;  break;
        case CC::kLabel:
        case CC::kIndicator:
        case CC::kGroup:
        case CC::CONTROL_NOT_SET:
            dispatchable = false;
            break;
    }

    if (!dispatchable) {
        error = "control '" + req.control_id() + "' (" +
                control_type_name(ctrl_case) +
                ") is not dispatchable";
        return false;
    }
    if (actual != expected) {
        error = "payload type mismatch for control '" + req.control_id() +
                "' (" + control_type_name(ctrl_case) + ")";
        return false;
    }
    return true;
}

// Verify a single EditorFieldValue: its value variant must match the
// addressed sub-control's type. Only input types (Toggle, TextInput,
// Choice) are valid commit targets inside an editor tree.
inline bool editor_field_matches(
    const ::beebium::EditorFieldValue& field,
    ::beebium::Control::ControlCase ctrl_case,
    std::string& error)
{
    using FV = ::beebium::EditorFieldValue::ValueCase;
    using CC = ::beebium::Control::ControlCase;

    FV actual = field.value_case();
    FV expected;
    switch (ctrl_case) {
        case CC::kToggle:         expected = FV::kBoolValue;   break;
        case CC::kTextInput:      expected = FV::kStringValue; break;
        case CC::kEditableChoice: expected = FV::kStringValue; break;
        case CC::kChoice:         expected = FV::kIndexValue;  break;
        default:
            error = "editor field '" + field.field_id() +
                    "' addresses a non-input control (" +
                    control_type_name(ctrl_case) + ")";
            return false;
    }
    if (actual != expected) {
        error = "editor field '" + field.field_id() +
                "' value variant mismatches sub-control type " +
                control_type_name(ctrl_case);
        return false;
    }
    return true;
}

// Validate an EditorCommit against an editor tree: every field_id names a
// sub-control in the tree, and each value variant matches that sub-control's
// type. All-or-nothing. Shared by ModalEditor commits and EditableList
// ADD/EDIT commits (whose editor trees are the list's add_editor and the
// item's editor respectively).
inline bool validate_commit_fields(
    const ::beebium::EditorCommit& commit,
    const ::beebium::Control& editor_root,
    std::string& error)
{
    std::unordered_map<std::string, const ::beebium::Control*> sub_ids;
    collect_editor_fields(editor_root, sub_ids);
    for (const auto& field : commit.fields()) {
        auto it = sub_ids.find(field.field_id());
        if (it == sub_ids.end()) {
            error = "unknown editor field id: " + field.field_id();
            return false;
        }
        if (!editor_field_matches(field, it->second->control_case(), error)) {
            return false;
        }
    }
    return true;
}

// Additional validation for a DispatchRequest whose target is a
// ModalEditor and whose payload is an EditorCommit. Checks (a) the
// ModalEditor is editable, (b) every EditorCommit.field_id names a
// sub-control in the editor tree, (c) each field's value variant
// matches the sub-control's type. All-or-nothing: any failure rejects
// the whole commit.
inline bool validate_editor_commit(
    const ::beebium::DispatchRequest& req,
    const ::beebium::ModalEditor& modal,
    std::string& error)
{
    if (!modal.editable()) {
        error = "ModalEditor '" + req.control_id() + "' is not editable";
        return false;
    }
    return validate_commit_fields(req.editor_commit(), modal.editor(), error);
}

// Additional validation for a DispatchRequest whose target is an
// EditableList and whose payload is an EditableListEvent. Gives
// handle_event the same guarantees ModalEditor commits enjoy:
//   ADD    -- the commit validates against the list's add_editor (the list
//             must allow adding).
//   EDIT   -- item_id names an item, that item is editable, and the commit
//             validates against that item's editor.
//   REMOVE -- item_id names a removable item.
//   ACTION -- item_id names an item and action_id names one of its actions.
// All-or-nothing: any failure rejects the event.
inline bool validate_editable_list_event(
    const ::beebium::DispatchRequest& req,
    const ::beebium::EditableList& list,
    std::string& error)
{
    const auto& event = req.editable_list_event();
    using Kind = ::beebium::EditableListEvent;

    // Locate the addressed item once (not needed for ADD).
    const ::beebium::EditableListItem* item = nullptr;
    if (event.kind() != Kind::ADD) {
        for (const auto& candidate : list.items()) {
            if (candidate.id() == event.item_id()) {
                item = &candidate;
                break;
            }
        }
        if (!item) {
            error = "unknown item id '" + event.item_id() +
                    "' in list '" + req.control_id() + "'";
            return false;
        }
    }

    switch (event.kind()) {
        case Kind::ADD:
            if (!list.can_add()) {
                error = "list '" + req.control_id() + "' does not allow adding";
                return false;
            }
            return validate_commit_fields(event.commit(), list.add_editor(),
                                          error);
        case Kind::EDIT:
            if (!item->editable()) {
                error = "item '" + event.item_id() + "' is not editable";
                return false;
            }
            return validate_commit_fields(event.commit(), item->editor(),
                                          error);
        case Kind::REMOVE:
            if (!item->removable()) {
                error = "item '" + event.item_id() + "' is not removable";
                return false;
            }
            return true;
        case Kind::ACTION:
            for (const auto& action : item->actions()) {
                if (action.id() == event.action_id()) {
                    // An action that opens an editor dispatches ACTION with a
                    // commit, which must validate against that editor.
                    if (action.has_editor()) {
                        return validate_commit_fields(event.commit(),
                                                      action.editor(), error);
                    }
                    return true;
                }
            }
            error = "unknown action id '" + event.action_id() +
                    "' on item '" + event.item_id() + "'";
            return false;
        default:
            error = "unknown EditableListEvent kind";
            return false;
    }
}

// Additional validation for a DispatchRequest whose target is a
// FileReference and whose payload is a file_action_id: the id must name
// one of the server actions the view listed on that FileReference (the
// renderer must never dispatch an action the view did not offer).
inline bool validate_file_reference_action(
    const ::beebium::DispatchRequest& req,
    const ::beebium::FileReference& file,
    std::string& error)
{
    for (const auto& action : file.actions()) {
        if (action.id() == req.file_action_id()) {
            return true;
        }
    }
    error = "unknown file action id '" + req.file_action_id() +
            "' on '" + req.control_id() + "'";
    return false;
}

}  // namespace detail

// All methods (including the destructor) are out-of-line so the class
// is fully anchored in the translation unit that compiles into
// beebium_extension_ui_proto.dll. The handler-class symbols and the
// gRPC service base class then live in the same Windows DLL, which
// avoids the cross-DLL gRPC closure-list corruption documented in
// docs/discussion/grpc-windows-streaming-race.md (gRPC issue #39198).
class ExtensionUiServiceImpl final
    : public ::beebium::ExtensionUiService::Service {
public:
    ExtensionUiServiceImpl(EconetTransportRegistry& transport_registry,
                           ExtensionRegistry& peripheral_registry);
    ~ExtensionUiServiceImpl() override;

    grpc::Status SubscribeView(
        grpc::ServerContext* context,
        const ::beebium::SubscribeViewRequest* request,
        grpc::ServerWriter<::beebium::View>* writer) override;

    grpc::Status Dispatch(
        grpc::ServerContext* context,
        const ::beebium::DispatchRequest* request,
        ::beebium::DispatchResponse* response) override;

private:
    Extension* find_extension(const std::string& name) const;

    EconetTransportRegistry& transport_registry_;
    ExtensionRegistry& peripheral_registry_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_EXTENSION_UI_SERVICE_HPP
