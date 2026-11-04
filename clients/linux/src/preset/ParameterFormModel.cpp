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

#include "preset/ParameterFormModel.hpp"

#include "preset/StructuredItem.hpp"

#include <algorithm>
#include <charconv>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const std::string PARAM = "param:";
const std::string BROWSE = "browse:";
const std::string GROUP = "group:";
const std::string DIAGNOSTIC = "diagnostic:";

std::string escaped(const std::string& key) {
    std::string out;
    for (const char c : key) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

// A value as a field shows it: a string as is, anything else as JSON.
std::string as_text(const std::optional<ordered_json>& value) {
    if (!value) {
        return {};
    }
    return value->is_string() ? value->get<std::string>() : value->dump();
}

bool empty_value(const std::optional<ordered_json>& value) {
    return !value || (value->is_string() && value->get<std::string>().empty()) ||
           (value->is_array() && value->empty());
}

std::string range(const SchemaParameter& p) {
    if (p.min && p.max) {
        return std::to_string(*p.min) + "–" + std::to_string(*p.max);
    }
    return p.effective_default;
}

beebium::Control* add(beebium::Control* group, const std::string& id) {
    beebium::Control* control = group->mutable_group()->add_controls();
    control->set_id(id);
    return control;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

ParameterFormModel::ParameterFormModel(SchemaExtension extension, std::string prefix,
                                       const PresetSchema& schema)
    : extension_(std::move(extension)), prefix_(std::move(prefix)) {
    for (const auto& p : extension_.parameters) {
        if (p.bundled.empty()) {
            continue;
        }
        for (const auto& image : schema.bundled_images(p.bundled)) {
            if (image.extension.empty() || image.extension == extension_.name) {
                bundled_[p.key].push_back(image.image);
            }
        }
    }
}

const SchemaParameter* ParameterFormModel::parameter(const std::string& key) const {
    for (const auto& p : extension_.parameters) {
        if (p.key == key) {
            return &p;
        }
    }
    return nullptr;
}

std::string ParameterFormModel::pointer(const std::string& key) const {
    return prefix_ + "/" + escaped(key);
}

std::optional<ordered_json> ParameterFormModel::value(const MachineDraft& draft,
                                                      const std::string& key) const {
    return draft.get(pointer(key));
}

bool ParameterFormModel::given(const MachineDraft& draft, const std::string& key) const {
    return !empty_value(value(draft, key));
}

beebium::View ParameterFormModel::view(const MachineDraft& draft,
                                       const std::vector<PresetDiagnostic>& diagnostics) const {
    beebium::View view;
    view.set_extension_id(extension_.name);
    beebium::Control* root = view.mutable_root();
    root->set_id("parameters");
    root->mutable_group();

    std::vector<std::string> in_groups;
    for (const auto& group : extension_.groups) {
        in_groups.insert(in_groups.end(), group.parameters.begin(), group.parameters.end());
    }
    std::vector<std::string> shown_groups;
    for (const auto& p : extension_.parameters) {
        const auto owner = std::find_if(extension_.groups.begin(), extension_.groups.end(),
                                        [&](const SchemaParameterGroup& g) {
                                            return std::find(g.parameters.begin(),
                                                             g.parameters.end(),
                                                             p.key) != g.parameters.end();
                                        });
        if (owner == extension_.groups.end()) {
            add_parameter(root, p, draft, diagnostics);
            continue;
        }
        if (std::find(shown_groups.begin(), shown_groups.end(), owner->key) != shown_groups.end()) {
            continue;
        }
        shown_groups.push_back(owner->key);
        // One choice among the alternatives, then the chosen one's field.
        auto* choice = add(root, GROUP + owner->key);
        choice->set_tooltip(owner->description);
        auto* picker = choice->mutable_choice();
        picker->set_label(owner->label);
        std::size_t selected = owner->parameters.size();
        for (std::size_t i = 0; i < owner->parameters.size(); ++i) {
            const SchemaParameter* member = parameter(owner->parameters[i]);
            picker->add_options(member ? member->label : owner->parameters[i]);
            if (selected == owner->parameters.size() && given(draft, owner->parameters[i])) {
                selected = i;
            }
        }
        if (selected == owner->parameters.size()) {
            if (const auto found = chosen_.find(owner->key); found != chosen_.end()) {
                const auto at = std::find(owner->parameters.begin(), owner->parameters.end(),
                                          found->second);
                selected = static_cast<std::size_t>(at - owner->parameters.begin());
            }
        }
        picker->add_options(owner->none_label.empty() ? "None" : owner->none_label);
        picker->set_selected_index(static_cast<std::uint32_t>(selected));
        if (selected < owner->parameters.size()) {
            if (const SchemaParameter* member = parameter(owner->parameters[selected])) {
                add_parameter(root, *member, draft, diagnostics);
            }
        }
    }
    return view;
}

void ParameterFormModel::add_parameter(beebium::Control* group, const SchemaParameter& p,
                                       const MachineDraft& draft,
                                       const std::vector<PresetDiagnostic>& diagnostics) const {
    const std::string id = PARAM + p.key;
    const auto current = value(draft, p.key);

    // Excluded by another parameter that is given: read-only, saying why.
    for (const auto& other : p.excludes) {
        if (given(draft, other)) {
            const SchemaParameter* by = parameter(other);
            auto* label = add(group, id)->mutable_label();
            label->set_text(p.label);
            label->set_secondary_text("Not used while " + (by ? by->label : other) + " is set");
            return;
        }
    }

    beebium::Control* control = add(group, id);
    control->set_tooltip(p.description);
    if (p.is_list) {
        auto* list = control->mutable_editable_list();
        list->set_title(p.label);
        list->set_help(p.description);
        list->set_can_add(true);
        const auto format = ItemFormat::parse(p.item_format);
        const bool structured = format && !p.fields.empty();
        auto make_editor = [&](beebium::Control* editor, const ItemStrings& strings) {
            editor->set_id(id + ":editor");
            auto* fields = editor->mutable_group();
            if (structured) {
                for (const auto& f : p.fields) {
                    auto* field = fields->add_controls();
                    field->set_id(f.key);
                    auto* input = field->mutable_text_input();
                    input->set_label(f.label);
                    input->set_help(f.description);
                    const auto found = strings.find(f.key);
                    input->set_value(found == strings.end() ? std::string() : found->second);
                    if (f.min && f.max) {
                        input->set_placeholder(std::to_string(*f.min) + "–" +
                                               std::to_string(*f.max));
                    } else {
                        input->set_placeholder(f.default_value);
                    }
                }
            } else {
                auto* field = fields->add_controls();
                field->set_id("value");
                auto* input = field->mutable_text_input();
                input->set_label(p.label);
                const auto found = strings.find("value");
                input->set_value(found == strings.end() ? std::string() : found->second);
            }
        };
        make_editor(list->mutable_add_editor(), {});
        if (current && current->is_array()) {
            for (std::size_t i = 0; i < current->size(); ++i) {
                const ordered_json& item = (*current)[i];
                auto* row = list->add_items();
                row->set_id(std::to_string(i));
                row->set_editable(true);
                row->set_removable(true);
                ItemStrings strings;
                if (structured && item.is_object()) {
                    strings = item_strings(p.fields, item);
                    row->set_primary(format->line(strings));
                } else {
                    strings["value"] = item.is_string() ? item.get<std::string>() : item.dump();
                    row->set_primary(strings["value"]);
                }
                make_editor(row->mutable_editor(), strings);
                // The server's word on this item, verbatim.
                const std::string item_pointer = pointer(p.key) + "/" + std::to_string(i);
                for (const auto& d : diagnostics) {
                    if (d.path == item_pointer || starts_with(d.path, item_pointer + "/")) {
                        row->set_note(d.message);
                        row->set_state(d.error() ? beebium::Indicator::ERROR
                                                 : beebium::Indicator::WARN);
                    }
                }
            }
        }
    } else if (!p.choices.empty()) {
        auto* choice = control->mutable_choice();
        choice->set_label(p.label);
        const std::string now = empty_value(current) ? p.effective_default : as_text(current);
        for (std::size_t i = 0; i < p.choices.size(); ++i) {
            choice->add_options(p.choices[i].label.empty() ? p.choices[i].value
                                                           : p.choices[i].label);
            if (p.choices[i].value == now) {
                choice->set_selected_index(static_cast<std::uint32_t>(i));
            }
        }
    } else if (p.type == "boolean") {
        auto* toggle = control->mutable_toggle();
        toggle->set_label(p.label);
        toggle->set_value(current ? (current->is_boolean() ? current->get<bool>()
                                                           : as_text(current) == "true")
                                  : p.effective_default == "true");
    } else if (p.choices_from == "serial-ports" || bundled_.count(p.key)) {
        auto* choice = control->mutable_editable_choice();
        choice->set_label(p.label);
        choice->set_value(as_text(current));
        choice->set_placeholder(p.effective_default);
        const auto& options = p.choices_from == "serial-ports" ? serial_ports_ : bundled_.at(p.key);
        for (const auto& option : options) {
            choice->add_options(option);
        }
    } else {
        auto* input = control->mutable_text_input();
        input->set_label(p.label);
        input->set_value(as_text(current));
        input->set_placeholder(p.type == "integer" ? range(p) : p.effective_default);
        input->set_help(p.description);
    }
    if (p.type == "filepath" || p.type == "dirpath") {
        auto* browse = add(group, BROWSE + p.key)->mutable_button();
        browse->set_label("Browse...");
        browse->set_enabled(true);
    }
    // The server's word on this parameter, verbatim, beneath it.
    const std::string here = pointer(p.key);
    for (const auto& d : diagnostics) {
        if (d.path == here || (!p.is_list && starts_with(d.path, here + "/"))) {
            auto* indicator = add(group, DIAGNOSTIC + p.key)->mutable_indicator();
            indicator->set_state(d.error() ? beebium::Indicator::ERROR : beebium::Indicator::WARN);
            indicator->set_text(d.message);
        }
    }
}

FormOutcome ParameterFormModel::apply(const beebium::DispatchRequest& request,
                                      MachineDraft& draft) const {
    FormOutcome outcome;
    const std::string& id = request.control_id();
    if (starts_with(id, BROWSE)) {
        const std::string key = id.substr(BROWSE.size());
        if (const SchemaParameter* p = parameter(key)) {
            FormBrowse browse;
            browse.key = key;
            browse.label = p->label;
            browse.current = as_text(value(draft, key));
            browse.chooser = p->type == "dirpath"   ? FormBrowse::Chooser::directory
                             : p->access == "write" ? FormBrowse::Chooser::save_file
                                                    : FormBrowse::Chooser::open_file;
            outcome.browse = browse;
        }
        return outcome;
    }
    if (starts_with(id, GROUP)) {
        const std::string key = id.substr(GROUP.size());
        for (const auto& group : extension_.groups) {
            if (group.key != key) {
                continue;
            }
            const std::size_t index = request.index_value();
            // Only the chosen alternative keeps a value.
            for (std::size_t i = 0; i < group.parameters.size(); ++i) {
                if (i != index && given(draft, group.parameters[i])) {
                    draft.set(pointer(group.parameters[i]), "");
                    outcome.edited = true;
                }
            }
            if (index < group.parameters.size()) {
                chosen_[key] = group.parameters[index];
            } else {
                chosen_.erase(key);
            }
        }
        return outcome;
    }
    if (!starts_with(id, PARAM)) {
        return outcome;
    }
    const std::string key = id.substr(PARAM.size());
    const SchemaParameter* p = parameter(key);
    if (!p) {
        return outcome;
    }
    outcome.edited = true;
    switch (request.payload_case()) {
    case beebium::DispatchRequest::kBoolValue:
        draft.set(pointer(key), request.bool_value());
        break;
    case beebium::DispatchRequest::kIndexValue:
        if (request.index_value() < p->choices.size()) {
            draft.set(pointer(key), p->choices[request.index_value()].value);
        }
        break;
    case beebium::DispatchRequest::kStringValue: {
        const std::string& text = request.string_value();
        if (p->type == "integer") {
            long long number = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
            if (text.empty()) {
                draft.unset(pointer(key));  // the server fills the default
            } else if (error == std::errc() && end == text.data() + text.size()) {
                draft.set(pointer(key), number);
            } else {
                draft.set(pointer(key), text);  // the server will say what is wrong
            }
        } else {
            draft.set(pointer(key), text);
        }
        break;
    }
    case beebium::DispatchRequest::kEditableListEvent: {
        const auto& event = request.editable_list_event();
        ordered_json list = value(draft, key).value_or(ordered_json::array());
        if (!list.is_array()) {
            list = ordered_json::array();
        }
        const auto format = ItemFormat::parse(p->item_format);
        const bool structured = format && !p->fields.empty();
        const auto item_from = [&](const beebium::EditorCommit& commit) {
            ItemStrings strings;
            for (const auto& field : commit.fields()) {
                strings[field.field_id()] = field.string_value();
            }
            return structured ? item_object(p->fields, strings)
                              : ordered_json(strings["value"]);
        };
        const std::size_t index = event.item_id().empty() ? 0 : std::stoul(event.item_id());
        switch (event.kind()) {
        case beebium::EditableListEvent::ADD: list.push_back(item_from(event.commit())); break;
        case beebium::EditableListEvent::EDIT:
            if (index < list.size()) {
                list[index] = item_from(event.commit());
            }
            break;
        case beebium::EditableListEvent::REMOVE:
            if (index < list.size()) {
                list.erase(index);
            }
            break;
        default: break;
        }
        draft.set(pointer(key), list);
        break;
    }
    default: outcome.edited = false; break;
    }
    return outcome;
}

void ParameterFormModel::set_path(MachineDraft& draft, const std::string& key,
                                  const std::string& path) const {
    draft.set(pointer(key), path);
}

}  // namespace beebium::linux_client
