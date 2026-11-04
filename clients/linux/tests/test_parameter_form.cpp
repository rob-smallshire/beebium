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

// An extension's parameters as a form over the draft (client specification
// 12.2): a View the live renderer shows, built from the manifest, the
// draft's values and the server's diagnostics, and the renderer's actions
// applied back to the draft. The real catalogue supplies the manifests.

#include "preset/ParameterFormModel.hpp"

#include "connection/ExtensionDispatch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

const PresetSchema& schema() {
    static const PresetSchema loaded = [] {
        std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                           "describe-preset-schema-model-b.json");
        std::stringstream text;
        text << file.rdbuf();
        return *PresetSchema::parse(text.str());
    }();
    return loaded;
}

ParameterFormModel form_for(const char* name, const char* prefix = "/extensions/0/config") {
    const auto extension = schema().extension(name);
    REQUIRE(extension);
    return ParameterFormModel(*extension, prefix, schema());
}

MachineDraft draft_with(const char* name, ordered_json config) {
    return MachineDraft(ordered_json{{"model", "model-b"},
                                     {"extensions", ordered_json::array({ordered_json{
                                                        {"name", name}, {"config", config}}})}});
}

const beebium::Control& control(const beebium::View& view, const char* id) {
    const beebium::Control* found = find_control(view.root(), id);
    INFO(id);
    REQUIRE(found);
    return *found;
}

}  // namespace

TEST_CASE("Integers and filepaths become text fields; Browse is a local action",
          "[preset][form]") {
    const auto form = form_for("scsi-hard-disc");
    MachineDraft draft = draft_with("scsi-hard-disc", {{"scsi-id", 0}, {"image", ""}});
    const beebium::View view = form.view(draft, {});
    const auto& id = control(view, "param:scsi-id");
    REQUIRE(id.has_text_input());
    CHECK(id.text_input().label() == "SCSI ID");
    CHECK(id.text_input().value() == "0");
    CHECK(id.text_input().placeholder() == "0–7");
    const auto& image = control(view, "param:image");
    CHECK(image.text_input().label() == "Disc image");
    const auto& browse = control(view, "browse:image");
    CHECK(browse.button().label() == "Browse...");

    // A typed number is written as a number; text that is not one is
    // written as it is, for the server to judge.
    CHECK_FALSE(form.apply(text_action(view, "param:scsi-id", "3"), draft).browse);
    CHECK(draft.get("/extensions/0/config/scsi-id") == ordered_json(3));
    form.apply(text_action(view, "param:scsi-id", "three"), draft);
    CHECK(draft.get("/extensions/0/config/scsi-id") == ordered_json("three"));

    const auto outcome = form.apply(button_action(view, "browse:image"), draft);
    REQUIRE(outcome.browse);
    CHECK(outcome.browse->key == "image");
    CHECK(outcome.browse->chooser == FormBrowse::Chooser::open_file);
    form.set_path(draft, "image", "/discs/scsi0.dat");
    CHECK(draft.get("/extensions/0/config/image") == ordered_json("/discs/scsi0.dat"));
}

TEST_CASE("A write filepath browses to save, a dirpath to a directory", "[preset][form]") {
    {
        const auto form = form_for("file-printer");
        MachineDraft draft = draft_with("file-printer", {{"path", ""}});
        const auto outcome = form.apply(button_action(form.view(draft, {}), "browse:path"), draft);
        REQUIRE(outcome.browse);
        CHECK(outcome.browse->chooser == FormBrowse::Chooser::save_file);
    }
    {
        const auto form = form_for("job-file-printer");
        MachineDraft draft = draft_with("job-file-printer", {{"directory", ""}});
        const auto outcome =
            form.apply(button_action(form.view(draft, {}), "browse:directory"), draft);
        REQUIRE(outcome.browse);
        CHECK(outcome.browse->chooser == FormBrowse::Chooser::directory);
    }
}

TEST_CASE("Serial port parameters offer the host's ports, and a bundled ROM its images",
          "[preset][form]") {
    auto form = form_for("host-serial");
    form.set_serial_ports({"/dev/ttyUSB0", "/dev/ttyS0"});
    MachineDraft draft = draft_with("host-serial", {{"path", ""}});
    const beebium::View serial_view = form.view(draft, {});
    const auto& path = control(serial_view, "param:path");
    REQUIRE(path.has_editable_choice());
    CHECK(path.editable_choice().options_size() == 2);
    CHECK(path.editable_choice().options(0) == "/dev/ttyUSB0");

    const auto copro = form_for("acorn-65c02-coprocessor");
    MachineDraft copro_draft = draft_with("acorn-65c02-coprocessor", {{"rom", ""}});
    const beebium::View copro_view = copro.view(copro_draft, {});
    const auto& rom = control(copro_view, "param:rom");
    REQUIRE(rom.has_editable_choice());
    // This extension's bundled ROM, by name.
    REQUIRE(rom.editable_choice().options_size() == 1);
    CHECK(rom.editable_choice().options(0) == "acorn-tube-6502_1_10.rom");
}

TEST_CASE("A group of alternatives is one choice, showing only the chosen one's field",
          "[preset][form][group]") {
    const auto form = form_for("acorn-rtc");
    MachineDraft draft = draft_with("acorn-rtc", {{"time", ""}, {"offset", ""}, {"layout", "7bit-year-in-r7"}});
    beebium::View view = form.view(draft, {});
    const auto& start = control(view, "group:start");
    REQUIRE(start.has_choice());
    CHECK(start.choice().label() == "Clock start");
    REQUIRE(start.choice().options_size() == 3);
    CHECK(start.choice().options(2) == "Host time");
    CHECK(start.choice().selected_index() == 2u);  // neither given
    CHECK(find_control(view.root(), "param:time") == nullptr);
    CHECK(find_control(view.root(), "param:offset") == nullptr);

    form.apply(choice_action(view, "group:start", 1), draft);  // offset
    view = form.view(draft, {});
    CHECK(control(view, "group:start").choice().selected_index() == 1u);
    CHECK(find_control(view.root(), "param:offset") != nullptr);
    CHECK(find_control(view.root(), "param:time") == nullptr);
    form.apply(text_action(view, "param:offset", "-10y"), draft);
    CHECK(draft.get("/extensions/0/config/offset") == ordered_json("-10y"));
    // Back to none: the alternatives are cleared.
    form.apply(choice_action(view, "group:start", 2), draft);
    CHECK(draft.get("/extensions/0/config/offset") == ordered_json(""));
}

TEST_CASE("A field excluded by another that is set is shown read-only, saying why",
          "[preset][form][excludes]") {
    const auto form = form_for("tcp-printer");
    MachineDraft draft = draft_with("tcp-printer", {{"url", "socket://192.0.2.5:9100"},
                                                    {"host", ""}, {"port", ""}});
    const beebium::View view = form.view(draft, {});
    CHECK(control(view, "param:url").has_text_input());
    const auto& host = control(view, "param:host");
    REQUIRE(host.has_label());
    CHECK(host.label().text() == "Printer host");
    CHECK(host.label().secondary_text() == "Not used while Printer URL is set");
}

TEST_CASE("Structured and plain lists edit item by item, written as objects or strings",
          "[preset][form][list]") {
    const auto form = form_for("aun", "/econet/transport/parameters");
    MachineDraft draft(ordered_json{{"model", "model-b"},
                                    {"econet", {{"station", 254},
                                                {"transport", {{"name", "aun"},
                                                               {"parameters", {{"map", ordered_json::array()},
                                                                               {"subnet", ordered_json::array()},
                                                                               {"map-file", ""}}}}}}}});
    beebium::View view = form.view(draft, {});
    const auto& map = control(view, "param:map");
    REQUIRE(map.has_editable_list());
    CHECK(map.editable_list().title() == "Peers");
    CHECK(map.editable_list().can_add());
    // The map file shows its default.
    CHECK(control(view, "param:map-file").text_input().placeholder() ==
          "/home/user/.config/beebium/aun-map.json");

    const EditorValues added{{"net", std::string("0")}, {"station", std::string("254")},
                             {"host", std::string("192.0.2.7")}, {"port", std::string("32768")}};
    form.apply(list_action(view, "param:map", beebium::EditableListEvent::ADD, "", "", &added), draft);
    const auto written = draft.get("/econet/transport/parameters/map");
    REQUIRE(written);
    REQUIRE(written->size() == 1);
    CHECK((*written)[0]["station"] == 254);
    CHECK((*written)[0]["host"] == "192.0.2.7");
    view = form.view(draft, {});
    REQUIRE(control(view, "param:map").editable_list().items_size() == 1);
    CHECK(control(view, "param:map").editable_list().items(0).primary() == "0.254@192.0.2.7@32768");

    const EditorValues edited{{"net", std::string("0")}, {"station", std::string("1")},
                              {"host", std::string("192.0.2.8")}, {"port", std::string("32768")}};
    form.apply(list_action(view, "param:map", beebium::EditableListEvent::EDIT, "0", "", &edited),
               draft);
    CHECK((*draft.get("/econet/transport/parameters/map"))[0]["station"] == 1);
    form.apply(list_action(view, "param:map", beebium::EditableListEvent::REMOVE, "0", "", nullptr),
               draft);
    CHECK(draft.get("/econet/transport/parameters/map")->empty());
}

TEST_CASE("The server's diagnostics show at their parameter, verbatim", "[preset][form][diagnostics]") {
    const auto form = form_for("scsi-hard-disc");
    MachineDraft draft = draft_with("scsi-hard-disc", {{"scsi-id", 9}, {"image", ""}});
    PresetDiagnostic error;
    error.path = "/extensions/0/config/scsi-id";
    error.message = "parameter 'scsi-id' must be between 0 and 7, got 9";
    PresetDiagnostic elsewhere;
    elsewhere.path = "/extensions/1/config/scsi-id";
    elsewhere.message = "not this one";
    const beebium::View view = form.view(draft, {error, elsewhere});
    const auto& note = control(view, "diagnostic:scsi-id");
    REQUIRE(note.has_indicator());
    CHECK(note.indicator().state() == beebium::Indicator::ERROR);
    CHECK(note.indicator().text() == "parameter 'scsi-id' must be between 0 and 7, got 9");
    CHECK(find_control(view.root(), "diagnostic:image") == nullptr);
}

TEST_CASE("Booleans are toggles and choices are pickers, written as values", "[preset][form]") {
    SchemaExtension made;
    made.name = "made-up";
    SchemaParameter flag;
    flag.key = "enabled";
    flag.label = "Enabled";
    flag.type = "boolean";
    flag.effective_default = "true";
    SchemaParameter mode;
    mode.key = "mode";
    mode.label = "Mode";
    mode.type = "string";
    mode.choices = {{"fast", "Fast", ""}, {"slow", "Slow", ""}};
    mode.effective_default = "slow";
    made.parameters = {flag, mode};
    const ParameterFormModel form(made, "/extensions/0/config", schema());
    MachineDraft draft = draft_with("made-up", ordered_json::object());
    beebium::View view = form.view(draft, {});
    CHECK(control(view, "param:enabled").toggle().value());  // the default
    CHECK(control(view, "param:mode").choice().selected_index() == 1u);
    form.apply(toggle_action(view, "param:enabled", false), draft);
    CHECK(draft.get("/extensions/0/config/enabled") == ordered_json(false));
    form.apply(choice_action(view, "param:mode", 0), draft);
    CHECK(draft.get("/extensions/0/config/mode") == ordered_json("fast"));
}
