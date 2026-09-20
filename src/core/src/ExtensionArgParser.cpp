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

#include "beebium/extension/ExtensionArgParser.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>

namespace beebium {

namespace {

// Format a list of valid parameter keys for error messages.
std::string format_valid_keys(const std::vector<ParameterSchema>& schema) {
    std::string result;
    for (const auto& p : schema) {
        if (!result.empty()) result += ", ";
        result += p.key;
    }
    // Framework-managed keys
    if (!result.empty()) result += ", ";
    result += "id, label";
    return result;
}

// Format the list of positional parameter keys for error messages.
std::string format_positional_keys(const std::vector<ParameterSchema>& schema) {
    // Collect positional params sorted by position
    std::vector<const ParameterSchema*> positional;
    for (const auto& p : schema) {
        if (p.position >= 0) positional.push_back(&p);
    }
    std::sort(positional.begin(), positional.end(),
              [](const ParameterSchema* a, const ParameterSchema* b) {
                  return a->position < b->position;
              });

    std::string result;
    for (const auto* p : positional) {
        if (!result.empty()) result += ", ";
        result += p->key;
    }
    return result;
}

bool is_framework_key(std::string_view key) {
    return key == "id" || key == "label";
}

bool validate_type(std::string_view type, std::string_view value) {
    if (type == "integer") {
        int result;
        auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
        return ec == std::errc{} && ptr == value.data() + value.size();
    }
    if (type == "boolean") {
        return value == "true" || value == "false" || value == "1" || value == "0";
    }
    // "string" and "filepath" accept any value
    return true;
}

std::string type_description(std::string_view type) {
    if (type == "integer") return "an integer";
    if (type == "boolean") return "a boolean (true/false)";
    if (type == "filepath") return "a file path";
    return "a string";
}

}  // namespace

void normalise_list_params(
        std::map<std::string, std::string>& config,
        std::map<std::string, std::vector<std::string>>& list_config,
        const std::vector<ParameterSchema>& schema) {
    for (const auto& p : schema) {
        if (!p.is_list) continue;
        auto cit = config.find(p.key);
        if (cit == config.end()) continue;
        if (list_config.find(p.key) == list_config.end()) {
            list_config[p.key].push_back(std::move(cit->second));
        }
        config.erase(cit);
    }
}

// split_colon_args now lives in beebium/CliArgSplit.hpp (a neutral home; it is
// not extension-specific). It is used below via the header.

namespace {

// Strip a single matched pair of surrounding double quotes from a value.
// Returns false (via `error`) if the value opens a quote it never closes.
bool strip_quotes(std::string& value, std::string& error) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
        return true;
    }
    if (!value.empty() && value.front() == '"') {
        error = "unterminated quote in value '" + value + "'";
        return false;
    }
    return true;
}

// A token beginning with '//' is the tell-tale of an unquoted scheme://... value
// that the tokenizer split at the scheme colon (scheme://host -> 'scheme',
// '//host'; file:///path -> 'file', '///path'). Guide the user to quoting.
bool looks_like_split_url(std::string_view token) {
    return token.size() >= 2 && token[0] == '/' && token[1] == '/';
}

}  // namespace

ParseResult parse_extension_args(
        std::string_view cli_name,
        std::string_view arg_string,
        const std::vector<ParameterSchema>& schema) {

    ParseResult result;
    result.ok = true;

    auto err = [&](const std::string& msg) -> ParseResult {
        ParseResult r;
        r.ok = false;
        r.error = std::string("--") + std::string(cli_name) + ": " + msg;
        return r;
    };

    // Build lookup structures
    std::map<std::string, const ParameterSchema*> by_key;
    std::vector<const ParameterSchema*> positional;
    for (const auto& p : schema) {
        by_key[p.key] = &p;
        if (p.position >= 0) {
            positional.push_back(&p);
        }
    }
    std::sort(positional.begin(), positional.end(),
              [](const ParameterSchema* a, const ParameterSchema* b) {
                  return a->position < b->position;
              });

    // Split the argument string
    auto tokens = split_colon_args(arg_string);

    // Remove empty tokens (e.g. from trailing colon)
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                [](const std::string& s) { return s.empty(); }),
                 tokens.end());

    if (tokens.empty() && !arg_string.empty()) {
        // arg_string was all colons
        tokens.clear();
    }

    // Guide the user past the most common mistake: an unquoted URL value, which
    // the colon split tore apart at the scheme colon. A stray '//...' token is
    // the unmistakable signature (a quoted value never produces one).
    for (const auto& token : tokens) {
        if (looks_like_split_url(token)) {
            return err("'" + token
                       + "' looks like part of an unquoted URL -- wrap values that "
                         "contain ':' in double quotes, e.g. key=\"scheme://host:port\"");
        }
    }

    // Parse tokens
    size_t positional_index = 0;

    for (const auto& token : tokens) {
        auto eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            // Keyword argument: key=value
            std::string key = token.substr(0, eq_pos);
            std::string value = token.substr(eq_pos + 1);
            if (std::string quote_error; !strip_quotes(value, quote_error)) {
                return err(quote_error);
            }

            if (is_framework_key(key)) {
                // Framework-managed key -- pass through without schema validation
                if (result.config.count(key)) {
                    return err("parameter '" + key + "' specified twice");
                }
                result.config[key] = std::move(value);
                // If this keyword is in a positional slot, consume the position
                positional_index++;
                continue;
            }

            auto it = by_key.find(key);
            if (it == by_key.end()) {
                return err("unknown parameter '" + key
                           + "' (valid parameters: " + format_valid_keys(schema) + ")");
            }

            const auto* param = it->second;

            if (!param->is_list && result.config.count(key)) {
                return err("parameter '" + key + "' specified twice");
            }

            // If positional, must be in the right slot
            if (param->position >= 0) {
                if (static_cast<size_t>(param->position) != positional_index) {
                    return err("keyword parameter '" + key + "' must appear at position "
                               + std::to_string(param->position)
                               + " but appears at position "
                               + std::to_string(positional_index));
                }
                positional_index++;
            }

            // List params accumulate into list_config as opaque vector<string>;
            // the parser does not inspect their contents. Scalar params go
            // into config as before.
            if (param->is_list) {
                result.list_config[key].push_back(std::move(value));
            } else {
                result.config[key] = std::move(value);
            }

        } else {
            // Positional argument
            if (positional_index >= positional.size()) {
                return err("unexpected positional argument '" + token
                           + "' (expected at most "
                           + std::to_string(positional.size()) + ": "
                           + format_positional_keys(schema) + ")");
            }

            const auto* param = positional[positional_index];

            if (result.config.count(param->key)) {
                return err("parameter '" + param->key + "' specified twice");
            }

            std::string value = token;
            if (std::string quote_error; !strip_quotes(value, quote_error)) {
                return err(quote_error);
            }
            result.config[param->key] = std::move(value);
            positional_index++;
        }
    }

    // Apply defaults for missing optional parameters
    for (const auto& p : schema) {
        if (result.config.count(p.key) == 0) {
            if (!p.default_value.empty()) {
                result.config[p.key] = p.default_value;
            }
        }
    }

    // Validate required parameters
    for (const auto& p : schema) {
        if (p.required && result.config.count(p.key) == 0) {
            return err("missing required parameter '" + p.key
                       + "' (" + p.description + ")");
        }
    }

    // Validate types
    for (const auto& p : schema) {
        auto it = result.config.find(p.key);
        if (it != result.config.end() && !it->second.empty()) {
            if (!validate_type(p.type, it->second)) {
                return err("parameter '" + p.key + "' must be "
                           + type_description(p.type)
                           + ", got '" + it->second + "'");
            }
        }
    }

    return result;
}

}  // namespace beebium
