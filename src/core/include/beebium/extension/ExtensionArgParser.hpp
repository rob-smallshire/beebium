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

#ifndef BEEBIUM_EXTENSION_ARG_PARSER_HPP
#define BEEBIUM_EXTENSION_ARG_PARSER_HPP

#include "Export.hpp"
#include "beebium/CliArgSplit.hpp"  // split_colon_args (shared tokenizer)

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beebium {

// One allowed value of a parameter that takes one of a fixed set.
struct ParameterChoice {
    ParameterChoice() = default;
    ParameterChoice(std::string value_, std::string label_, std::string description_ = {})
        : value(std::move(value_)), label(std::move(label_)),
          description(std::move(description_)) {}

    std::string value;          // as written in a preset or on the command line
    std::string label;          // human-readable, for a configuration UI
    std::string description;    // what choosing it means, for a configuration
                                // UI; empty when the label says enough
};

// One field of the items of a list parameter whose items have parts: an AUN
// peer's net, station, host and port. The members mean what a parameter's
// do, for one part of one item.
struct ParameterField {
    std::string key;
    std::string label;
    std::string description;
    std::string type = "string";  // "string", "integer" or "boolean"
    bool required = false;
    std::string default_value;    // empty = no default; what the extension
                                  // takes when the field is absent
    std::optional<long long> min;
    std::optional<long long> max;
    std::vector<ParameterChoice> choices;
};

// Schema for one extension parameter.
struct ParameterSchema {
    ParameterSchema() = default;
    // The core fields, in the order a manifest's parameters are usually
    // written; the rest are set by name.
    ParameterSchema(std::string key_, std::string type_, std::string description_,
                    int position_ = -1, bool required_ = false, bool is_list_ = false,
                    std::string default_value_ = {})
        : key(std::move(key_)), type(std::move(type_)), description(std::move(description_)),
          position(position_), required(required_), is_list(is_list_),
          default_value(std::move(default_value_)) {}

    std::string key;
    std::string type;           // "string", "integer", "boolean", "filepath"
    std::string description;
    int position = -1;          // -1 = keyword-only; 0, 1, 2, ... = positional order
    bool required = false;
    bool is_list = false;       // true: repeated key=value tokens accumulate
                                // into ParseResult::list_config as a vector
                                // of raw values (the parser does not tokenise
                                // them -- each consumer picks its own inner
                                // separator).
                                // false: a repeated key is a parse error.
    std::string default_value;  // empty = no default
    std::string provider_of;    // non-empty: the value is the `id` of the
                                // instance providing this attachment point
                                // (e.g. "scsi") that the extension attaches
                                // to; empty value = the default provider
    std::string bundled;        // non-empty: a bare-name value refers to one
                                // of the server's bundled images of this
                                // kind (e.g. "hard-disc"), used through a
                                // per-user copy-on-write working copy
    bool unique = false;        // the value must differ among the instances
                                // attached to the same provider (e.g. a SCSI
                                // target ID on one bus)
    std::string label;          // short human-readable name, for a form
    std::optional<long long> min;  // "integer" only: the least value allowed
    std::optional<long long> max;  // "integer" only: the greatest value allowed
    std::string choices_from;              // non-empty: a configuration UI
                                           // may offer values from this
                                           // host enumeration (today
                                           // "serial-ports", as
                                           // list-serial-ports lists them);
                                           // the value stays free text
    std::vector<std::string> excludes;     // keys that may not be given with
                                           // this one (either side declaring
                                           // it is enough)
    std::vector<ParameterChoice> choices;  // non-empty: the only values allowed
                                           // (matched ignoring ASCII case and
                                           // written as declared)
    std::vector<ParameterField> fields;    // is_list only, non-empty: each item
                                           // is made of these fields, and a
                                           // preset may give it as an object
    std::string item_format;               // with fields: an item's string form,
                                           // {key} placeholders between literal
                                           // separators, e.g.
                                           // "{net}.{station}@{host}@{port}"
};

// The fields of one item of a list parameter, by key, each value as a string
// (as a parameter's value is).
using ListItemFields = std::map<std::string, std::string>;

// One item of a list parameter as a preset gives it: the string form, or, for
// a parameter with fields, an object of field values.
struct ConfigListItem {
    ConfigListItem(std::string text_) : text(std::move(text_)) {}
    ConfigListItem(const char* text_) : text(text_) {}
    ConfigListItem(ListItemFields fields_) : fields(std::move(fields_)) {}

    std::string text;                     // the string form, when fields is unset
    std::optional<ListItemFields> fields; // the object form

    bool operator==(const ConfigListItem&) const = default;
};

// Why a parameter's fields and item_format cannot describe its items, or
// nullopt when they can (or it has neither). The template must name every
// field once, put a non-empty literal separator between two placeholders, and
// may leave only its first field optional, when a separator follows it: that
// field may then be absent together with its separator ("254@host@port" for
// "{net}.{station}@{host}@{port}"). A separator may appear more than once.
BEEBIUM_EXT_API std::optional<std::string> check_item_format(const ParameterSchema& p);

// A problem with one item of a list parameter: the field it concerns (empty
// for the item as a whole) and what is wrong.
struct ListItemError {
    std::string field;
    std::string message;
};

// The string form of an item given as fields, by the parameter's item_format:
// each placeholder replaced by its field's value, and an absent optional first
// field left out with its separator. The fields must have passed
// check_list_item_fields.
BEEBIUM_EXT_API std::string format_list_item(const ParameterSchema& p,
                                             const ListItemFields& fields);

// The fields of an item given in its string form: the text split on the
// template's literal separators, left to right, each at its first occurrence.
// An absent optional first field is absent from the result. The values are
// not checked; check_list_item_fields does that.
struct ListItemParse {
    std::optional<ListItemFields> fields;  // unset when the text is malformed
    ListItemError error;
};
BEEBIUM_EXT_API ListItemParse parse_list_item(const ParameterSchema& p, std::string_view text);

// The first problem with an item's fields, or nullopt: a field the parameter
// does not have, a required field absent, a value not of its field's type,
// range or choices, or a value holding the separator that ends it (which its
// string form could not carry). An empty value is an absent field.
BEEBIUM_EXT_API std::optional<ListItemError> check_list_item_fields(
    const ParameterSchema& p, const ListItemFields& fields);

// Result of parsing extension arguments.
//
// Scalar (non-list) parameters land in `config` as key -> value.
// List parameters land in `list_config` as key -> vector<value> and do not
// appear in `config`.
struct ParseResult {
    bool ok = false;
    std::map<std::string, std::string> config;
    std::map<std::string, std::vector<std::string>> list_config;
    // Keys the caller actually supplied on the command line, before optional
    // parameters' defaults are filled in. A later preset merge needs this to
    // tell a typed value from a defaulted one, so only typed keys override a
    // preset (#166).
    std::set<std::string> explicit_keys;
    std::string error;          // populated when !ok
    std::string error_pointer;  // with error: what it is about, as a JSON
                                // pointer below the config object (e.g.
                                // "map/1/station"); empty for the config as
                                // a whole
};

// Parse a colon-separated argument string against a parameter schema.
//
// Tokens are split on ':' (but not '://' inside URIs). Each token is either:
//   - A keyword argument: "key=value"
//   - A positional argument: assigned by position order in the schema
//
// Positional arguments may also be written in keyword form (key=value)
// but must appear in the correct positional slot.
//
// After parsing, defaults are applied for missing optional parameters,
// required parameters are validated, and type checking is performed.
//
// The cli_name parameter is used in error messages (e.g. "--scsi-hdd").
//
// Framework-managed parameters (id, label) are NOT part of the schema
// and are handled separately by the caller.
BEEBIUM_EXT_API ParseResult parse_extension_args(
    std::string_view cli_name,
    std::string_view arg_string,
    const std::vector<ParameterSchema>& schema);

// Normalise an extension config that arrives already split into key/value
// pairs -- a preset's extensions[].config or econet.transport.parameters --
// exactly as parse_extension_args normalises a command-line string: unknown
// keys are rejected, a single value for an is_list parameter becomes a
// one-element list (and a list for a scalar parameter is an error), the
// supplied keys are recorded in explicit_keys, defaults are filled in, and
// required parameters and types are checked. An item of a list parameter with
// fields may be an object, which is checked field by field and becomes its
// string form, so the extension sees the strings it sees from the command
// line. `source` prefixes any error (e.g. "preset extension
// 'scsi-hard-disc'"). The framework keys id and label pass through
// unvalidated, as they do on the command line.
BEEBIUM_EXT_API ParseResult normalise_extension_config(
    std::string_view source,
    std::map<std::string, std::string> config,
    std::map<std::string, std::vector<ConfigListItem>> list_config,
    const std::vector<ParameterSchema>& schema);

// The colon-separated CLI tokenizer split_colon_args() moved to a neutral home
// (beebium/CliArgSplit.hpp) - it is not extension-specific. Included here so
// existing users of this header keep compiling.

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ARG_PARSER_HPP
