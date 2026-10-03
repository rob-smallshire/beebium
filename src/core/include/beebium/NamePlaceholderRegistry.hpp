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

// The server's machine-name placeholders: the union of its providers (#153).
//
// Built at launch -- the core machine's provider, the Econet socket's, and
// one per extension that offers placeholders -- and read-only afterwards, so
// it needs no lock. It holds providers by reference; each must outlive it.
//
// Registration enforces the rules of NamePlaceholderProvider.hpp: domains are
// single words and belong to one provider each; keys are well formed, unique,
// and begin with one of their provider's domains and '-'. A provider that
// breaks one is refused whole, leaving the registry unchanged.

#ifndef BEEBIUM_NAME_PLACEHOLDER_REGISTRY_HPP
#define BEEBIUM_NAME_PLACEHOLDER_REGISTRY_HPP

#include "beebium/NameTemplate.hpp"
#include "beebium/extension/NamePlaceholderProvider.hpp"

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

class Extension;

// A placeholder as clients see it: its description and its current value.
struct NamePlaceholder {
    std::string key;
    std::string label;
    std::string description;
    std::string group;
    // What a picker inserts into a template for this placeholder: "{key}".
    // Carried so that front ends need not build it.
    std::string insertion;
    std::string value;
    bool applicable = true;
};

// A provider broke a registration rule. The message names the provider and
// the offending domain or key.
class NamePlaceholderError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

class NamePlaceholderRegistry {
public:
    // Register `provider`, named `provider_name` in error messages. Throws
    // NamePlaceholderError, registering nothing, if any rule is broken.
    void add(const NamePlaceholderProvider& provider, std::string_view provider_name);

    // Register the placeholders `extension` offers through its
    // name_placeholders() hook, named by its instance id. Does nothing when it
    // offers none.
    void add_extension(const Extension& extension);

    // Every placeholder with its current value, in registration order.
    std::vector<NamePlaceholder> snapshot() const;

    // Render a template against the current values.
    NameRendering render(std::string_view name_template) const;

private:
    struct Entry {
        const NamePlaceholderProvider* provider;
        NamePlaceholderInfo info;
    };
    std::vector<Entry> entries_;
    std::map<std::string, std::size_t, std::less<>> index_by_key_;
    // Domain -> the provider name that owns it.
    std::map<std::string, std::string, std::less<>> domain_owners_;
};

}  // namespace beebium

#endif  // BEEBIUM_NAME_PLACEHOLDER_REGISTRY_HPP
