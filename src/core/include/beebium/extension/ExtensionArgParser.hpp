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
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Schema for one extension parameter.
struct ParameterSchema {
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
    bool unique = false;        // the value must differ among the instances
                                // attached to the same provider (e.g. a SCSI
                                // target ID on one bus)
};

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
// required parameters and types are checked. `source` prefixes any error
// (e.g. "preset extension 'scsi-hard-disc'"). The framework keys id and label
// pass through unvalidated, as they do on the command line.
BEEBIUM_EXT_API ParseResult normalise_extension_config(
    std::string_view source,
    std::map<std::string, std::string> config,
    std::map<std::string, std::vector<std::string>> list_config,
    const std::vector<ParameterSchema>& schema);

// The colon-separated CLI tokenizer split_colon_args() moved to a neutral home
// (beebium/CliArgSplit.hpp) - it is not extension-specific. Included here so
// existing users of this header keep compiling.

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ARG_PARSER_HPP
