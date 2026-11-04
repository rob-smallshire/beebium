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

// Actions on an extension's view (client specification 12.1): one Dispatch
// per action, its payload typed by the control, and always the revision of
// the view the user was looking at.

#include "connection/ExtensionDispatch.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

beebium::View view_with(std::uint64_t revision) {
    beebium::View view;
    view.set_extension_id("ext-1");
    view.set_view_revision(revision);
    auto* root = view.mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();
    auto* toggle = group->add_controls();
    toggle->set_id("enabled");
    toggle->mutable_toggle()->set_label("Enabled");
    auto* editor = group->add_controls();
    editor->set_id("station");
    auto* modal = editor->mutable_modal_editor();
    modal->mutable_anchor()->set_id("station-anchor");
    modal->mutable_anchor()->mutable_label()->set_text("1.254");
    auto* fields = modal->mutable_editor()->mutable_group();
    auto* net = fields->add_controls();
    net->set_id("net");
    net->mutable_text_input()->set_value("1");
    auto* kind = fields->add_controls();
    kind->set_id("kind");
    kind->mutable_choice()->add_options("a");
    kind->mutable_choice()->add_options("b");
    kind->mutable_choice()->set_selected_index(1);
    auto* on = fields->add_controls();
    on->set_id("on");
    on->mutable_toggle()->set_value(true);
    return view;
}

}  // namespace

TEST_CASE("Every action names its extension and control and echoes the view's revision",
          "[extension][dispatch]") {
    const beebium::View view = view_with(42);
    const auto toggle = toggle_action(view, "enabled", true);
    CHECK(toggle.extension_id() == "ext-1");
    CHECK(toggle.control_id() == "enabled");
    CHECK(toggle.view_revision() == 42);
    CHECK(toggle.bool_value());

    CHECK(text_action(view, "name", "Elite").string_value() == "Elite");
    CHECK(choice_action(view, "mode", 2).index_value() == 2u);
    const auto button = button_action(view, "connect");
    CHECK(button.payload_case() == beebium::DispatchRequest::PAYLOAD_NOT_SET);
    CHECK(button.view_revision() == 42);
    CHECK(file_action(view, "map", "reload").file_action_id() == "reload");
}

TEST_CASE("An editor's starting values are its leaves' values, by field id",
          "[extension][dispatch]") {
    const beebium::View view = view_with(7);
    const beebium::Control* editor = find_control(view.root(), "station");
    REQUIRE(editor);
    EditorValues values = editor_values(editor->modal_editor().editor());
    REQUIRE(values.size() == 3);
    CHECK(std::get<std::string>(values.at("net")) == "1");
    CHECK(std::get<std::uint32_t>(values.at("kind")) == 1u);
    CHECK(std::get<bool>(values.at("on")));

    values["net"] = std::string("2");
    const auto commit = editor_action(view, "station", values);
    CHECK(commit.view_revision() == 7);
    REQUIRE(commit.editor_commit().fields_size() == 3);
    bool saw_net = false;
    for (const auto& field : commit.editor_commit().fields()) {
        if (field.field_id() == "net") {
            saw_net = true;
            CHECK(field.string_value() == "2");
        }
        if (field.field_id() == "kind") {
            CHECK(field.index_value() == 1u);
        }
    }
    CHECK(saw_net);
}

TEST_CASE("Editable list events carry the item, action and commit the kind needs",
          "[extension][dispatch]") {
    const beebium::View view = view_with(3);
    const EditorValues commit{{"host", std::string("192.0.2.10")}};
    const auto add = list_action(view, "peers", beebium::EditableListEvent::ADD, "", "", &commit);
    CHECK(add.editable_list_event().kind() == beebium::EditableListEvent::ADD);
    CHECK(add.editable_list_event().item_id().empty());
    CHECK(add.editable_list_event().commit().fields_size() == 1);
    const auto remove =
        list_action(view, "peers", beebium::EditableListEvent::REMOVE, "peer-2", "", nullptr);
    CHECK(remove.editable_list_event().item_id() == "peer-2");
    CHECK_FALSE(remove.editable_list_event().has_commit());
    const auto action = list_action(view, "peers", beebium::EditableListEvent::ACTION, "peer-2",
                                    "save", nullptr);
    CHECK(action.editable_list_event().action_id() == "save");
    CHECK(action.view_revision() == 3);
}

TEST_CASE("Controls are found by id anywhere in the tree", "[extension][dispatch]") {
    const beebium::View view = view_with(1);
    CHECK(find_control(view.root(), "root") != nullptr);
    CHECK(find_control(view.root(), "enabled")->has_toggle());
    CHECK(find_control(view.root(), "station-anchor")->has_label());
    CHECK(find_control(view.root(), "kind")->has_choice());
    CHECK(find_control(view.root(), "nowhere") == nullptr);
}
