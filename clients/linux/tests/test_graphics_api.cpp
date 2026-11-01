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

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Vulkan is the default away from Wayland", "[graphics-api]") {
    for (const char* platform : {"xcb", "eglfs", "vkkhrdisplay", "offscreen"}) {
        INFO(platform);
        const auto choice = choose_graphics_api("", platform);
        REQUIRE(choice);
        CHECK(choice->api == GraphicsApi::vulkan);
    }
}

TEST_CASE("OpenGL ES is the default on Wayland, and says why", "[graphics-api]") {
    for (const char* platform : {"wayland", "wayland-egl"}) {
        INFO(platform);
        const auto choice = choose_graphics_api("", platform);
        REQUIRE(choice);
        CHECK(choice->api == GraphicsApi::opengl_es);
        CHECK(choice->reason.find("Wayland") != std::string::npos);
    }
}

TEST_CASE("The graphics-api option overrides the default anywhere", "[graphics-api]") {
    CHECK(choose_graphics_api("vulkan", "wayland")->api == GraphicsApi::vulkan);
    CHECK(choose_graphics_api("opengl", "xcb")->api == GraphicsApi::opengl_es);
    CHECK(choose_graphics_api("gles", "xcb")->api == GraphicsApi::opengl_es);
}

TEST_CASE("An unknown API is refused", "[graphics-api]") {
    CHECK_FALSE(choose_graphics_api("metal", "xcb").has_value());
}
