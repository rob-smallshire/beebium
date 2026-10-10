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

// The items of a list parameter with fields: the template that gives an
// item's string form, formatting an object to it and parsing it back, and
// checking an item's fields. The shapes are the AUN transport's map and
// subnet parameters.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <beebium/extension/ExtensionArgParser.hpp>

using namespace beebium;
using Catch::Matchers::ContainsSubstring;

namespace {

ParameterField integer_field(std::string key, bool required, long long min, long long max,
                             std::string default_value = {}) {
    ParameterField f;
    f.key = std::move(key);
    f.type = "integer";
    f.required = required;
    f.min = min;
    f.max = max;
    f.default_value = std::move(default_value);
    return f;
}

ParameterField string_field(std::string key) {
    ParameterField f;
    f.key = std::move(key);
    f.required = true;
    return f;
}

ParameterSchema map_parameter() {
    ParameterSchema p{"map", "string", "Peers", -1, false, /*is_list=*/true};
    p.fields = {integer_field("net", false, 0, 255, "0"), integer_field("station", true, 1, 254),
                string_field("host"), integer_field("port", true, 1, 65535)};
    p.item_format = "{net}.{station}@{host}@{port}";
    return p;
}

ParameterSchema subnet_parameter() {
    ParameterSchema p{"subnet", "string", "Subnet rules", -1, false, /*is_list=*/true};
    p.fields = {integer_field("net", true, 1, 255), string_field("address")};
    p.item_format = "{net}@{address}";
    return p;
}

ListItemFields parsed(const ParameterSchema& p, std::string_view text) {
    auto result = parse_list_item(p, text);
    INFO(result.error.message);
    REQUIRE(result.fields);
    return *result.fields;
}

}  // namespace

TEST_CASE("the AUN map and subnet templates describe their items", "[extension][list-item]") {
    CHECK_FALSE(check_item_format(map_parameter()));
    CHECK_FALSE(check_item_format(subnet_parameter()));
}

TEST_CASE("a map item formats by its template", "[extension][list-item]") {
    CHECK(format_list_item(map_parameter(),
                           {{"net", "1"}, {"station", "254"}, {"host", "10.0.0.5"},
                            {"port", "32768"}}) == "1.254@10.0.0.5@32768");
}

TEST_CASE("a map item without its net formats without the net or its separator",
          "[extension][list-item]") {
    CHECK(format_list_item(map_parameter(),
                           {{"station", "254"}, {"host", "10.0.0.5"}, {"port", "32768"}}) ==
          "254@10.0.0.5@32768");
}

TEST_CASE("a map item parses into its fields", "[extension][list-item]") {
    CHECK(parsed(map_parameter(), "1.254@10.0.0.5@32768") ==
          ListItemFields{{"net", "1"}, {"station", "254"}, {"host", "10.0.0.5"},
                         {"port", "32768"}});
}

TEST_CASE("a bare station parses with no net, though the host has dots",
          "[extension][list-item]") {
    CHECK(parsed(map_parameter(), "254@10.0.0.5@32768") ==
          ListItemFields{{"station", "254"}, {"host", "10.0.0.5"}, {"port", "32768"}});
}

TEST_CASE("map and subnet items survive a round trip in either form", "[extension][list-item]") {
    for (const char* text : {"0.254@127.0.0.1@32768", "254@127.0.0.1@32768",
                             "3.1@192.168.1.10@1"}) {
        INFO(text);
        CHECK(format_list_item(map_parameter(), parsed(map_parameter(), text)) == text);
    }
    const ListItemFields subnet{{"net", "128"}, {"address", "192.168.1.0/24"}};
    CHECK(format_list_item(subnet_parameter(), subnet) == "128@192.168.1.0/24");
    CHECK(parsed(subnet_parameter(), "128@192.168.1.0/24") == subnet);
}

TEST_CASE("an item missing a separator is malformed as a whole", "[extension][list-item]") {
    auto result = parse_list_item(map_parameter(), "0.254@10.0.0.5");
    CHECK_FALSE(result.fields);
    CHECK(result.error.field.empty());
    CHECK_THAT(result.error.message, ContainsSubstring("{net}.{station}@{host}@{port}"));
    CHECK_FALSE(parse_list_item(subnet_parameter(), "128").fields);
}

TEST_CASE("an item's fields are checked against their types, ranges and requirement",
          "[extension][list-item]") {
    const auto map = map_parameter();
    CHECK_FALSE(check_list_item_fields(
        map, {{"station", "254"}, {"host", "10.0.0.5"}, {"port", "32768"}}));

    auto range = check_list_item_fields(
        map, {{"station", "255"}, {"host", "10.0.0.5"}, {"port", "32768"}});
    REQUIRE(range);
    CHECK(range->field == "station");
    CHECK_THAT(range->message, ContainsSubstring("1-254"));

    auto type = check_list_item_fields(
        map, {{"station", "1"}, {"host", "h"}, {"port", "lots"}});
    REQUIRE(type);
    CHECK(type->field == "port");
    CHECK_THAT(type->message, ContainsSubstring("integer"));

    auto missing = check_list_item_fields(map, {{"station", "1"}, {"port", "32768"}});
    REQUIRE(missing);
    CHECK(missing->field == "host");

    auto empty = check_list_item_fields(map, {{"station", "1"}, {"host", ""}, {"port", "1"}});
    REQUIRE(empty);
    CHECK(empty->field == "host");

    auto unknown = check_list_item_fields(
        map, {{"station", "1"}, {"host", "h"}, {"port", "1"}, {"remark", "x"}});
    REQUIRE(unknown);
    CHECK(unknown->field == "remark");
    CHECK_THAT(unknown->message, ContainsSubstring("net, station, host, port"));
}

TEST_CASE("a field may not hold the separator that ends it", "[extension][list-item]") {
    auto found = check_list_item_fields(
        map_parameter(), {{"station", "1"}, {"host", "a@b"}, {"port", "32768"}});
    REQUIRE(found);
    CHECK(found->field == "host");
    CHECK_THAT(found->message, ContainsSubstring("'@'"));
}

TEST_CASE("a template that cannot describe its items is refused", "[extension][list-item]") {
    auto refused = [](std::string item_format, auto change) {
        auto p = map_parameter();
        p.item_format = std::move(item_format);
        change(p);
        auto err = check_item_format(p);
        REQUIRE(err);
        return *err;
    };
    auto unchanged = [](ParameterSchema&) {};

    CHECK_THAT(refused("", unchanged), ContainsSubstring("item_format"));
    CHECK_THAT(refused("{station}@{host}@{port}", unchanged), ContainsSubstring("'net'"));
    CHECK_THAT(refused("{net}.{station}@{host}", unchanged), ContainsSubstring("'port'"));
    CHECK_THAT(refused("{net}.{station}@{host}{port}", unchanged),
               ContainsSubstring("separator"));
    CHECK_THAT(refused("{net}.{station}@{host}@{port}@{remark}", unchanged),
               ContainsSubstring("'remark'"));
    CHECK_THAT(refused("{net}.{station}@{host}@{port}@{port}", unchanged),
               ContainsSubstring("'port'"));
    CHECK_THAT(refused("{net}.{station}@{host}@{port", unchanged), ContainsSubstring("{"));
    CHECK_THAT(refused("{station}.{net}@{host}@{port}", unchanged),
               ContainsSubstring("optional"));
    CHECK_THAT(refused("{net}.{station}@{host}@{port}",
                       [](ParameterSchema& p) { p.is_list = false; }),
               ContainsSubstring("list"));
    CHECK_THAT(refused("{net}.{station}@{host}@{port}",
                       [](ParameterSchema& p) { p.fields[2].type = "filepath"; }),
               ContainsSubstring("filepath"));

    ParameterSchema no_fields{"tag", "string", "Tags", -1, false, true};
    no_fields.item_format = "{a}";
    CHECK(check_item_format(no_fields));
    CHECK_FALSE(check_item_format(ParameterSchema{"tag", "string", "Tags", -1, false, true}));
}
