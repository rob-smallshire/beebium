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

// A source of machine-name placeholders (#153).
//
// A machine's name is a template (beebium/NameTemplate.hpp) whose `{key}`
// placeholders the server fills from providers: the core machine, the Econet
// socket, and any extension, which offers one by overriding
// Extension::name_placeholders(). Adding a placeholder is adding an entry
// here; no protocol, client or template change follows.
//
// Every key begins with one of the provider's declared domains and '-'
// (`econet-station` in domain `econet`), and a domain belongs to one
// provider; the registry refuses a provider that breaks either rule. A key,
// once shipped, is never renamed: saved templates refer to it.
//
// A placeholder earns its place only if its value can change while the
// template stays the same, or differs between machines launched from one
// preset; static facts belong in the template as literal text.

#ifndef BEEBIUM_EXTENSION_NAME_PLACEHOLDER_PROVIDER_HPP
#define BEEBIUM_EXTENSION_NAME_PLACEHOLDER_PROVIDER_HPP

#include "Export.hpp"
#include "beebium/NameTemplate.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// A placeholder's description, as a picker shows it.
struct NamePlaceholderInfo {
    std::string key;          // used in templates: "econet-station"
    std::string label;        // short human name: "Econet station"
    std::string description;  // one sentence: what it shows and when it changes
    std::string group;        // picker heading: "Econet", "Machine"
};

class BEEBIUM_EXT_TYPE_VISIBLE NamePlaceholderProvider {
public:
    // Out of line so the vtable is anchored in beebium_extension_api.
    BEEBIUM_EXT_API virtual ~NamePlaceholderProvider();

    // The domains this provider's keys belong to: single lowercase words.
    virtual std::vector<std::string> placeholder_domains() const = 0;

    // The placeholders this provider offers. Fixed for its lifetime: the
    // registry reads them once, when the provider is registered.
    virtual std::vector<NamePlaceholderInfo> placeholders() const = 0;

    // The current value of one of this provider's keys. Called on a server
    // thread -- never the emulation thread -- about once a second and on
    // demand, concurrently with emulation. An implementation reads emulated
    // state only through paths that are safe from another thread (atomics,
    // co-owning handles, or the quiesce primitive; see
    // docs/emulation-thread-ownership.md). Return applicable=false when this
    // machine cannot have a value (the value text is then ignored).
    virtual NamePlaceholderValue placeholder_value(std::string_view key) const = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_NAME_PLACEHOLDER_PROVIDER_HPP
