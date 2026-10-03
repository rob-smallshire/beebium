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

#include "beebium/NamePlaceholderRegistry.hpp"

#include "beebium/extension/Extension.hpp"

#include <algorithm>
#include <set>

namespace beebium {

namespace {

std::string in_quotes(std::string_view text) {
    return "'" + std::string(text) + "'";
}

}  // namespace

void NamePlaceholderRegistry::add(const NamePlaceholderProvider& provider,
                                  std::string_view provider_name) {
    const auto domains = provider.placeholder_domains();
    const auto infos = provider.placeholders();
    const std::string who = "name placeholder provider " + in_quotes(provider_name);

    // Validate everything before changing anything.
    if (domains.empty() && !infos.empty()) {
        throw NamePlaceholderError(who + " declares no domains for its placeholders");
    }
    for (const auto& domain : domains) {
        if (!is_valid_placeholder_domain(domain)) {
            throw NamePlaceholderError(
                who + ": domain " + in_quotes(domain) +
                " is not a single lowercase word (letters and digits, starting with a letter)");
        }
        if (auto owner = domain_owners_.find(domain); owner != domain_owners_.end()) {
            throw NamePlaceholderError(who + ": domain " + in_quotes(domain) +
                                       " already belongs to " + in_quotes(owner->second));
        }
    }
    std::set<std::string, std::less<>> seen;
    for (const auto& info : infos) {
        if (!is_valid_placeholder_key(info.key)) {
            throw NamePlaceholderError(
                who + ": key " + in_quotes(info.key) +
                " is not lowercase words joined by single hyphens");
        }
        const bool in_domain = std::any_of(domains.begin(), domains.end(),
            [&](const std::string& domain) {
                return info.key.size() > domain.size() + 1 &&
                       info.key.compare(0, domain.size(), domain) == 0 &&
                       info.key[domain.size()] == '-';
            });
        if (!in_domain) {
            std::string list;
            for (const auto& domain : domains) {
                list += (list.empty() ? "" : ", ") + in_quotes(domain + "-");
            }
            throw NamePlaceholderError(who + ": key " + in_quotes(info.key) +
                                       " does not begin with one of its domains (" +
                                       list + ")");
        }
        if (index_by_key_.count(info.key) != 0 || !seen.insert(info.key).second) {
            throw NamePlaceholderError(who + ": key " + in_quotes(info.key) +
                                       " is already registered");
        }
    }

    for (const auto& domain : domains) {
        domain_owners_.emplace(domain, std::string(provider_name));
    }
    for (const auto& info : infos) {
        index_by_key_.emplace(info.key, entries_.size());
        entries_.push_back({&provider, info});
    }
}

void NamePlaceholderRegistry::add_extension(const Extension& extension) {
    if (const auto* provider = extension.name_placeholders()) {
        add(*provider, extension.id());
    }
}

std::vector<NamePlaceholder> NamePlaceholderRegistry::snapshot() const {
    std::vector<NamePlaceholder> out;
    out.reserve(entries_.size());
    for (const auto& entry : entries_) {
        const auto value = entry.provider->placeholder_value(entry.info.key);
        out.push_back({entry.info.key, entry.info.label, entry.info.description,
                       entry.info.group, "{" + entry.info.key + "}",
                       value.applicable ? value.text : std::string{}, value.applicable});
    }
    return out;
}

NameRendering NamePlaceholderRegistry::render(std::string_view name_template) const {
    return render_name_template(name_template,
        [this](std::string_view key) -> std::optional<NamePlaceholderValue> {
            auto it = index_by_key_.find(key);
            if (it == index_by_key_.end()) return std::nullopt;
            const auto& entry = entries_[it->second];
            return entry.provider->placeholder_value(entry.info.key);
        });
}

}  // namespace beebium
