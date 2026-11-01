# Reproducer for a Qt 6.8 crash closing a Vulkan QRhiWidget window on
# Wayland; see main.cpp. A project of its own, never built with the client.

cmake_minimum_required(VERSION 3.21)
project(qrhiwidget-wayland-close LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Gui)
add_executable(qrhiwidget-wayland-close main.cpp)
# QRhi's headers: Debian packages them with the private ones.
target_link_libraries(qrhiwidget-wayland-close PRIVATE Qt6::Widgets Qt6::GuiPrivate)
