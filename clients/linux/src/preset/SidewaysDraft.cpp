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

#include "preset/SidewaysDraft.hpp"

#include <algorithm>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const char* SLOTS = "/sideways_bank/slots";

bool answers(const SchemaSocket& socket, const ordered_json& entry) {
    if (!entry.is_object() || !entry.contains("slot") || !entry["slot"].is_number_integer()) {
        return false;
    }
    const int slot = entry["slot"].get<int>();
    return std::find(socket.slots.begin(), socket.slots.end(), slot) != socket.slots.end();
}

}  // namespace

SidewaysDraft::SidewaysDraft(MachineDraft& draft, std::vector<SchemaSocket> sockets)
    : draft_(draft), sockets_(std::move(sockets)) {
    std::stable_sort(sockets_.begin(), sockets_.end(),
                     [](const SchemaSocket& a, const SchemaSocket& b) {
                         return a.priority() > b.priority();
                     });
}

SocketContent SidewaysDraft::read(const ordered_json& slots, std::size_t socket) const {
    SocketContent content;
    if (!slots.is_array()) {
        return content;
    }
    for (const auto& entry : slots) {
        if (answers(sockets_[socket], entry)) {
            content.type = entry.value("type", std::string("empty"));
            const auto uri = entry.value("image_uri", ordered_json());
            content.image_uri = uri.is_string() ? uri.get<std::string>() : std::string();
            content.write_protected = entry.value("write_protected", false);
            break;
        }
    }
    return content;
}

SocketContent SidewaysDraft::content(std::size_t socket) const {
    return read(draft_.get(SLOTS).value_or(ordered_json::array()), socket);
}

SocketContent SidewaysDraft::resolved(std::size_t socket) const {
    const ordered_json::json_pointer path(SLOTS);
    const ordered_json& baseline = draft_.baseline();
    return read(baseline.contains(path) ? baseline.at(path) : ordered_json::array(), socket);
}

std::optional<int> SidewaysDraft::entry_slot(std::size_t socket) const {
    const auto slots = draft_.get(SLOTS);
    if (slots && slots->is_array()) {
        for (const auto& entry : *slots) {
            if (answers(sockets_[socket], entry)) {
                return entry["slot"].get<int>();
            }
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> SidewaysDraft::entry_index(std::size_t socket) const {
    const auto slots = draft_.get(SLOTS);
    if (slots && slots->is_array()) {
        for (std::size_t i = 0; i < slots->size(); ++i) {
            if (answers(sockets_[socket], (*slots)[i])) {
                return i;
            }
        }
    }
    return std::nullopt;
}

void SidewaysDraft::set(std::size_t socket, const SocketContent& content) {
    const int slot = entry_slot(socket).value_or(sockets_[socket].priority());
    ordered_json slots = draft_.get(SLOTS).value_or(ordered_json::array());
    if (!slots.is_array()) {
        slots = ordered_json::array();
    }
    ordered_json entry{{"slot", slot}, {"type", content.type}};
    if (content.type != "empty") {
        entry["image_uri"] = content.image_uri.empty() ? ordered_json() : ordered_json(content.image_uri);
    }
    if (content.type == "ram") {
        entry["write_protected"] = content.write_protected;
    }
    // In the entry's place, or after the rest; the socket's other slots go.
    bool placed = false;
    ordered_json out = ordered_json::array();
    for (const auto& existing : slots) {
        if (answers(sockets_[socket], existing)) {
            if (!placed) {
                out.push_back(entry);
                placed = true;
            }
            continue;
        }
        out.push_back(existing);
    }
    if (!placed) {
        out.push_back(entry);
    }
    draft_.set(SLOTS, out);
}

void SidewaysDraft::swap(std::size_t a, std::size_t b) {
    const SocketContent first = content(a);
    const SocketContent second = content(b);
    set(a, second);
    set(b, first);
}

}  // namespace beebium::linux_client
