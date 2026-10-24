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
#include <iterator>
#include <optional>
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
    // "string", "filepath" and "dirpath" accept any value
    return true;
}

bool equal_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               auto lower = [](char c) {
                   return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
               };
               return lower(x) == lower(y);
           });
}

std::string type_description(std::string_view type) {
    if (type == "integer") return "an integer";
    if (type == "boolean") return "a boolean (true/false)";
    if (type == "filepath") return "a file path";
    if (type == "dirpath") return "a directory path";
    return "a string";
}

// What is wrong with a non-empty value of a parameter or a field, as the end
// of a sentence naming it ("must be an integer, got 'x'"), or nullopt. A value
// that matches a choice ignoring case is rewritten as the choice declares it.
std::optional<std::string> value_problem(std::string_view type,
                                         const std::optional<long long>& min,
                                         const std::optional<long long>& max,
                                         const std::vector<ParameterChoice>& choices,
                                         std::string& value) {
    if (!validate_type(type, value)) {
        return "must be " + type_description(type) + ", got '" + value + "'";
    }
    if (type == "integer" && (min || max)) {
        const long long v = std::stoll(value);
        if ((min && v < *min) || (max && v > *max)) {
            std::string range = min && max ? std::to_string(*min) + "-" + std::to_string(*max)
                                : min      ? "at least " + std::to_string(*min)
                                           : "at most " + std::to_string(*max);
            return "must be " + range + ", got '" + value + "'";
        }
    }
    if (!choices.empty()) {
        auto match = std::find_if(choices.begin(), choices.end(), [&](const ParameterChoice& c) {
            return equal_ignoring_case(c.value, value);
        });
        if (match == choices.end()) {
            std::string values;
            for (const auto& c : choices) {
                values += (values.empty() ? "" : ", ") + c.value;
            }
            return "must be one of: " + values + ", got '" + value + "'";
        }
        value = match->value;
    }
    return std::nullopt;
}

// One part of an item template: a placeholder naming a field, or a literal.
struct TemplatePart {
    bool is_field = false;
    std::string text;  // the field's key, or the literal
};

// The parts of an item template, or nullopt with `error` set when a brace is
// unmatched or a placeholder is empty.
std::optional<std::vector<TemplatePart>> template_parts(std::string_view item_format,
                                                        std::string& error) {
    std::vector<TemplatePart> parts;
    std::string literal;
    for (std::size_t i = 0; i < item_format.size(); ++i) {
        const char c = item_format[i];
        if (c == '}') {
            error = "item_format '" + std::string(item_format) + "' has a '}' with no '{'";
            return std::nullopt;
        }
        if (c != '{') {
            literal += c;
            continue;
        }
        const auto close = item_format.find('}', i + 1);
        const auto key = item_format.substr(i + 1, close == std::string_view::npos
                                                       ? std::string_view::npos
                                                       : close - i - 1);
        if (close == std::string_view::npos || key.empty() ||
            key.find('{') != std::string_view::npos) {
            error = "item_format '" + std::string(item_format) +
                    "' has a '{' that does not open a placeholder";
            return std::nullopt;
        }
        if (!literal.empty()) parts.push_back({false, std::move(literal)});
        literal.clear();
        parts.push_back({true, std::string(key)});
        i = close;
    }
    if (!literal.empty()) parts.push_back({false, std::move(literal)});
    return parts;
}

// The parts of a parameter's template, which check_item_format has accepted.
std::vector<TemplatePart> item_parts(const ParameterSchema& p) {
    std::string error;
    return template_parts(p.item_format, error).value_or(std::vector<TemplatePart>{});
}

const ParameterField* find_field(const ParameterSchema& p, std::string_view key) {
    for (const auto& f : p.fields) {
        if (f.key == key) return &f;
    }
    return nullptr;
}

std::string field_keys(const ParameterSchema& p) {
    std::string keys;
    for (const auto& f : p.fields) {
        keys += (keys.empty() ? "" : ", ") + f.key;
    }
    return keys;
}

// A field's value, or empty when it is absent.
std::string field_value(const ListItemFields& fields, const std::string& key) {
    auto it = fields.find(key);
    return it == fields.end() ? std::string{} : it->second;
}

}  // namespace

std::optional<std::string> check_item_format(const ParameterSchema& p) {
    if (p.fields.empty()) {
        if (p.item_format.empty()) return std::nullopt;
        return "parameter '" + p.key + "' has an item_format but no fields";
    }
    const std::string where = "parameter '" + p.key + "'";
    if (!p.is_list) return where + " has fields but is not a list";
    if (p.item_format.empty()) return where + " has fields but no item_format";
    for (std::size_t i = 0; i < p.fields.size(); ++i) {
        const auto& f = p.fields[i];
        if (f.key.empty()) return where + " has a field with no key";
        if (f.type != "string" && f.type != "integer" && f.type != "boolean") {
            return where + " field '" + f.key + "' has type '" + f.type +
                   "'; a field is a string, an integer or a boolean";
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (p.fields[j].key == f.key) return where + " has two fields '" + f.key + "'";
        }
    }
    std::string error;
    auto parts = template_parts(p.item_format, error);
    if (!parts) return where + ": " + error;
    std::vector<std::string> named;
    for (std::size_t i = 0; i < parts->size(); ++i) {
        const auto& part = (*parts)[i];
        if (!part.is_field) continue;
        const auto* f = find_field(p, part.text);
        if (!f) return where + " item_format names '" + part.text + "', which is not a field";
        if (std::find(named.begin(), named.end(), part.text) != named.end()) {
            return where + " item_format names '" + part.text + "' twice";
        }
        named.push_back(part.text);
        if (i + 1 < parts->size() && (*parts)[i + 1].is_field) {
            return where + " item_format needs a separator between {" + part.text + "} and {" +
                   (*parts)[i + 1].text + "}";
        }
        if (!f->required && (i != 0 || i + 1 == parts->size())) {
            return where + " field '" + f->key +
                   "' is optional, but only a first field followed by a separator may be";
        }
    }
    for (const auto& f : p.fields) {
        if (std::find(named.begin(), named.end(), f.key) == named.end()) {
            return where + " item_format does not name field '" + f.key + "'";
        }
    }
    return std::nullopt;
}

std::string format_list_item(const ParameterSchema& p, const ListItemFields& fields) {
    auto parts = item_parts(p);
    std::size_t first = 0;
    if (parts.size() > 1 && parts[0].is_field && field_value(fields, parts[0].text).empty()) {
        first = 2;  // the absent optional field and its separator
    }
    std::string text;
    for (std::size_t i = first; i < parts.size(); ++i) {
        text += parts[i].is_field ? field_value(fields, parts[i].text) : parts[i].text;
    }
    return text;
}

ListItemParse parse_list_item(const ParameterSchema& p, std::string_view text) {
    auto parts = item_parts(p);
    auto malformed = [&]() {
        return ListItemParse{std::nullopt,
                             {"", "'" + std::string(text) + "' is not in the form " +
                                      p.item_format}};
    };
    // An optional first field is present when its separator comes before the
    // next one the template has (or, with no next one, at all).
    std::size_t first = 0;
    if (parts.size() > 1 && parts[0].is_field) {
        const auto* f = find_field(p, parts[0].text);
        if (f && !f->required) {
            const auto own = text.find(parts[1].text);
            const auto next = parts.size() > 3 ? text.find(parts[3].text)
                                               : std::string_view::npos;
            if (own == std::string_view::npos || (next != std::string_view::npos && next < own)) {
                first = 2;
            }
        }
    }
    ListItemFields fields;
    std::size_t pos = 0;
    for (std::size_t i = first; i < parts.size(); ++i) {
        const auto& part = parts[i];
        if (!part.is_field) {
            if (text.compare(pos, part.text.size(), part.text) != 0) return malformed();
            pos += part.text.size();
            continue;
        }
        const auto end = i + 1 < parts.size() ? text.find(parts[i + 1].text, pos) : text.size();
        if (end == std::string_view::npos) return malformed();
        fields[part.text] = std::string(text.substr(pos, end - pos));
        pos = end;
    }
    if (pos != text.size()) return malformed();
    return {std::move(fields), {}};
}

std::optional<ListItemError> check_list_item_fields(const ParameterSchema& p,
                                                    const ListItemFields& fields) {
    for (const auto& [key, value] : fields) {
        if (!find_field(p, key)) {
            return ListItemError{key, "has no field '" + key + "' (fields: " + field_keys(p) +
                                          ")"};
        }
    }
    const auto parts = item_parts(p);
    for (const auto& f : p.fields) {
        std::string value = field_value(fields, f.key);
        if (value.empty()) {
            if (f.required) {
                return ListItemError{f.key, "field '" + f.key + "' is required (" +
                                                (f.description.empty() ? f.label : f.description) +
                                                ")"};
            }
            continue;
        }
        if (auto problem = value_problem(f.type, f.min, f.max, f.choices, value)) {
            return ListItemError{f.key, "field '" + f.key + "' " + *problem};
        }
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (parts[i].is_field && parts[i].text == f.key &&
                value.find(parts[i + 1].text) != std::string::npos) {
                return ListItemError{f.key, "field '" + f.key + "' may not contain '" +
                                                parts[i + 1].text + "', got '" + value + "'"};
            }
        }
    }
    return std::nullopt;
}

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

// A Windows drive letter is the one unquoted colon we rejoin automatically:
// split_colon_args tore `map-file=C:\Users\...` into `map-file=C` and
// `\Users\...`. True when `cur` is a `key=value` whose value is a single ASCII
// letter (the drive) and `next` begins with a path separator -- the shape a
// torn `C:\path` or `C:/path` leaves, and nothing else legitimate does. A URL
// (`scheme://host`) does not match: its scheme is more than one letter, and its
// continuation begins `//`, which the split-URL guidance above catches instead.
bool is_drive_letter_split(const std::string& cur, const std::string& next) {
    if (next.empty() || (next[0] != '\\' && next[0] != '/')) {
        return false;
    }
    // A '//' continuation is a split URL, not a drive path; leave it for the
    // split-URL guidance.
    if (next[0] == '/' && next.size() >= 2 && next[1] == '/') {
        return false;
    }
    auto eq = cur.find('=');
    if (eq == std::string::npos) {
        return false;
    }
    std::string_view value(cur);
    value.remove_prefix(eq + 1);
    return value.size() == 1 &&
           ((value[0] >= 'A' && value[0] <= 'Z') ||
            (value[0] >= 'a' && value[0] <= 'z'));
}

// The steps every extension config takes once its keys and values are known,
// however they arrived: record the keys the caller supplied, fill the defaults
// of the optional parameters it left out, then check the required parameters
// are present and the values have their types. `err` builds the failed result.
template <typename Err>
ParseResult complete_config(ParseResult result,
                            const std::vector<ParameterSchema>& schema,
                            Err err) {
    // An empty value is a parameter left out: `key=` on the command line, ""
    // or [] in a preset. So a document that spells out every parameter, with
    // "" where there is no value (resolve-preset's output), means what the
    // same document without them means, and the default still applies.
    for (auto it = result.config.begin(); it != result.config.end();) {
        it = it->second.empty() ? result.config.erase(it) : std::next(it);
    }
    for (auto it = result.list_config.begin(); it != result.list_config.end();) {
        it = it->second.empty() ? result.list_config.erase(it) : std::next(it);
    }

    // Record which keys the caller actually supplied, before any defaults are
    // filled in -- a preset merge uses this to override only typed keys (#166).
    for (const auto& [key, value] : result.config) {
        result.explicit_keys.insert(key);
    }
    for (const auto& [key, values] : result.list_config) {
        result.explicit_keys.insert(key);
    }

    // Parameters that exclude each other: given ones only, before defaults.
    for (const auto& p : schema) {
        if (result.explicit_keys.count(p.key) == 0) continue;
        for (const auto& other : p.excludes) {
            if (result.explicit_keys.count(other) != 0) {
                return err("parameter '" + p.key + "' cannot be combined with '" + other + "'");
            }
        }
    }

    // Apply defaults for missing optional parameters
    for (const auto& p : schema) {
        if (p.default_value.empty()) {
            continue;
        }
        if (p.is_list) {
            if (result.list_config.count(p.key) == 0) {
                result.list_config[p.key].push_back(p.default_value);
            }
        } else if (result.config.count(p.key) == 0) {
            result.config[p.key] = p.default_value;
        }
    }

    // Validate required parameters
    for (const auto& p : schema) {
        const bool present = p.is_list ? result.list_config.count(p.key) != 0
                                       : result.config.count(p.key) != 0;
        if (p.required && !present) {
            return err("missing required parameter '" + p.key
                       + "' (" + p.description + ")");
        }
    }

    // Validate types, then bounds and choices
    for (const auto& p : schema) {
        auto it = result.config.find(p.key);
        if (it == result.config.end() || it->second.empty()) {
            continue;
        }
        if (auto problem = value_problem(p.type, p.min, p.max, p.choices, it->second)) {
            return err("parameter '" + p.key + "' " + *problem);
        }
    }

    // Check each item of a list parameter with fields, as its string form;
    // a string has no field to point into, so an error points at the item.
    for (const auto& p : schema) {
        auto it = result.list_config.find(p.key);
        if (p.fields.empty() || it == result.list_config.end()) continue;
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            const std::string where = p.key + " item " + std::to_string(i) + ": ";
            const std::string pointer = p.key + "/" + std::to_string(i);
            auto parsed = parse_list_item(p, it->second[i]);
            if (!parsed.fields) {
                return err(where + parsed.error.message, pointer);
            }
            if (auto problem = check_list_item_fields(p, *parsed.fields)) {
                return err(where + problem->message, pointer);
            }
        }
    }

    return result;
}

}  // namespace

ParseResult parse_extension_args(
        std::string_view cli_name,
        std::string_view arg_string,
        const std::vector<ParameterSchema>& schema) {

    ParseResult result;
    result.ok = true;

    auto err = [&](const std::string& msg, std::string pointer = {}) -> ParseResult {
        ParseResult r;
        r.ok = false;
        r.error = std::string("--") + std::string(cli_name) + ": " + msg;
        r.error_pointer = std::move(pointer);
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

    // Rejoin a Windows drive letter the split tore off a value: `map-file=C`
    // followed by `\Users\...` becomes `map-file=C:\Users\...` again. Only this
    // narrow shape is rejoined; a value with any other embedded colon must be
    // quoted (and a split URL is surfaced below). Keep absorbing while the rule
    // still applies, which for a real drive path is a single step.
    {
        std::vector<std::string> coalesced;
        coalesced.reserve(tokens.size());
        for (size_t i = 0; i < tokens.size(); ++i) {
            std::string tok = std::move(tokens[i]);
            while (i + 1 < tokens.size() &&
                   is_drive_letter_split(tok, tokens[i + 1])) {
                tok += ':';
                tok += tokens[i + 1];
                ++i;
            }
            coalesced.push_back(std::move(tok));
        }
        tokens = std::move(coalesced);
    }

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

    return complete_config(std::move(result), schema, err);
}

ParseResult normalise_extension_config(
        std::string_view source,
        std::map<std::string, std::string> config,
        std::map<std::string, std::vector<ConfigListItem>> list_config,
        const std::vector<ParameterSchema>& schema) {

    auto err = [&](const std::string& msg, std::string pointer = {}) -> ParseResult {
        ParseResult r;
        r.ok = false;
        r.error = std::string(source) + ": " + msg;
        r.error_pointer = std::move(pointer);
        return r;
    };

    std::map<std::string, const ParameterSchema*> by_key;
    for (const auto& p : schema) {
        by_key[p.key] = &p;
    }

    ParseResult result;
    result.ok = true;

    for (auto& [key, value] : config) {
        if (!is_framework_key(key)) {
            auto it = by_key.find(key);
            if (it == by_key.end()) {
                return err("unknown parameter '" + key
                           + "' (valid parameters: " + format_valid_keys(schema) + ")");
            }
            // A single value for a list parameter is a one-element list.
            if (it->second->is_list) {
                if (list_config.count(key) == 0) {
                    result.list_config[key].push_back(std::move(value));
                }
                continue;
            }
        }
        result.config[key] = std::move(value);
    }
    for (auto& [key, items] : list_config) {
        auto it = by_key.find(key);
        if (it == by_key.end()) {
            return err("unknown parameter '" + key
                       + "' (valid parameters: " + format_valid_keys(schema) + ")");
        }
        const auto& p = *it->second;
        if (!p.is_list) {
            return err("parameter '" + key + "' takes one value, not a list");
        }
        // An object item is checked here, where its fields can be pointed
        // at, and goes on as its string form.
        auto& values = result.list_config[key];
        values.clear();
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto& item = items[i];
            if (!item.fields) {
                values.push_back(std::move(item.text));
                continue;
            }
            const std::string where = key + " item " + std::to_string(i) + ": ";
            const std::string pointer = key + "/" + std::to_string(i);
            if (p.fields.empty()) {
                return err(where + "parameter '" + key + "' has no fields, so an item is a string",
                           pointer);
            }
            if (auto problem = check_list_item_fields(p, *item.fields)) {
                return err(where + problem->message, pointer + "/" + problem->field);
            }
            values.push_back(format_list_item(p, *item.fields));
        }
    }

    return complete_config(std::move(result), schema, err);
}

}  // namespace beebium
