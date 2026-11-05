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

// The machine draft (client specification 5.2), as the macOS client holds
// it: the resolved document edited in place, each section's change judged
// against the document as resolved, the whole of it written for a launch.

#include "preset/MachineDraft.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

ordered_json resolved() {
    return ordered_json::parse(R"({
      "name": "BBC Model B with DFS",
      "model": "model-b",
      "description": null,
      "speed": 1.0,
      "startup": {"screen_mode": 7, "auto_boot": false},
      "storage": {"fdc_socket": {"id": "acorn-8271"},
                  "floppy_drives": [{"drive": 0, "image_uri": null},
                                    {"drive": 1, "image_uri": null}]},
      "sideways_bank": {"slots": [{"slot": 15, "type": "rom", "image_uri": "file:///roms/basic.rom"}]},
      "econet": null,
      "extensions": [],
      "future_key": {"kept": true}
    })");
}

}  // namespace

TEST_CASE("An untouched draft is unchanged everywhere and serialises whole", "[preset][draft]") {
    const MachineDraft draft(resolved());
    CHECK_FALSE(draft.changed());
    CHECK_FALSE(draft.changed("/storage"));
    // The whole document, nulls and unknown keys included, as the server writes it.
    CHECK(draft.serialise() == resolved().dump(2) + "\n");
}

TEST_CASE("An edit changes its section only, and reverting restores it", "[preset][draft]") {
    MachineDraft draft(resolved());
    draft.set("/storage/floppy_drives/0/image_uri", "file:///discs/elite.ssd");
    CHECK(draft.changed());
    CHECK(draft.changed("/storage"));
    CHECK_FALSE(draft.changed("/sideways_bank"));
    CHECK(draft.get("/storage/floppy_drives/0/image_uri") == ordered_json("file:///discs/elite.ssd"));
    // Back to what was resolved: unchanged again, explicitly written or not.
    draft.set("/storage/floppy_drives/0/image_uri", nullptr);
    CHECK_FALSE(draft.changed("/storage"));
    draft.set("/speed", 4.0);
    CHECK(draft.changed("/speed"));
    draft.revert("/speed");
    CHECK_FALSE(draft.changed());
}

TEST_CASE("A value equal to the resolved one is written explicitly, not dropped",
          "[preset][draft]") {
    MachineDraft draft(resolved());
    draft.set("/econet", ordered_json::parse(R"({"station": 254, "transport": null})"));
    CHECK(draft.changed("/econet"));
    draft.set("/sideways_bank/slots/0/write_protected", false);
    // Written as chosen, even where it matches the default.
    CHECK(draft.json()["sideways_bank"]["slots"][0].contains("write_protected"));
    CHECK(draft.json()["future_key"]["kept"] == true);
}

TEST_CASE("Null and absent are the same to a section's change", "[preset][draft]") {
    MachineDraft draft(resolved());
    draft.unset("/description");
    CHECK_FALSE(draft.changed("/description"));
    CHECK_FALSE(draft.changed());
    draft.set("/description", "A machine for Elite");
    CHECK(draft.changed("/description"));
    draft.revert("");
    CHECK_FALSE(draft.changed());
    CHECK(draft.json() == resolved());
}

TEST_CASE("Part of an array is judged changed by the items kept", "[preset][draft]") {
    MachineDraft draft(ordered_json::parse(R"({"model": "model-b", "extensions": [
        {"name": "acorn-65c02-coprocessor"}, {"name": "acorn-rtc"}]})"));
    const auto tube = [](const ordered_json& item) {
        return item.value("name", std::string()).find("coprocessor") != std::string::npos;
    };
    const auto others = [&](const ordered_json& item) { return !tube(item); };
    draft.set("/extensions", ordered_json::parse(R"([{"name": "acorn-rtc"}, {"name": "acorn-65c102-coprocessor"}])"));
    CHECK(draft.changed_where("/extensions", tube));
    CHECK_FALSE(draft.changed_where("/extensions", others));
}
