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

#ifndef BEEBIUM_EXTENSION_ATTACHMENT_POINT_CATALOGUE_HPP
#define BEEBIUM_EXTENSION_ATTACHMENT_POINT_CATALOGUE_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// An attachment point is a place a peripheral extension can plug in: the RS423
// serial port, the user port, the 1MHz bus, the Tube, a SCSI bus, etc. Each
// extension declares the point(s) it attaches to via attaches_to() (and, for
// listing without loading, in its manifest). This catalogue gives every point a
// human-readable display name plus its occupancy bounds, so a configuration UI
// can ask "which of these <point> extensions would you like?" and know whether
// that means at most one (a connector) or several (a bus).
//
// Occupancy is an integer range [min_occupancy, max_occupancy]: min is how many
// must be attached (usually 0 -- optional), max is how many may be (1 for a
// single connector, more for a bus). max_occupancy is std::nullopt when there is
// no fixed upper bound. The range models real hardware that does not fit a
// single/multi boolean -- e.g. a machine with two Tube sockets is 0..2.
//
// The point *ids* are the same strings used by attaches_to() / provides() and
// register_extension_point(). This is the single source of truth for their
// display names. It is deliberately open to extension: future points (e.g. an
// "analogue-port" for joysticks) just gain an entry here.
struct AttachmentPoint {
    std::string id;            // e.g. "serial-port" (matches attaches_to() ids)
    std::string display_name;  // e.g. "Serial Port"
    std::string description;   // one sentence a user understands
    int min_occupancy = 0;     // fewest extensions that may attach (0 = optional)
    std::optional<int> max_occupancy;  // most that may attach; nullopt = unbounded
};

// The known attachment points, in a stable presentation order.
inline const std::vector<AttachmentPoint>& attachment_point_catalogue() {
    static const std::vector<AttachmentPoint> kPoints = {
        {"serial-port", "Serial Port",
         "The RS423 serial port, for modems, printers and other computers.", 0, 1},
        {"user-port", "User Port",
         "The user port, a parallel connector for add-ons such as a real-time clock.", 0, 1},
        {"1mhz-bus", "1 MHz Bus",
         "The 1 MHz bus, the expansion connector for add-ons such as a SCSI host adapter.", 0, std::nullopt},
        {"tube", "Coprocessor",
         "The Tube connector, which takes a second processor.", 0, 1},
        {"scsi", "SCSI Bus",
         "The SCSI bus of a host adapter, which holds up to seven hard discs.", 0, 7},
    };
    return kPoints;
}

// A compact human-readable occupancy label: "1" (exactly one), "0..1", "0..2",
// or "0..N" (no upper bound).
inline std::string occupancy_label(const AttachmentPoint& point) {
    const std::string lo = std::to_string(point.min_occupancy);
    if (!point.max_occupancy) return lo + "..N";
    if (*point.max_occupancy == point.min_occupancy) return lo;
    return lo + ".." + std::to_string(*point.max_occupancy);
}

// The catalogue entry for an id, or nullptr if the id is not catalogued.
inline const AttachmentPoint* find_attachment_point(std::string_view id) {
    for (const auto& p : attachment_point_catalogue()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// The display name for a point id, falling back to the id itself when the point
// is not (yet) catalogued, so callers always have something to show.
inline std::string_view attachment_point_display_name(std::string_view id) {
    const AttachmentPoint* p = find_attachment_point(id);
    return p ? std::string_view(p->display_name) : id;
}

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ATTACHMENT_POINT_CATALOGUE_HPP
