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

#ifndef BEEBIUM_EXTENSION_ATTACHMENT_PLAN_HPP
#define BEEBIUM_EXTENSION_ATTACHMENT_PLAN_HPP

#include "AttachmentPointCatalogue.hpp"
#include "ExtensionManifest.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beebium {

// One extension instance a machine is to load: its canonical name and its
// normalised config (framework id included, if given).
struct AttachmentRequest {
    std::string name;
    std::map<std::string, std::string> config;
};

// Something about how an instance would attach that would stop it loading.
// `instance` indexes the requests; `key` names the parameter at fault, or is
// empty when the instance as a whole is.
struct AttachmentProblem {
    std::size_t instance = 0;
    std::string key;
    std::string message;
};

// Work out, from manifests alone, where each requested extension would attach
// when the machine is built, and report what would stop it. This mirrors
// ExtensionRegistry and ExtensionContext:
//
//  - Every point an extension attaches_to must exist: one of the machine's own
//    (machine_points) or one an instance here provides.
//  - Instances that provide something load first, keeping their order; with
//    no explicit choice an extension attaches to the last provider of the
//    point (ExtensionContext keeps the last one registered). A parameter
//    declared provider_of a point chooses the provider by its `id` instead,
//    and must name one.
//  - Each provider of a point, or the machine's own point, holds at most the
//    point's max_occupancy instances (AttachmentPointCatalogue).
//  - A parameter declared unique must differ among the instances attached to
//    the same provider. Its value is compared after defaults are filled in.
//
// Instances whose name has no manifest are skipped: reporting an unknown
// extension is the caller's job. `known` is every manifest the resolver has,
// so a missing point can name the extensions that would provide it.
inline std::vector<AttachmentProblem> check_attachments(
        const std::vector<AttachmentRequest>& requests,
        const std::vector<const ExtensionManifest*>& known,
        const std::vector<std::string>& machine_points) {
    std::vector<AttachmentProblem> problems;

    auto manifest_of = [&](const std::string& name) -> const ExtensionManifest* {
        for (const auto* m : known) {
            if (m->name == name) return m;
        }
        return nullptr;
    };
    auto config_of = [&](std::size_t i, const ParameterSchema& p) -> std::string {
        const auto& config = requests[i].config;
        auto it = config.find(p.key);
        return it != config.end() ? it->second : p.default_value;
    };

    // The load order: providers first, each group in request order.
    std::vector<std::size_t> order;
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < requests.size(); ++i) {
            const auto* m = manifest_of(requests[i].name);
            const bool provides = m && !m->provides.empty();
            if ((pass == 0) == provides) order.push_back(i);
        }
    }

    // Who provides each point, in load order.
    std::map<std::string, std::vector<std::size_t>> providers;
    for (auto i : order) {
        if (const auto* m = manifest_of(requests[i].name)) {
            for (const auto& point : m->provides) providers[point].push_back(i);
        }
    }

    // For each (point, provider) the instances attached there so far. The
    // machine's own point has no provider instance.
    using Attachment = std::pair<std::string, std::optional<std::size_t>>;
    std::map<Attachment, std::vector<std::size_t>> attached;

    for (auto i : order) {
        const auto* m = manifest_of(requests[i].name);
        if (!m) continue;
        for (const auto& point : m->attaches_to) {
            const std::string point_name(attachment_point_display_name(point));
            const bool on_machine = std::find(machine_points.begin(), machine_points.end(),
                                              point) != machine_points.end();
            const auto pit = providers.find(point);
            if (!on_machine && pit == providers.end()) {
                std::string would_provide;
                for (const auto* other : known) {
                    if (std::find(other->provides.begin(), other->provides.end(), point) !=
                        other->provides.end()) {
                        would_provide += (would_provide.empty() ? "" : ", ") + other->name;
                    }
                }
                problems.push_back(
                    {i, "",
                     "attaches to " + point + " (" + point_name +
                         "), which this machine does not have" +
                         (would_provide.empty()
                              ? std::string()
                              : " and nothing here provides; add one of: " + would_provide)});
                continue;
            }

            // Which provider: the one a provider_of parameter names, else the
            // last one loaded, else the machine's own point.
            std::optional<std::size_t> provider;
            bool chosen_ok = true;
            for (const auto& p : m->parameters) {
                if (p.provider_of != point) continue;
                const auto wanted = config_of(i, p);
                if (wanted.empty()) break;
                if (pit != providers.end()) {
                    for (auto candidate : pit->second) {
                        auto id = requests[candidate].config.find("id");
                        if (id != requests[candidate].config.end() && id->second == wanted) {
                            provider = candidate;
                        }
                    }
                }
                if (!provider) {
                    problems.push_back({i, p.key,
                                        p.key + " '" + wanted + "' names no " + point_name +
                                            " provider here (give the provider that id)"});
                    chosen_ok = false;
                }
                break;
            }
            if (!chosen_ok) continue;
            if (!provider && pit != providers.end()) provider = pit->second.back();
            if (provider && *provider == i) continue;  // never attaches to itself

            auto& siblings = attached[{point, provider}];

            const auto* catalogued = find_attachment_point(point);
            if (catalogued && catalogued->max_occupancy &&
                static_cast<int>(siblings.size()) >= *catalogued->max_occupancy) {
                problems.push_back({i, "",
                                    point_name + " holds at most " +
                                        std::to_string(*catalogued->max_occupancy) +
                                        (*catalogued->max_occupancy == 1 ? " extension"
                                                                         : " extensions")});
                continue;
            }

            bool clash = false;
            for (const auto& p : m->parameters) {
                if (!p.unique) continue;
                const auto value = config_of(i, p);
                for (auto sibling : siblings) {
                    if (requests[sibling].name == requests[i].name &&
                        config_of(sibling, p) == value) {
                        problems.push_back({i, p.key,
                                            p.key + " " + value + " is already used on this " +
                                                point_name});
                        clash = true;
                        break;
                    }
                }
            }
            if (!clash) siblings.push_back(i);
        }
    }

    std::stable_sort(problems.begin(), problems.end(),
                     [](const AttachmentProblem& a, const AttachmentProblem& b) {
                         return a.instance < b.instance;
                     });
    return problems;
}

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ATTACHMENT_PLAN_HPP
