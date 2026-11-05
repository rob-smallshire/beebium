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

#ifndef BEEBIUM_LINUX_SERVERS_PRESET_LIBRARY_HPP
#define BEEBIUM_LINUX_SERVERS_PRESET_LIBRARY_HPP

#include "servers/CoreCli.hpp"

#include <QString>
#include <QStringList>

#include <vector>

namespace beebium::linux_client {

// The order the preset library lists presets in, as the macOS client's:
// by release date, a missing month or day as 00 and no date last, then by
// name as a person reads numbers in it.
std::vector<PresetListing> library_order(std::vector<PresetListing> presets);

// Names as a person reads them, whatever the locale: letters without
// regard to case, a run of digits by its value (then the shorter run, so
// "2" before "02"), anything else by code point. Negative, zero or
// positive as a sorts before, with or after b.
int natural_compare(const QString& a, const QString& b);

// The preset library's commands, run by the preset's model's server:
// create-preset from another under a new name (its description and date
// come with it), export-preset to an archive, delete-preset of a user's.
QStringList duplicate_preset_arguments(const PresetListing& source, const QString& name);
QStringList export_preset_arguments(const PresetListing& preset, const QString& filepath);
QStringList delete_preset_arguments(const PresetListing& preset);

// The name Duplicate suggests: "<base> Copy", then "<base> Copy 2" and on,
// the first not taken, a copy's own "Copy N" giving way to the next.
QString duplicate_name(const QString& name, const QStringList& taken);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_SERVERS_PRESET_LIBRARY_HPP
