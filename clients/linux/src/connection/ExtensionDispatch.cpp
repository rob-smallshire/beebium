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

#include "connection/ExtensionDispatch.hpp"

namespace beebium::linux_client {

namespace {

beebium::DispatchRequest addressed(const beebium::View& view, const std::string& control_id) {
    beebium::DispatchRequest request;
    request.set_extension_id(view.extension_id());
    request.set_control_id(control_id);
    request.set_view_revision(view.view_revision());
    return request;
}

void fill_commit(beebium::EditorCommit* commit, const EditorValues& values) {
    for (const auto& [field_id, value] : values) {
        auto* field = commit->add_fields();
        field->set_field_id(field_id);
        if (const auto* flag = std::get_if<bool>(&value)) {
            field->set_bool_value(*flag);
        } else if (const auto* text = std::get_if<std::string>(&value)) {
            field->set_string_value(*text);
        } else {
            field->set_index_value(std::get<std::uint32_t>(value));
        }
    }
}

void collect(const beebium::Control& control, EditorValues& values) {
    switch (control.control_case()) {
    case beebium::Control::kToggle: values[control.id()] = control.toggle().value(); break;
    case beebium::Control::kTextInput:
        values[control.id()] = control.text_input().value();
        break;
    case beebium::Control::kEditableChoice:
        values[control.id()] = control.editable_choice().value();
        break;
    case beebium::Control::kChoice:
        values[control.id()] = control.choice().selected_index();
        break;
    case beebium::Control::kGroup:
        for (const auto& child : control.group().controls()) {
            collect(child, values);
        }
        break;
    default: break;
    }
}

}  // namespace

const beebium::Control* find_control(const beebium::Control& root, const std::string& id) {
    if (root.id() == id) {
        return &root;
    }
    const auto search = [&](const beebium::Control& child) { return find_control(child, id); };
    switch (root.control_case()) {
    case beebium::Control::kGroup:
        for (const auto& child : root.group().controls()) {
            if (const auto* found = search(child)) {
                return found;
            }
        }
        break;
    case beebium::Control::kModalEditor:
        if (const auto* found = search(root.modal_editor().anchor())) {
            return found;
        }
        return search(root.modal_editor().editor());
    default: break;
    }
    return nullptr;
}

EditorValues editor_values(const beebium::Control& editor) {
    EditorValues values;
    collect(editor, values);
    return values;
}

beebium::DispatchRequest toggle_action(const beebium::View& view, const std::string& control_id,
                                       bool value) {
    auto request = addressed(view, control_id);
    request.set_bool_value(value);
    return request;
}

beebium::DispatchRequest text_action(const beebium::View& view, const std::string& control_id,
                                     const std::string& value) {
    auto request = addressed(view, control_id);
    request.set_string_value(value);
    return request;
}

beebium::DispatchRequest choice_action(const beebium::View& view, const std::string& control_id,
                                       std::uint32_t index) {
    auto request = addressed(view, control_id);
    request.set_index_value(index);
    return request;
}

beebium::DispatchRequest button_action(const beebium::View& view, const std::string& control_id) {
    return addressed(view, control_id);
}

beebium::DispatchRequest file_action(const beebium::View& view, const std::string& control_id,
                                     const std::string& action_id) {
    auto request = addressed(view, control_id);
    request.set_file_action_id(action_id);
    return request;
}

beebium::DispatchRequest editor_action(const beebium::View& view, const std::string& control_id,
                                       const EditorValues& values) {
    auto request = addressed(view, control_id);
    fill_commit(request.mutable_editor_commit(), values);
    return request;
}

beebium::DispatchRequest list_action(const beebium::View& view, const std::string& control_id,
                                     beebium::EditableListEvent::Kind kind,
                                     const std::string& item_id, const std::string& action_id,
                                     const EditorValues* commit) {
    auto request = addressed(view, control_id);
    auto* event = request.mutable_editable_list_event();
    event->set_kind(kind);
    event->set_item_id(item_id);
    event->set_action_id(action_id);
    if (commit) {
        fill_commit(event->mutable_commit(), *commit);
    }
    return request;
}

}  // namespace beebium::linux_client
