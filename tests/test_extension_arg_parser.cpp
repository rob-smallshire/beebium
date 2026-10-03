// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <beebium/extension/ExtensionArgParser.hpp>

using namespace beebium;
using Catch::Matchers::ContainsSubstring;

namespace {

// scsi-hdd schema: scsi-id (pos 0, int, default 0), image (pos 1, filepath)
const std::vector<ParameterSchema> kScsiHddSchema = {
    {"scsi-id", "integer", "SCSI target ID (0-7)", 0, false, false, "0"},
    {"image", "filepath", "Path to DAT disc image file", 1, false, false, ""},
    {"adapter-id", "string", "ID of SCSI adapter to attach to", -1, false, false, ""},
};

// Simple schema: one required string
const std::vector<ParameterSchema> kRequiredSchema = {
    {"name", "string", "Device name", 0, true, false, ""},
};

// Boolean schema
const std::vector<ParameterSchema> kBoolSchema = {
    {"enabled", "boolean", "Enable feature", 0, false, false, "true"},
};

// Empty schema (no parameters)
const std::vector<ParameterSchema> kEmptySchema = {};

}  // namespace

// ---------------------------------------------------------------------------
// Colon splitting
// ---------------------------------------------------------------------------

TEST_CASE("split_colon_args empty string", "[extension][arg-parser]") {
    auto tokens = split_colon_args("");
    REQUIRE(tokens.empty());
}

TEST_CASE("split_colon_args single token", "[extension][arg-parser]") {
    auto tokens = split_colon_args("hello");
    REQUIRE(tokens.size() == 1);
    REQUIRE(tokens[0] == "hello");
}

TEST_CASE("split_colon_args two tokens", "[extension][arg-parser]") {
    auto tokens = split_colon_args("0:/path/to/file");
    REQUIRE(tokens.size() == 2);
    REQUIRE(tokens[0] == "0");
    REQUIRE(tokens[1] == "/path/to/file");
}

TEST_CASE("split_colon_args does not split inside double quotes",
          "[extension][arg-parser]") {
    auto tokens = split_colon_args("url=\"ip232://host:25232\"");
    REQUIRE(tokens.size() == 1);
    REQUIRE(tokens[0] == "url=\"ip232://host:25232\"");  // quotes retained here
}

TEST_CASE("split_colon_args quoted value among other tokens",
          "[extension][arg-parser]") {
    auto tokens = split_colon_args("0:image=\"file:///path/to/file\"");
    REQUIRE(tokens.size() == 2);
    REQUIRE(tokens[0] == "0");
    REQUIRE(tokens[1] == "image=\"file:///path/to/file\"");
}

TEST_CASE("split_colon_args splits an unquoted URL (no more :// heuristic)",
          "[extension][arg-parser]") {
    auto tokens = split_colon_args("file:///path/to/file");
    REQUIRE(tokens.size() == 2);
    REQUIRE(tokens[0] == "file");
    REQUIRE(tokens[1] == "///path/to/file");
}

TEST_CASE("split_colon_args keyword arguments", "[extension][arg-parser]") {
    auto tokens = split_colon_args("0:/path:adapter-id=scsi-a");
    REQUIRE(tokens.size() == 3);
    REQUIRE(tokens[0] == "0");
    REQUIRE(tokens[1] == "/path");
    REQUIRE(tokens[2] == "adapter-id=scsi-a");
}

TEST_CASE("split_colon_args trailing colon", "[extension][arg-parser]") {
    auto tokens = split_colon_args("hello:");
    REQUIRE(tokens.size() == 2);
    REQUIRE(tokens[0] == "hello");
    REQUIRE(tokens[1] == "");
}

// ---------------------------------------------------------------------------
// Basic parsing
// ---------------------------------------------------------------------------

TEST_CASE("parse empty arg string with no required params", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "0");  // default applied
}

TEST_CASE("parse single positional argument", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "3", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "3");
}

TEST_CASE("parse two positional arguments", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "0:/path/to/drive.dat", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "0");
    REQUIRE(result.config["image"] == "/path/to/drive.dat");
}

TEST_CASE("parse keyword argument", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "adapter-id=scsi-a", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["adapter-id"] == "scsi-a");
    REQUIRE(result.config["scsi-id"] == "0");  // default applied
}

TEST_CASE("parse mixed positional and keyword", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:/path/to/drive.dat:adapter-id=scsi-a", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "0");
    REQUIRE(result.config["image"] == "/path/to/drive.dat");
    REQUIRE(result.config["adapter-id"] == "scsi-a");
}

TEST_CASE("parse positional in keyword form", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "scsi-id=2:/path/to/drive.dat", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "2");
    REQUIRE(result.config["image"] == "/path/to/drive.dat");
}

TEST_CASE("parse all keyword form", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "scsi-id=1:image=/path/to/drive.dat", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "1");
    REQUIRE(result.config["image"] == "/path/to/drive.dat");
}

TEST_CASE("defaults applied for missing optional params", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "0");
    REQUIRE(result.config.count("image") == 0);  // no default for image
    REQUIRE(result.config.count("adapter-id") == 0);
}

TEST_CASE("explicit_keys records only the keys the user typed, not defaults",
          "[extension][arg-parser]") {
    // scsi-id is typed; its presence in explicit_keys must not be confused with
    // the default that also lands in config. A preset merge relies on this to
    // override only typed keys (#166).
    auto typed = parse_extension_args("scsi-hdd", "5:/img", kScsiHddSchema);
    REQUIRE(typed.ok);
    CHECK(typed.explicit_keys.count("scsi-id") == 1);  // user typed it
    CHECK(typed.explicit_keys.count("image") == 1);    // user typed it
    CHECK(typed.explicit_keys.count("adapter-id") == 0);  // never given

    // With nothing typed, scsi-id still gets its default in config but is NOT
    // explicit -- the distinction the merge needs.
    auto defaulted = parse_extension_args("scsi-hdd", "", kScsiHddSchema);
    REQUIRE(defaulted.ok);
    CHECK(defaulted.config.at("scsi-id") == "0");       // default filled
    CHECK(defaulted.explicit_keys.count("scsi-id") == 0);  // but not explicit
    CHECK(defaulted.explicit_keys.empty());
}

// ---------------------------------------------------------------------------
// Framework-managed keys (id, label)
// ---------------------------------------------------------------------------

TEST_CASE("parse framework key id", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:/path:id=my-drive", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["id"] == "my-drive");
    REQUIRE(result.config["scsi-id"] == "0");
}

TEST_CASE("parse framework key label", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:/path:label=System Disc", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["label"] == "System Disc");
}

TEST_CASE("parse framework keys id and label together", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:/path:id=hdd0:label=Boot Drive", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["id"] == "hdd0");
    REQUIRE(result.config["label"] == "Boot Drive");
}

// ---------------------------------------------------------------------------
// Filepath handling
// ---------------------------------------------------------------------------

TEST_CASE("filepath with quoted file:// URI survives the colon split",
          "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:image=\"file:///Users/rjs/discs/drive.dat\"", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["scsi-id"] == "0");
    REQUIRE(result.config["image"] == "file:///Users/rjs/discs/drive.dat");
}

TEST_CASE("filepath with quoted http:// URI survives the colon split",
          "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:image=\"http://example.com:8080/drive.dat\"", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["image"] == "http://example.com:8080/drive.dat");
}

TEST_CASE("unquoted URI value yields the quoting guidance error",
          "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:file:///Users/rjs/discs/drive.dat", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("double quotes") != std::string::npos);
}

TEST_CASE("unterminated quote in a value is an error", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "image=\"file:///oops", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("unterminated quote") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Multiple instances (independent parses)
// ---------------------------------------------------------------------------

TEST_CASE("two independent parses produce different results", "[extension][arg-parser]") {
    auto r1 = parse_extension_args("scsi-hdd", "0:/drive0.dat", kScsiHddSchema);
    auto r2 = parse_extension_args("scsi-hdd", "1:/drive1.dat", kScsiHddSchema);
    REQUIRE(r1.ok);
    REQUIRE(r2.ok);
    REQUIRE(r1.config["scsi-id"] == "0");
    REQUIRE(r2.config["scsi-id"] == "1");
    REQUIRE(r1.config["image"] == "/drive0.dat");
    REQUIRE(r2.config["image"] == "/drive1.dat");
}

// ---------------------------------------------------------------------------
// Error cases (verify error messages)
// ---------------------------------------------------------------------------

TEST_CASE("error: missing required parameter", "[extension][arg-parser]") {
    auto result = parse_extension_args("test", "", kRequiredSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("--test"));
    REQUIRE_THAT(result.error, ContainsSubstring("missing required parameter"));
    REQUIRE_THAT(result.error, ContainsSubstring("'name'"));
    REQUIRE_THAT(result.error, ContainsSubstring("Device name"));
}

TEST_CASE("error: unknown keyword argument", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "drive=0", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("--scsi-hdd"));
    REQUIRE_THAT(result.error, ContainsSubstring("unknown parameter 'drive'"));
    REQUIRE_THAT(result.error, ContainsSubstring("valid parameters"));
    REQUIRE_THAT(result.error, ContainsSubstring("scsi-id"));
}

TEST_CASE("error: too many positional arguments", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "0:/path/to/drive.dat:extra", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("--scsi-hdd"));
    REQUIRE_THAT(result.error, ContainsSubstring("unexpected positional argument"));
    REQUIRE_THAT(result.error, ContainsSubstring("'extra'"));
    REQUIRE_THAT(result.error, ContainsSubstring("at most 2"));
}

TEST_CASE("error: integer type with non-integer value", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd", "abc", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("--scsi-hdd"));
    REQUIRE_THAT(result.error, ContainsSubstring("'scsi-id'"));
    REQUIRE_THAT(result.error, ContainsSubstring("an integer"));
    REQUIRE_THAT(result.error, ContainsSubstring("'abc'"));
}

TEST_CASE("error: boolean type with non-boolean value", "[extension][arg-parser]") {
    auto result = parse_extension_args("test", "maybe", kBoolSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("'enabled'"));
    REQUIRE_THAT(result.error, ContainsSubstring("a boolean"));
    REQUIRE_THAT(result.error, ContainsSubstring("'maybe'"));
}

TEST_CASE("error: duplicate keyword", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "scsi-id=0:scsi-id=1", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("'scsi-id'"));
    REQUIRE_THAT(result.error, ContainsSubstring("specified twice"));
}

TEST_CASE("error: duplicate framework key", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "id=a:id=b", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("'id'"));
    REQUIRE_THAT(result.error, ContainsSubstring("specified twice"));
}

TEST_CASE("error: keyword at wrong position", "[extension][arg-parser]") {
    // scsi-id is position 0, but image (position 1) appears first
    auto result = parse_extension_args("scsi-hdd",
        "image=/path:scsi-id=0", kScsiHddSchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("'image'"));
    REQUIRE_THAT(result.error, ContainsSubstring("position"));
}

TEST_CASE("error: any arg with empty schema", "[extension][arg-parser]") {
    auto result = parse_extension_args("music-5000", "something", kEmptySchema);
    REQUIRE_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("unexpected positional argument"));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_CASE("empty arg string is valid with all-optional schema", "[extension][arg-parser]") {
    auto result = parse_extension_args("acorn-scsi", "", kEmptySchema);
    REQUIRE(result.ok);
    REQUIRE(result.config.empty());
}

TEST_CASE("only keyword args no positional", "[extension][arg-parser]") {
    auto result = parse_extension_args("scsi-hdd",
        "adapter-id=scsi-a", kScsiHddSchema);
    REQUIRE(result.ok);
    REQUIRE(result.config["adapter-id"] == "scsi-a");
}

TEST_CASE("boolean type accepts true/false/1/0", "[extension][arg-parser]") {
    for (auto val : {"true", "false", "1", "0"}) {
        auto result = parse_extension_args("test", val, kBoolSchema);
        REQUIRE(result.ok);
    }
}

TEST_CASE("integer accepts negative numbers", "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"offset", "integer", "Byte offset", 0, false, false, ""},
    };
    auto result = parse_extension_args("test", "-100", schema);
    REQUIRE(result.ok);
    REQUIRE(result.config["offset"] == "-100");
}

TEST_CASE("list parameter accumulates repeated key=value tokens",
          "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"map", "string", "Peer map entries", -1, false, /*is_list=*/true, ""},
    };
    auto result = parse_extension_args(
        "aun", "map=alpha:map=beta:map=gamma", schema);
    REQUIRE(result.ok);
    // List values land in list_config as a vector of raw tokens; the
    // parser does not inspect their contents.
    REQUIRE(result.config.count("map") == 0);
    REQUIRE(result.list_config["map"] == std::vector<std::string>{"alpha", "beta", "gamma"});
}

TEST_CASE("list parameter accepts a single occurrence",
          "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"map", "string", "Peer map entries", -1, false, /*is_list=*/true, ""},
    };
    auto result = parse_extension_args("aun", "map=only-one", schema);
    REQUIRE(result.ok);
    REQUIRE(result.config.count("map") == 0);
    REQUIRE(result.list_config["map"] == std::vector<std::string>{"only-one"});
}

TEST_CASE("list parameter preserves opaque tokens (no inner tokenising)",
          "[extension][arg-parser]") {
    // The parser must be neutral about inner separators; consumers pick
    // their own. Tokens here contain commas, semicolons, and at-signs --
    // all should survive verbatim.
    std::vector<ParameterSchema> schema = {
        {"map", "string", "Peer map entries", -1, false, /*is_list=*/true, ""},
    };
    auto result = parse_extension_args(
        "aun", "map=0.254@127.0.0.1@32768:map=0.253,127.0.0.1,32769", schema);
    REQUIRE(result.ok);
    REQUIRE(result.list_config["map"] == std::vector<std::string>{
        "0.254@127.0.0.1@32768", "0.253,127.0.0.1,32769"});
}

TEST_CASE("list and scalar params coexist", "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"port", "string", "Bind port", -1, false, false, "32768"},
        {"map",  "string", "Peer entry", -1, false, /*is_list=*/true, ""},
    };
    auto result = parse_extension_args(
        "aun", "port=9000:map=one:map=two", schema);
    REQUIRE(result.ok);
    REQUIRE(result.config.at("port") == "9000");
    REQUIRE(result.config.count("map") == 0);
    REQUIRE(result.list_config.at("map") == std::vector<std::string>{"one", "two"});
}

TEST_CASE("non-list parameter still rejects duplicates",
          "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"name", "string", "Device name", -1, false, false, ""},
    };
    auto result = parse_extension_args("test", "name=a:name=b", schema);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("specified twice") != std::string::npos);
}

TEST_CASE("normalise_list_params moves scalar into list_config as 1-element vector",
          "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"port", "string", "Bind port", -1, false, false, "32768"},
        {"map",  "string", "Peer entry", -1, false, /*is_list=*/true, ""},
    };
    std::map<std::string, std::string> config = {
        {"port", "9000"}, {"map", "0.254@127.0.0.1@32768"}};
    std::map<std::string, std::vector<std::string>> list_config;
    normalise_list_params(config, list_config, schema);
    REQUIRE(config.count("map") == 0);
    REQUIRE(config.at("port") == "9000");
    REQUIRE(list_config.at("map") == std::vector<std::string>{"0.254@127.0.0.1@32768"});
}

TEST_CASE("normalise_list_params: list_config form wins when both present",
          "[extension][arg-parser]") {
    std::vector<ParameterSchema> schema = {
        {"map",  "string", "Peer entry", -1, false, /*is_list=*/true, ""},
    };
    std::map<std::string, std::string> config = {{"map", "from-scalar"}};
    std::map<std::string, std::vector<std::string>> list_config = {
        {"map", {"from-list"}}};
    normalise_list_params(config, list_config, schema);
    REQUIRE(config.count("map") == 0);
    REQUIRE(list_config.at("map") == std::vector<std::string>{"from-list"});
}

// ---------------------------------------------------------------------------
// Windows drive-letter rejoin (#150 Windows CI): split_colon_args tears
// `map-file=C:\path` at the drive colon; the parser puts it back together.
// ---------------------------------------------------------------------------

namespace {
// net/port scalars, a map-file filepath, and a repeatable map list.
const std::vector<ParameterSchema> kAunLikeSchema = {
    {"net", "string", "Econet net", -1, false, false, "0"},
    {"port", "string", "UDP port", -1, false, false, ""},
    {"map-file", "filepath", "Shared map file path", -1, false, false, ""},
    {"map", "string", "Peer mapping", -1, false, true, ""},
};
}  // namespace

TEST_CASE("parse rejoins a backslash Windows drive path in a value",
          "[extension][arg-parser]") {
    auto result = parse_extension_args(
        "aun", "net=0:port=0:map-file=C:\\Users\\x\\aun-map.json", kAunLikeSchema);
    REQUIRE(result.ok);
    CHECK(result.config["map-file"] == "C:\\Users\\x\\aun-map.json");
    CHECK(result.config["net"] == "0");
    CHECK(result.config["port"] == "0");
}

TEST_CASE("parse rejoins a forward-slash Windows drive path in a value",
          "[extension][arg-parser]") {
    auto result = parse_extension_args(
        "aun", "map-file=C:/Users/x/aun-map.json", kAunLikeSchema);
    REQUIRE(result.ok);
    CHECK(result.config["map-file"] == "C:/Users/x/aun-map.json");
}

TEST_CASE("parse does not rejoin a single-letter value before a non-slash token",
          "[extension][arg-parser]") {
    // value 'a' is a single letter, but 'port=0' is not a path continuation:
    // the colon still splits, so map-file is 'a', not 'a:port=0'.
    auto result = parse_extension_args("aun", "map-file=a:port=0", kAunLikeSchema);
    REQUIRE(result.ok);
    CHECK(result.config["map-file"] == "a");
    CHECK(result.config["port"] == "0");
}

TEST_CASE("parse still splits an ordinary colon-separated list value",
          "[extension][arg-parser]") {
    auto result = parse_extension_args(
        "aun", "map=0.1@host@1:map=0.2@host@2", kAunLikeSchema);
    REQUIRE(result.ok);
    REQUIRE(result.list_config["map"].size() == 2);
    CHECK(result.list_config["map"][0] == "0.1@host@1");
    CHECK(result.list_config["map"][1] == "0.2@host@2");
}

TEST_CASE("parse leaves a single-letter-scheme URL for the split-URL guidance",
          "[extension][arg-parser]") {
    // A '//' continuation is a URL, not a drive path: it must not be absorbed,
    // so the split-URL guidance still fires.
    auto result = parse_extension_args("aun", "map-file=s://host", kAunLikeSchema);
    CHECK_FALSE(result.ok);
    REQUIRE_THAT(result.error, ContainsSubstring("URL"));
}
