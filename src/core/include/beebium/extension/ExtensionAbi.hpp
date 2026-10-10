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

#ifndef BEEBIUM_EXTENSION_ABI_HPP
#define BEEBIUM_EXTENSION_ABI_HPP

#include <cstdint>

namespace beebium {

// The version of the binary interface between the server and a plugin: the
// layout of every type that crosses the plugin boundary by value or by
// pointer (ExtensionManifest, ParameterSchema, the Extension class hierarchy
// and its vtables, ExtensionContext, ...). A plugin built against one version
// and loaded by a server of another would misread those types and crash, so
// every plugin exports the version it was built with
// (beebium_plugin_abi_version, added to each plugin by
// beebium_finalize_plugin) and PluginLoader refuses a plugin whose version
// differs, naming both.
//
// Increment this whenever such a type changes layout: a member added,
// removed or reordered, a virtual function added or reordered.
inline constexpr std::uint32_t kExtensionAbiVersion = 4;

// The name of the exported function that returns a plugin's ABI version.
inline constexpr const char* kPluginAbiVersionSymbol = "beebium_plugin_abi_version";

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ABI_HPP
