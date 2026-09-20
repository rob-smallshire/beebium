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

// Validate a list of parsed --sideways arguments against a machine variant's
// SlotTopology. Detects the user blunders enumerated in GitHub issue #31:
//
//   * referencing a sideways slot that does not exist on the chosen machine
//     variant (e.g. slot 12 on a Model B+, where there is no socket wired to
//     that slot number);
//
//   * configuring a slot as a type the underlying socket does not support
//     (e.g. RAM on a Model B+, whose sockets are all ROM-only);
//
//   * configuring two aliased slots with conflicting requirements (different
//     types, or different ROM images), since they share a single physical
//     socket on the real hardware;
//
//   * specifying the same slot twice (which is a degenerate case of aliased
//     conflict).
//
// All detected problems are reported in one error message rather than
// failing on the first one, so the user can fix every problem before
// rerunning.

#ifndef BEEBIUM_SERVER_SIDEWAYS_VALIDATION_HPP
#define BEEBIUM_SERVER_SIDEWAYS_VALIDATION_HPP

#include "CliArgParsers.hpp"
#include <beebium/SlotTopology.hpp>

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

namespace detail {

inline std::string_view sideways_type_name(SidewaysSlotType t) {
    switch (t) {
        case SidewaysSlotType::Empty: return "empty";
        case SidewaysSlotType::Rom:   return "ROM";
        case SidewaysSlotType::Ram:   return "RAM";
    }
    return "?";
}

inline std::string format_slot_list(const std::vector<int>& slots) {
    std::ostringstream out;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (i > 0) out << ", ";
        out << slots[i];
    }
    return out.str();
}

inline std::string format_socket_alias_clause(const SocketSpec& spec) {
    std::ostringstream out;
    out << "Socket " << spec.label;
    if (spec.slots.size() > 1) {
        out << " (aliased to slots " << format_slot_list(spec.slots) << ")";
    }
    return out.str();
}

// Describe one --sideways request in the same shorthand the user typed,
// so the error message points back at their CLI input.
inline std::string format_request(const SidewaysConfig& cfg) {
    std::ostringstream out;
    out << "--sideways slot=" << static_cast<int>(cfg.slot)
        << ":type=" << sideways_type_name(cfg.type);
    if (!cfg.image_filepath.empty()) {
        out << ":image=" << cfg.image_filepath;
    }
    if (cfg.write_protected) {
        out << ":write-protect";
    }
    return out.str();
}

}  // namespace detail

// Validate a parsed list of --sideways configurations against a machine
// variant's slot topology. Returns nullopt on success, or a multi-line error
// message describing every detected problem on failure.
inline std::optional<std::string> validate_sideways_configs(
    const SlotTopology& topo,
    const std::vector<SidewaysConfig>& configs)
{
    std::vector<std::string> errors;

    // Bucket each config by the physical socket it targets.
    // Each entry is (socket pointer, original index in configs).
    std::map<int, std::vector<size_t>> by_socket;

    for (size_t i = 0; i < configs.size(); ++i) {
        const auto& cfg = configs[i];
        const SocketSpec* socket = topo.find_socket_for_slot(cfg.slot);
        if (socket == nullptr) {
            std::ostringstream msg;
            msg << "Sideways slot " << static_cast<int>(cfg.slot)
                << " does not exist on this machine variant. "
                << "Valid slots are: "
                << detail::format_slot_list(topo.all_slots()) << '.';
            errors.push_back(msg.str());
            continue;
        }

        // Check the socket supports the requested type.
        bool ok = true;
        switch (cfg.type) {
            case SidewaysSlotType::Rom:   ok = socket->supports_rom; break;
            case SidewaysSlotType::Ram:   ok = socket->supports_ram; break;
            case SidewaysSlotType::Empty: ok = socket->supports_empty; break;
        }
        if (!ok) {
            std::ostringstream msg;
            msg << detail::format_socket_alias_clause(*socket)
                << " cannot be configured as "
                << detail::sideways_type_name(cfg.type)
                << " (requested by " << detail::format_request(cfg)
                << "). This socket is ROM-only on this machine variant.";
            errors.push_back(msg.str());
            continue;
        }

        by_socket[socket->socket_index].push_back(i);
    }

    // For each socket targeted by more than one --sideways arg, all of those
    // requests must agree -- they map to the same physical chip.
    for (const auto& [socket_idx, indices] : by_socket) {
        if (indices.size() < 2) continue;

        const SocketSpec* socket = nullptr;
        for (const auto& s : topo.sockets) {
            if (s.socket_index == socket_idx) { socket = &s; break; }
        }
        if (socket == nullptr) continue;  // unreachable

        // All requests must have the same slot type.
        const SidewaysSlotType first_type = configs[indices.front()].type;
        bool type_conflict = false;
        for (size_t idx : indices) {
            if (configs[idx].type != first_type) { type_conflict = true; break; }
        }
        if (type_conflict) {
            std::ostringstream msg;
            msg << detail::format_socket_alias_clause(*socket)
                << " has conflicting type requests:";
            for (size_t idx : indices) {
                msg << "\n  - " << detail::format_request(configs[idx]);
            }
            msg << "\nA single physical socket cannot hold more than one type.";
            errors.push_back(msg.str());
            continue;
        }

        // For ROM, all specified image paths must agree. (At most one path may
        // legitimately be specified per socket; if multiple arguments target
        // the same socket they must all name the same image, since there is
        // only one physical chip.)
        if (first_type == SidewaysSlotType::Rom) {
            std::string canonical;
            bool image_conflict = false;
            for (size_t idx : indices) {
                const auto& path = configs[idx].image_filepath;
                if (path.empty()) continue;  // shouldn't happen for ROM (parser enforces)
                if (canonical.empty()) {
                    canonical = path;
                } else if (path != canonical) {
                    image_conflict = true;
                    break;
                }
            }
            if (image_conflict) {
                std::ostringstream msg;
                msg << detail::format_socket_alias_clause(*socket)
                    << " has conflicting ROM image requests:";
                for (size_t idx : indices) {
                    msg << "\n  - " << detail::format_request(configs[idx]);
                }
                msg << "\nA single physical socket can only hold one ROM image.";
                errors.push_back(msg.str());
                continue;
            }
        }

        // For RAM (with optional preload image), preload paths must agree
        // for the same reason. Empty has no image so cannot conflict.
        if (first_type == SidewaysSlotType::Ram) {
            std::string canonical;
            bool image_conflict = false;
            for (size_t idx : indices) {
                const auto& path = configs[idx].image_filepath;
                if (path.empty()) continue;
                if (canonical.empty()) {
                    canonical = path;
                } else if (path != canonical) {
                    image_conflict = true;
                    break;
                }
            }
            if (image_conflict) {
                std::ostringstream msg;
                msg << detail::format_socket_alias_clause(*socket)
                    << " has conflicting RAM preload image requests:";
                for (size_t idx : indices) {
                    msg << "\n  - " << detail::format_request(configs[idx]);
                }
                msg << "\nA single physical socket can only be preloaded with one image.";
                errors.push_back(msg.str());
                continue;
            }
        }

        // No semantic conflict, but warn about pure duplication: same slot
        // number specified twice. This is almost certainly a typo.
        std::map<std::uint8_t, int> slot_counts;
        for (size_t idx : indices) {
            slot_counts[configs[idx].slot]++;
        }
        for (const auto& [slot, count] : slot_counts) {
            if (count > 1) {
                std::ostringstream msg;
                msg << "Sideways slot " << static_cast<int>(slot)
                    << " specified " << count << " times.";
                errors.push_back(msg.str());
            }
        }
    }

    if (errors.empty()) return std::nullopt;

    std::ostringstream combined;
    combined << "Invalid --sideways configuration:";
    for (const auto& e : errors) {
        combined << "\n  * " << e;
    }
    return combined.str();
}

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_SIDEWAYS_VALIDATION_HPP
