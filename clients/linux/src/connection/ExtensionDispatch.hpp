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

#ifndef BEEBIUM_LINUX_CONNECTION_EXTENSION_DISPATCH_HPP
#define BEEBIUM_LINUX_CONNECTION_EXTENSION_DISPATCH_HPP

#include "extension_ui.pb.h"

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace beebium::linux_client {

// The user's actions on an extension's view (client specification 12.1),
// as DispatchRequests: the payload typed by the control, and always the
// revision of the view the user acted on, so the server can refuse an
// action on a control it has since replaced. Nothing here changes any
// state: the result arrives as the next view.

// A leaf's value inside an editor: a TextInput's or EditableChoice's text,
// a Choice's index, a Toggle's state.
using EditorValue = std::variant<bool, std::string, std::uint32_t>;
using EditorValues = std::map<std::string, EditorValue>;

// The control with this id in a tree, or null.
const beebium::Control* find_control(const beebium::Control& root, const std::string& id);

// An editor tree's leaves and their current values, keyed by control id:
// where an editor starts, and what its commit sends.
EditorValues editor_values(const beebium::Control& editor);

beebium::DispatchRequest toggle_action(const beebium::View& view, const std::string& control_id,
                                       bool value);
beebium::DispatchRequest text_action(const beebium::View& view, const std::string& control_id,
                                     const std::string& value);
beebium::DispatchRequest choice_action(const beebium::View& view, const std::string& control_id,
                                       std::uint32_t index);
beebium::DispatchRequest button_action(const beebium::View& view, const std::string& control_id);
beebium::DispatchRequest file_action(const beebium::View& view, const std::string& control_id,
                                     const std::string& action_id);
// One atomic commit of a ModalEditor's fields.
beebium::DispatchRequest editor_action(const beebium::View& view, const std::string& control_id,
                                       const EditorValues& values);
// An EditableList event: ADD with a commit; EDIT with the item and a
// commit; REMOVE with the item; ACTION with the item, the action and,
// when the action has an editor, a commit.
beebium::DispatchRequest list_action(const beebium::View& view, const std::string& control_id,
                                     beebium::EditableListEvent::Kind kind,
                                     const std::string& item_id, const std::string& action_id,
                                     const EditorValues* commit);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_EXTENSION_DISPATCH_HPP
