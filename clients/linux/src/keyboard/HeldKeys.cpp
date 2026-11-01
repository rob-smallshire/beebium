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

#include "keyboard/HeldKeys.hpp"

namespace beebium::linux_client {

bool HeldKeys::physical(KeyRole role) const {
    for (const auto& [id, held] : held_) {
        if (held.key.role == role) {
            return true;
        }
    }
    return false;
}

HeldKeys::Facts HeldKeys::facts_for(const ResolvedKey& key) const {
    Facts facts;
    if (key.role == KeyRole::shift || key.role == KeyRole::ctrl) {
        return facts;  // the user's own modifier: physical state, not facts
    }
    if (key.from_character_mapping) {
        // The character decides SHIFT: ride the user's SHIFT when it already
        // gives the right face, add one when it is missing, suppress it when
        // it would give the wrong face. A character never needs or forbids
        // CTRL (R9).
        const bool physical_shift = physical(KeyRole::shift);
        facts.needs_shift = key.bbc_shift && !physical_shift;
        facts.forbids_shift = !key.bbc_shift && physical_shift;
    } else {
        facts.needs_shift = key.bbc_shift;
        facts.needs_ctrl = key.bbc_ctrl;
    }
    return facts;
}

void HeldKeys::reconcile(std::vector<WireEvent>& events) {
    bool forbid_shift = false, need_shift = false, forbid_ctrl = false, need_ctrl = false;
    for (const auto& [id, held] : held_) {
        forbid_shift = forbid_shift || held.facts.forbids_shift;
        need_shift = need_shift || held.facts.needs_shift;
        forbid_ctrl = forbid_ctrl || held.facts.forbids_ctrl;
        need_ctrl = need_ctrl || held.facts.needs_ctrl;
    }
    const bool want_shift = !forbid_shift && (need_shift || physical(KeyRole::shift));
    const bool want_ctrl = !forbid_ctrl && (need_ctrl || physical(KeyRole::ctrl));

    if (want_shift != sent_shift_) {
        events.push_back({want_shift ? WireAction::key_down : WireAction::key_up, BBC_SHIFT_IK});
        sent_shift_ = want_shift;
    }
    if (want_ctrl != sent_ctrl_) {
        events.push_back({want_ctrl ? WireAction::key_down : WireAction::key_up, BBC_CTRL_IK});
        sent_ctrl_ = want_ctrl;
    }
}

std::vector<WireEvent> HeldKeys::press(const KeyId& id, const ResolvedKey& key) {
    std::vector<WireEvent> events;
    if (held_.count(id) != 0) {
        return events;
    }
    const Facts facts = facts_for(key);
    held_.emplace(id, Held{key, facts});
    reconcile(events);
    if (key.role == KeyRole::matrix) {
        events.push_back({WireAction::key_down, key.ik_number});
    } else if (key.role == KeyRole::break_key) {
        events.push_back({WireAction::break_down, 0});
    }
    return events;
}

std::vector<WireEvent> HeldKeys::release(const KeyId& id) {
    std::vector<WireEvent> events;
    const auto found = held_.find(id);
    if (found == held_.end()) {
        return events;
    }
    const ResolvedKey key = found->second.key;
    held_.erase(found);
    if (key.role == KeyRole::matrix) {
        events.push_back({WireAction::key_up, key.ik_number});
    } else if (key.role == KeyRole::break_key) {
        events.push_back({WireAction::break_up, 0});
    }
    reconcile(events);
    return events;
}

std::vector<WireEvent> HeldKeys::release_all() {
    std::vector<WireEvent> events;
    for (const auto& [id, held] : held_) {
        if (held.key.role == KeyRole::matrix) {
            events.push_back({WireAction::key_up, held.key.ik_number});
        } else if (held.key.role == KeyRole::break_key) {
            events.push_back({WireAction::break_up, 0});
        }
    }
    held_.clear();
    reconcile(events);
    return events;
}

}  // namespace beebium::linux_client
