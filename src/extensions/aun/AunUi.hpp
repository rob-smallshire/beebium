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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_UI_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_UI_HPP

// Server-driven UI for the AUN built-in transport extension. Today (Slice
// 1) it exposes the configured peer list as a Group of Labels; later
// slices will add a Connect/Disconnect Button + UDP-port Label, then an
// Add Peer form (TextInput x 3 + Button). All actions dispatch into
// AunBackend's typed C++ methods directly -- the Extension UI surface is
// in addition to AunService's typed RPCs (which scripts and integration
// tests use), not a replacement. See feedback_extension_multi_api.md.

#include "beebium/extension/ExtensionUi.hpp"

#include <string>

namespace beebium {

class AunEconetTransportExtension;  // forward; defined in this dir

class AunUi : public ExtensionUi {
public:
    explicit AunUi(AunEconetTransportExtension& ext) : ext_(ext) {}

    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;

private:
    // Validate and apply an Add/Edit peer or subnet submission, recording a
    // field-named error (or clearing it on success) and marking the view dirty.
    void apply_add_peer(const std::string& net_stn, const std::string& host,
                        const std::string& port, const std::string& label);
    void apply_add_subnet(const std::string& net, const std::string& subnet,
                          const std::string& label);

    AunEconetTransportExtension& ext_;

    // The last map-edit error (field-named), shown in the panel until the next
    // successful edit clears it. Set by handle_event, read by build_view.
    std::string last_edit_error_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_UI_HPP
