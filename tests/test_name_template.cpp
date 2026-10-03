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

// Machine-name templates (#153): the syntax, rendering and the defined
// behaviour for every malformed input.

#include <catch2/catch_test_macros.hpp>

#include "beebium/NameTemplate.hpp"

#include <chrono>
#include <map>
#include <string>
#include <vector>

using beebium::NamePlaceholderValue;
using beebium::NameRendering;
using beebium::is_valid_placeholder_domain;
using beebium::is_valid_placeholder_key;
using beebium::render_name_template;

namespace {

// Known keys and their values; "econet-net" is known but not applicable.
NameRendering render(std::string_view text) {
    static const std::map<std::string, NamePlaceholderValue, std::less<>> values{
        {"econet-station", {"80", true}},
        {"machine-model", {"BBC Model B", true}},
        {"econet-net", {"", false}},
        {"value-with-braces", {"{x}", true}},
    };
    return render_name_template(text, [](std::string_view key)
                                          -> std::optional<NamePlaceholderValue> {
        auto it = values.find(key);
        if (it == values.end()) return std::nullopt;
        return it->second;
    });
}

using Strings = std::vector<std::string>;

}  // namespace

TEST_CASE("A template without braces is a plain name", "[name][template]") {
    auto r = render("Station 80 (AUN, Model B)");
    CHECK(r.text == "Station 80 (AUN, Model B)");
    CHECK(r.unknown_keys.empty());
    CHECK(r.malformed.empty());
    CHECK(r.inapplicable_keys.empty());
}

TEST_CASE("An empty template renders empty", "[name][template]") {
    auto r = render("");
    CHECK(r.text.empty());
    CHECK(r.unknown_keys.empty());
    CHECK(r.malformed.empty());
}

TEST_CASE("A known placeholder renders its value", "[name][template]") {
    CHECK(render("Station {econet-station}").text == "Station 80");
    CHECK(render("{econet-station}").text == "80");
    CHECK(render("{machine-model}: {econet-station}").text == "BBC Model B: 80");
    CHECK(render("{econet-station}{econet-station}").text == "8080");
}

TEST_CASE("A value is inserted literally, never re-parsed", "[name][template]") {
    auto r = render("[{value-with-braces}]");
    CHECK(r.text == "[{x}]");
    CHECK(r.unknown_keys.empty());
    CHECK(r.malformed.empty());
}

TEST_CASE("Doubled braces are literal braces", "[name][template]") {
    CHECK(render("{{").text == "{");
    CHECK(render("}}").text == "}");
    CHECK(render("{{econet-station}}").text == "{econet-station}");
    CHECK(render("a {{ b }} c").text == "a { b } c");
    // {{{econet-station}}} is an escaped brace, a placeholder, an escaped brace.
    CHECK(render("{{{econet-station}}}").text == "{80}");
    auto r = render("{{econet-station}}");
    CHECK(r.unknown_keys.empty());
    CHECK(r.malformed.empty());
}

TEST_CASE("An unknown key renders verbatim and is reported", "[name][template]") {
    auto r = render("Station {econet-sttion} ok");
    CHECK(r.text == "Station {econet-sttion} ok");
    CHECK(r.unknown_keys == Strings{"econet-sttion"});
    CHECK(r.malformed.empty());
}

TEST_CASE("Unknown keys are reported once each, in order of appearance", "[name][template]") {
    auto r = render("{b-key} {a-key} {b-key} {econet-station}");
    CHECK(r.text == "{b-key} {a-key} {b-key} 80");
    CHECK(r.unknown_keys == Strings{"b-key", "a-key"});
}

TEST_CASE("Reserved characters inside braces make the placeholder unknown",
          "[name][template]") {
    for (const char* text : {"{econet-station:03}", "{econet-station|none}",
                             "{econet-station?}", "{!econet-station}"}) {
        INFO(text);
        auto r = render(text);
        CHECK(r.text == text);
        REQUIRE(r.unknown_keys.size() == 1);
        CHECK("{" + r.unknown_keys[0] + "}" == text);
    }
}

TEST_CASE("A key that breaks the key grammar is unknown", "[name][template]") {
    for (const char* text : {"{Econet-Station}", "{econet station}", "{econet_station}",
                             "{-econet}", "{econet-}", "{econet--station}", "{ }"}) {
        INFO(text);
        auto r = render(text);
        CHECK(r.text == text);
        CHECK(r.unknown_keys.size() == 1);
    }
}

TEST_CASE("An empty placeholder is unknown with an empty key", "[name][template]") {
    auto r = render("a{}b");
    CHECK(r.text == "a{}b");
    CHECK(r.unknown_keys == Strings{""});
    CHECK(r.malformed.empty());
}

TEST_CASE("A known but inapplicable placeholder renders empty and is reported",
          "[name][template]") {
    auto r = render("net [{econet-net}]");
    CHECK(r.text == "net []");
    CHECK(r.unknown_keys.empty());
    CHECK(r.inapplicable_keys == Strings{"econet-net"});
}

TEST_CASE("An unterminated placeholder is literal and reported as malformed",
          "[name][template]") {
    auto r = render("Station {econet-station");
    CHECK(r.text == "Station {econet-station");
    CHECK(r.malformed == Strings{"{econet-station"});
    CHECK(r.unknown_keys.empty());

    auto lone = render("trailing {");
    CHECK(lone.text == "trailing {");
    CHECK(lone.malformed == Strings{"{"});
}

TEST_CASE("A lone closing brace is literal and reported as malformed", "[name][template]") {
    auto r = render("a } b");
    CHECK(r.text == "a } b");
    CHECK(r.malformed == Strings{"}"});

    // After a placeholder, the third brace of '}}}' is the odd one out.
    auto after = render("{econet-station}}}");
    CHECK(after.text == "80}");
    CHECK(after.malformed.empty());
    auto odd = render("{econet-station}}");
    CHECK(odd.text == "80}");
    CHECK(odd.malformed == Strings{"}"});
}

TEST_CASE("A brace opened inside a placeholder ends the outer one as malformed",
          "[name][template]") {
    // The outer '{a' meets another '{' before any '}', so it is literal text;
    // '{econet-station}' is a placeholder; the final '}' is a lone brace.
    auto r = render("{a{econet-station}}");
    CHECK(r.text == "{a80}");
    CHECK(r.malformed == Strings{"{a", "}"});

    auto nested = render("{a{b}c}");
    CHECK(nested.text == "{a{b}c}");
    CHECK(nested.malformed == Strings{"{a", "}"});
    CHECK(nested.unknown_keys == Strings{"b"});
}

TEST_CASE("Malformed fragments are each reported, in order", "[name][template]") {
    auto r = render("} {x");
    CHECK(r.text == "} {x");
    CHECK(r.malformed == Strings{"}", "{x"});
}

TEST_CASE("Rendering is total and linear on very long input", "[name][template]") {
    std::string text;
    for (int i = 0; i < 100000; ++i) text += "ab{{c}}d{econet-station}{";
    const auto start = std::chrono::steady_clock::now();
    auto r = render(text);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // 100000 unterminated braces, each swallowing the next fragment.
    CHECK_FALSE(r.text.empty());
    CHECK(r.malformed.size() == 100000);
    // A generous bound: quadratic behaviour on 2.5 MB would take minutes.
    CHECK(elapsed < std::chrono::seconds(10));

    std::string braces(1'000'000, '{');
    auto opens = render(braces);
    CHECK(opens.text == std::string(500'000, '{'));
    CHECK(opens.malformed.empty());

    std::string closes(1'000'001, '}');
    auto odd = render(closes);
    CHECK(odd.text == std::string(500'001, '}'));
    CHECK(odd.malformed == Strings{"}"});
}

TEST_CASE("Non-ASCII text passes through untouched", "[name][template]") {
    const std::string text = "Station \xC2\xA3{econet-station} \xE2\x9C\x93";
    CHECK(render(text).text == "Station \xC2\xA3" "80 \xE2\x9C\x93");
}

TEST_CASE("Placeholder key and domain grammar", "[name][template]") {
    CHECK(is_valid_placeholder_key("econet-station"));
    CHECK(is_valid_placeholder_key("machine-model"));
    CHECK(is_valid_placeholder_key("scsi-id0-title"));
    CHECK(is_valid_placeholder_key("a"));
    CHECK_FALSE(is_valid_placeholder_key(""));
    CHECK_FALSE(is_valid_placeholder_key("Econet"));
    CHECK_FALSE(is_valid_placeholder_key("econet_station"));
    CHECK_FALSE(is_valid_placeholder_key("-econet"));
    CHECK_FALSE(is_valid_placeholder_key("econet-"));
    CHECK_FALSE(is_valid_placeholder_key("econet--station"));
    CHECK_FALSE(is_valid_placeholder_key("0econet"));
    CHECK_FALSE(is_valid_placeholder_key("econet:station"));

    CHECK(is_valid_placeholder_domain("econet"));
    CHECK(is_valid_placeholder_domain("scsi2"));
    CHECK_FALSE(is_valid_placeholder_domain("aun-map"));
    CHECK_FALSE(is_valid_placeholder_domain(""));
    CHECK_FALSE(is_valid_placeholder_domain("Econet"));
    CHECK_FALSE(is_valid_placeholder_domain("2scsi"));
}
