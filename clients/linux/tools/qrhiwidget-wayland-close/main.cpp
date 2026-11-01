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

// Reproducer: Qt 6.8.2 crashes closing a window whose QRhiWidget draws with
// Vulkan on the Wayland platform.
//
// A QRhiWidget clearing its target, in a QMainWindow closed by a timer after
// two seconds. Run under a Wayland compositor:
//
//   QT_QPA_PLATFORM=wayland ./qrhiwidget-wayland-close        (Vulkan: crashes)
//   QT_QPA_PLATFORM=wayland ./qrhiwidget-wayland-close gl     (OpenGL: exits 0)
//   QT_QPA_PLATFORM=xcb     ./qrhiwidget-wayland-close        (Vulkan, X11: exits 0)
//
// Seen on Raspberry Pi 5 (Mesa 26.2.2) and Pi 400 (Mesa 25.0.7), Debian 13 /
// Raspberry Pi OS trixie, Qt 6.8.2, under labwc 0.9.7 and under cage 0.3.1:
// a segmentation fault, an abort (glibc "__pthread_tpp_change_priority:
// assertion failed"), or a hang, on every run with Vulkan. Mesa first warns:
//
//   warning: queue "mesa vk display queue" ... destroyed while proxies still attached:
//     wl_callback#65 still attached
//     wl_buffer#63 still attached
//
// and the crash is on the GUI thread:
//
//   #0 pthread_mutex_lock
//   #1 wl_display_flush                                  (libwayland-client)
//   #2 ??                                                (libvulkan_broadcom)
//   #4 ??                                                (libQt6Gui)
//   #8 QBackingStoreRhiSupport::SwapchainData::reset()
//  #13 QWindowPrivate::destroy()
//  #14 QWindow::event(QEvent*)
//  #17 QGuiApplicationPrivate::processCloseEvent(...)
//  #19 QWindow::close()
//
// Destroying the platform window explicitly before the close hides it makes
// no difference. The Linux client defaults to OpenGL ES on Wayland because of
// this (src/app/GraphicsApi.cpp).
//
// Built on its own, not with the client:
//   cmake -S . -B build -G Ninja && cmake --build build

#include <QApplication>
#include <QMainWindow>
#include <QRhiWidget>
#include <QTimer>

#include <rhi/qrhi.h>

#include <cstring>

namespace {

class ClearWidget : public QRhiWidget {
public:
    void render(QRhiCommandBuffer* command_buffer) override {
        command_buffer->beginPass(renderTarget(), QColor(20, 20, 20), {1.0f, 0});
        command_buffer->endPass();
        update();
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QMainWindow window;
    auto* widget = new ClearWidget;
    const bool use_gl = argc > 1 && std::strcmp(argv[1], "gl") == 0;
    widget->setApi(use_gl ? QRhiWidget::Api::OpenGL : QRhiWidget::Api::Vulkan);
    window.setCentralWidget(widget);
    window.resize(640, 480);
    window.show();
    QTimer::singleShot(2000, &window, [&window] { window.close(); });
    return QApplication::exec();
}
