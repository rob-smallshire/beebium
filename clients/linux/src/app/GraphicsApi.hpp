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

#ifndef BEEBIUM_LINUX_APP_GRAPHICS_API_HPP
#define BEEBIUM_LINUX_APP_GRAPHICS_API_HPP

#include <optional>
#include <string>
#include <string_view>

namespace beebium::linux_client {

enum class GraphicsApi { vulkan, opengl_es };

struct GraphicsApiChoice {
    GraphicsApi api = GraphicsApi::vulkan;
    std::string reason;  // logged at start-up
};

// Which graphics API the display renders with.
//
// Vulkan is the design's preference (docs/discussion/linux-client-architecture.md
// section 2), with OpenGL ES the fallback. Except on Wayland: Qt 6.8 crashes
// closing a window whose QRhiWidget draws with Vulkan there (the reproducer
// and its stack are in tools/qrhiwidget-wayland-close), so Wayland defaults
// to OpenGL ES until a Qt that fixes it ships. --graphics-api still chooses
// either anywhere.
//
// `requested` is the --graphics-api value, empty when not given: "vulkan",
// or "opengl" or "gles" for OpenGL ES. `platform` is
// QGuiApplication::platformName(). Nullopt for an unknown request.
std::optional<GraphicsApiChoice> choose_graphics_api(std::string_view requested,
                                                     std::string_view platform);

std::string_view graphics_api_name(GraphicsApi api);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_GRAPHICS_API_HPP
