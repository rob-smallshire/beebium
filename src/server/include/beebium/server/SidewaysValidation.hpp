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

// Validate a machine's sideways configurations, a preset's sideways_bank
// entries and --sideways arguments together, against a machine variant's
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
//     conflict);
//
//   * a bank whose socket a cartridge brings (SocketSpec::provided_by_point)
//     with no cartridge at that point.
//
// Messages speak in the preset's terms (the bank, the socket's label, the
// type asked for), never quoting a command line to a configurator's user;
// one about a --sideways argument adds the argument as typed.
//
// All detected problems are reported in one error message rather than
// failing on the first one, so the user can fix every problem before
// rerunning.

#ifndef BEEBIUM_SERVER_SIDEWAYS_VALIDATION_HPP
#define BEEBIUM_SERVER_SIDEWAYS_VALIDATION_HPP

#include "CliArgParsers.hpp"
#include <beebium/SlotTopology.hpp>
#include <beebium/extension/AttachmentPointCatalogue.hpp>

#include <algorithm>
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

// What a bank of that type is, for a sentence: "a ROM", "RAM", "empty".
inline std::string_view sideways_type_phrase(SidewaysSlotType t) {
    switch (t) {
        case SidewaysSlotType::Empty: return "empty";
        case SidewaysSlotType::Rom:   return "a ROM";
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

// "Socket IC52 (aliased to banks 0, 4, 8, 12)", or "Socket IC41".
inline std::string format_socket_clause(const SocketSpec& spec) {
    std::ostringstream out;
    out << "Socket " << spec.label;
    if (spec.slots.size() > 1) {
        out << " (aliased to banks " << format_slot_list(spec.slots) << ")";
    }
    return out.str();
}

// The slot types a socket supports, e.g. "ROM or empty".
inline std::string format_supported_types(const SocketSpec& spec) {
    std::vector<std::string_view> names;
    if (spec.supports_rom) names.push_back("ROM");
    if (spec.supports_ram) names.push_back("RAM");
    if (spec.supports_empty) names.push_back("empty");
    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out << (i + 1 == names.size() ? " or " : ", ");
        out << names[i];
    }
    return out.str();
}

// A --sideways request as the user typed it.
inline std::string format_cli_request(const SidewaysConfig& cfg) {
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

// One request in the terms it was made in: a --sideways argument as typed,
// or a preset's sideways_bank entry as the bank and what it holds.
inline std::string format_request(const SidewaysConfig& cfg) {
    if (cfg.from_cli) return format_cli_request(cfg);
    std::ostringstream out;
    out << "bank " << static_cast<int>(cfg.slot) << " as " << sideways_type_name(cfg.type);
    if (!cfg.image_filepath.empty()) out << " " << cfg.image_filepath;
    if (cfg.write_protected) out << ", write-protected";
    return out.str();
}

// " (requested by --sideways ...)" for a --sideways argument, so the
// message points back at the command line; nothing for a preset's entry,
// whose sentence already names the bank.
inline std::string cli_request_suffix(const SidewaysConfig& cfg) {
    if (!cfg.from_cli) return {};
    return " (requested by " + format_cli_request(cfg) + ")";
}

// The point's name as the configurator shows it: "the Rear Cartridge Slot".
inline std::string point_phrase(std::string_view point_id) {
    return "the " + std::string(beebium::attachment_point_display_name(point_id));
}

}  // namespace detail

// One problem with a list of sideways configurations: the index of the
// configuration it is reported against (for a conflict, the later one) and
// the message.
struct SidewaysProblem {
    size_t index = 0;
    std::string message;
};

// Every problem with a list of sideways configurations, a preset's
// sideways_bank entries and --sideways arguments together, against a
// machine variant's slot topology, in the order found. A message speaks in
// the preset's terms (the bank, the socket's label, the type asked for); one
// about a --sideways argument also quotes the argument.
inline std::vector<SidewaysProblem> sideways_config_problems(
    const SlotTopology& topo,
    const std::vector<SidewaysConfig>& configs)
{
    std::vector<SidewaysProblem> problems;

    // Bucket each config by the physical socket it targets.
    // Each entry is (socket pointer, original index in configs).
    std::map<int, std::vector<size_t>> by_socket;

    for (size_t i = 0; i < configs.size(); ++i) {
        const auto& cfg = configs[i];
        const SocketSpec* socket = topo.find_socket_for_slot(cfg.slot);
        if (socket == nullptr) {
            std::ostringstream msg;
            msg << "Bank " << static_cast<int>(cfg.slot)
                << " does not exist on this machine"
                << detail::cli_request_suffix(cfg) << ". "
                << "Its banks are "
                << detail::format_slot_list(topo.all_slots()) << '.';
            problems.push_back({i, msg.str()});
            continue;
        }

        // Check the socket supports the requested type.
        bool ok = true;
        switch (cfg.type) {
            case SidewaysSlotType::Rom:   ok = socket->supports_rom; break;
            case SidewaysSlotType::Ram:   ok = socket->supports_ram; break;
            case SidewaysSlotType::Empty: ok = socket->supports_empty; break;
        }
        if (!ok && !socket->provided_by_point.empty()) {
            // A socket an extension brings, with no extension there.
            const auto& point = socket->provided_by_point;
            std::ostringstream msg;
            msg << "Bank " << static_cast<int>(cfg.slot) << " cannot be "
                << detail::sideways_type_phrase(cfg.type)
                << detail::cli_request_suffix(cfg) << ": it is "
                << detail::point_phrase(point) << "'s, and has no socket until a "
                << "cartridge is fitted there. ";
            if (cfg.from_cli) {
                msg << "Place a cartridge extension with at=" << point << ".";
            } else {
                msg << "Fit a cartridge at " << detail::point_phrase(point)
                    << " under Peripherals.";
            }
            problems.push_back({i, msg.str()});
            continue;
        }
        if (!ok) {
            std::ostringstream msg;
            msg << "Bank " << static_cast<int>(cfg.slot) << " cannot be "
                << detail::sideways_type_phrase(cfg.type)
                << detail::cli_request_suffix(cfg) << ": its socket, " << socket->label
                << ", supports only " << detail::format_supported_types(*socket)
                << " on this machine.";
            problems.push_back({i, msg.str()});
            continue;
        }

        by_socket[socket->socket_index].push_back(i);
    }

    // For each socket targeted by more than one request, all of those
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
            msg << detail::format_socket_clause(*socket)
                << " has conflicting type requests:";
            for (size_t idx : indices) {
                msg << "\n  - " << detail::format_request(configs[idx]);
            }
            msg << "\nA single physical socket cannot hold more than one type.";
            problems.push_back({indices.back(), msg.str()});
            continue;
        }

        // For ROM, all specified image paths must agree. (At most one path may
        // legitimately be specified per socket; if several requests target
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
                msg << detail::format_socket_clause(*socket)
                    << " has conflicting ROM image requests:";
                for (size_t idx : indices) {
                    msg << "\n  - " << detail::format_request(configs[idx]);
                }
                msg << "\nA single physical socket can only hold one ROM image.";
                problems.push_back({indices.back(), msg.str()});
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
                msg << detail::format_socket_clause(*socket)
                    << " has conflicting RAM preload image requests:";
                for (size_t idx : indices) {
                    msg << "\n  - " << detail::format_request(configs[idx]);
                }
                msg << "\nA single physical socket can only be preloaded with one image.";
                problems.push_back({indices.back(), msg.str()});
                continue;
            }
        }

        // No semantic conflict, but warn about pure duplication: same slot
        // number specified twice. This is almost certainly a typo.
        std::map<std::uint8_t, int> slot_counts;
        for (size_t idx : indices) {
            slot_counts[configs[idx].slot]++;
        }
        auto last_index_of = [&](std::uint8_t slot) {
            size_t last = indices.front();
            for (size_t idx : indices) {
                if (configs[idx].slot == slot) last = idx;
            }
            return last;
        };
        for (const auto& [slot, count] : slot_counts) {
            if (count > 1) {
                std::ostringstream msg;
                msg << "Bank " << static_cast<int>(slot)
                    << " is given " << count << " times.";
                problems.push_back({last_index_of(slot), msg.str()});
            }
        }
    }

    // A RAM chip spanning several slots is fitted to all of them or none.
    for (const auto& chip_slots : topo.ram_chips) {
        std::vector<size_t> ram_requests;
        for (size_t i = 0; i < configs.size(); ++i) {
            if (configs[i].type != SidewaysSlotType::Ram) continue;
            if (std::find(chip_slots.begin(), chip_slots.end(),
                          static_cast<int>(configs[i].slot)) != chip_slots.end()) {
                ram_requests.push_back(i);
            }
        }
        if (ram_requests.empty()) continue;
        std::vector<int> missing;
        for (int slot : chip_slots) {
            bool requested = std::any_of(
                ram_requests.begin(), ram_requests.end(),
                [&](size_t i) { return static_cast<int>(configs[i].slot) == slot; });
            if (!requested) missing.push_back(slot);
        }
        if (missing.empty()) continue;
        const bool from_cli = configs[ram_requests.front()].from_cli;
        std::ostringstream msg;
        msg << "Banks " << detail::format_slot_list(chip_slots)
            << " share one RAM chip, so RAM must be fitted to all of them or none"
            << " (requested by";
        for (size_t i : ram_requests) msg << ' ' << detail::format_request(configs[i]);
        if (from_cli) {
            msg << "). Also specify:";
            for (int slot : missing) msg << " --sideways slot=" << slot << ":type=RAM";
        } else {
            msg << "). Make " << (missing.size() > 1 ? "banks " : "bank ")
                << detail::format_slot_list(missing) << " RAM as well.";
        }
        problems.push_back({ram_requests.front(), msg.str()});
    }

    return problems;
}

// Validate a list of sideways configurations against a machine variant's
// slot topology. Returns nullopt on success, else the problem's sentence, or
// a list of every problem when there are several.
inline std::optional<std::string> validate_sideways_configs(
    const SlotTopology& topo,
    const std::vector<SidewaysConfig>& configs)
{
    const auto problems = sideways_config_problems(topo, configs);
    if (problems.empty()) return std::nullopt;
    if (problems.size() == 1) return problems.front().message;

    std::ostringstream combined;
    combined << "The sideways banks cannot be configured as asked:";
    for (const auto& p : problems) {
        combined << "\n  * " << p.message;
    }
    return combined.str();
}

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_SIDEWAYS_VALIDATION_HPP
