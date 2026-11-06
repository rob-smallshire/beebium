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

// The shared conformance corpus (docs/keyboard-mapping-files.md 6): every
// case in rules.json that is portable or Linux's, resolved here exactly as
// the other clients must.

#include "keyboard/KeyResolver.hpp"
#include "keyboard/MappingFile.hpp"

#include "Corpus.hpp"

#include <catch2/catch_test_macros.hpp>


using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

HostModifiers modifiers_from(const Json& names) {
    HostModifiers held = 0;
    for (const auto& name : names) {
        const std::string text = name.get<std::string>();
        held |= text == "Shift"     ? MODIFIER_SHIFT
                : text == "Control" ? MODIFIER_CONTROL
                : text == "Alt"     ? MODIFIER_ALT
                : text == "Meta"    ? MODIFIER_META
                                    : 0;
    }
    return held;
}

}  // namespace

TEST_CASE("Every portable and Linux case in the corpus resolves as expected",
          "[keyboard][corpus]") {
    const Json corpus = Json::parse(corpus_text("rules.json"));

    std::vector<BbcKeyEntry> entries;
    for (const auto& row : corpus["catalogue"]["entries"]) {
        entries.push_back({row["character"].get<std::string>(),
                           static_cast<std::uint8_t>(row["ik"].get<int>()),
                           row["needsShift"].get<bool>(), row["name"].get<std::string>()});
    }
    const BbcKeyTable table(entries);

    // A case's mapping, read with the keyboard classes the case states
    // attached (3.5), a standard keyboard when it states none.
    const auto mapping_for = [&](const std::string& name, const KeyboardClasses& keyboards) {
        if (corpus["mappings"].contains(name)) {
            auto parsed = parse_mapping_file(corpus["mappings"][name].dump(), keyboards);
            REQUIRE(std::holds_alternative<MappingFile>(parsed));
            return std::get<MappingFile>(std::move(parsed));
        }
        return corpus_mapping(name, keyboards);
    };

    int run = 0;
    for (const auto& item : corpus["cases"]) {
        const std::string name = item["name"].get<std::string>();
        const Json& event = item["event"];
        const std::string platform = event.value("platform", "");
        if (!platform.empty() && platform != "linux") {
            continue;  // another client's native key
        }
        ++run;
        INFO(item["id"].get<std::string>() << " " << name);

        KeyboardClasses keyboards{"standard"};
        if (event.contains("keyboards")) {
            keyboards.clear();
            for (const auto& keyboard : event["keyboards"]) {
                keyboards.insert(keyboard.get<std::string>());
            }
        }
        const MappingFile mapping = mapping_for(item["mapping"].get<std::string>(), keyboards);

        HostKey key;
        key.code = event.value("code", "");
        key.modifiers = modifiers_from(event.value("modifiers", Json::array()));
        key.text = event.value("text", "");
        if (event.contains("native")) {
            key.evdev = event["native"]["scancode"].get<std::uint32_t>();
        }
        const auto resolved = resolve_key(mapping, table, key);

        const Json& expect = item["expect"];
        if (expect.value("ignored", false)) {
            // Nothing, or a key the user switched off, which does nothing.
            CHECK((!resolved || resolved->role == KeyRole::switched_off));
        } else if (expect.value("break", false)) {
            REQUIRE(resolved.has_value());
            CHECK(resolved->role == KeyRole::break_key);
        } else {
            REQUIRE(resolved.has_value());
            CHECK(resolved->role != KeyRole::break_key);
            CHECK(int(resolved->ik_number) == expect["ik"].get<int>());
            CHECK(resolved->bbc_shift == expect["shift"].get<bool>());
            CHECK(resolved->bbc_ctrl == expect["ctrl"].get<bool>());
        }
    }
    // The corpus has macOS and Windows cases this client skips, and the rest.
    CHECK(run >= 30);
}

TEST_CASE("Every built-in mapping in the corpus parses", "[keyboard][corpus]") {
    for (const char* filename : {"Logical.json", "Arcadians.json", "Boffin.json",
                                 "Planetoid.json", "Snapper.json", "Thrust.json"}) {
        INFO(filename);
        const MappingFile mapping = corpus_mapping(filename);
        CHECK_FALSE(mapping.keys.empty());
    }
}

TEST_CASE("The built-ins carry the ids section 5 fixes", "[keyboard][corpus]") {
    CHECK(corpus_mapping("Logical.json").id == "00000000-0000-0000-0000-000000000001");
    CHECK(corpus_mapping("Thrust.json").id == "00000000-0000-0000-0000-000000000002");
    CHECK(corpus_mapping("Snapper.json").id == "00000000-0000-0000-0000-000000000003");
    CHECK(corpus_mapping("Arcadians.json").id == "00000000-0000-0000-0000-000000000004");
    CHECK(corpus_mapping("Boffin.json").id == "00000000-0000-0000-0000-000000000005");
    CHECK(corpus_mapping("Planetoid.json").id == "00000000-0000-0000-0000-000000000006");
}
