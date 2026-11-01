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

#include "app/GraphicsApi.hpp"

namespace beebium::linux_client {

std::optional<GraphicsApiChoice> choose_graphics_api(std::string_view requested,
                                                     std::string_view platform) {
    if (requested == "vulkan") {
        return GraphicsApiChoice{GraphicsApi::vulkan, "requested with --graphics-api"};
    }
    if (requested == "opengl" || requested == "gles") {
        return GraphicsApiChoice{GraphicsApi::opengl_es, "requested with --graphics-api"};
    }
    if (!requested.empty()) {
        return std::nullopt;
    }
    if (platform.substr(0, 7) == "wayland") {
        return GraphicsApiChoice{
            GraphicsApi::opengl_es,
            "the default on Wayland, where Qt 6.8 crashes closing a Vulkan window "
            "(clients/linux/tools/qrhiwidget-wayland-close)"};
    }
    return GraphicsApiChoice{GraphicsApi::vulkan, "the default"};
}

std::string_view graphics_api_name(GraphicsApi api) {
    return api == GraphicsApi::vulkan ? "Vulkan" : "OpenGL ES";
}

}  // namespace beebium::linux_client
