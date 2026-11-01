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

#include "app/ApplicationInfo.hpp"
#include "app/GraphicsApi.hpp"
#include "app/MainWindow.hpp"
#include "app/SystemPowerMonitor.hpp"
#include "app/SpikeMetrics.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <memory>

using namespace beebium::linux_client;

int main(int argc, char* argv[]) {
    // Answered before QApplication exists, so it works with no display: a
    // packaging script or a remote shell asking which build this is.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts(version_text().c_str());
            return 0;
        }
    }

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Beebium"));
    QApplication::setApplicationVersion(QString::fromLatin1(
        client_version().data(), static_cast<qsizetype>(client_version().size())));
    // The application ID: reverse DNS, as the macOS bundle identifier
    // (com.beebium.Beebium). Wayland compositors match the desktop entry by it.
    QApplication::setDesktopFileName(QStringLiteral("com.beebium.Beebium"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Beebium BBC Micro emulator client"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("version"), QStringLiteral("Show version information.")});
    const QCommandLineOption connect_option(
        QStringLiteral("connect"), QStringLiteral("Connect to the server at <host:port>."),
        QStringLiteral("host:port"));
    const QCommandLineOption api_option(
        QStringLiteral("graphics-api"),
        QStringLiteral("Draw with <api>: vulkan or opengl (OpenGL ES). Default: Vulkan, "
                       "but OpenGL ES on Wayland."),
        QStringLiteral("api"));
    const QCommandLineOption stats_option(
        QStringLiteral("stats-interval"),
        QStringLiteral("Print frame pacing and CPU use every <seconds>."),
        QStringLiteral("seconds"));
    const QCommandLineOption quit_option(
        QStringLiteral("quit-after"), QStringLiteral("Quit after <seconds>."),
        QStringLiteral("seconds"));
    parser.addOptions({connect_option, api_option, stats_option, quit_option});
    parser.process(application);

    const auto choice = choose_graphics_api(parser.value(api_option).toLower().toStdString(),
                                            QGuiApplication::platformName().toStdString());
    if (!choice) {
        std::fprintf(stderr, "beebium: unknown graphics API '%s'\n",
                     qPrintable(parser.value(api_option)));
        return 2;
    }
    std::fprintf(stderr, "beebium: graphics API %s, %s\n",
                 std::string(graphics_api_name(choice->api)).c_str(), choice->reason.c_str());
    QRhiWidget::Api api = QRhiWidget::Api::Vulkan;
    if (choice->api == GraphicsApi::opengl_es) {
        // OpenGL ES, the GPU's native API: Debian's Qt would otherwise ask Mesa
        // for desktop GL, a 3.1 subset on the Pi's V3D. Set before any window
        // exists, so before the first context.
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setVersion(3, 0);
        QSurfaceFormat::setDefaultFormat(format);
        api = QRhiWidget::Api::OpenGL;
    }

    // Full system sleep and wake, for every window's recovery (4.4).
    const auto power = SystemPowerMonitor::create_for_this_host();
    MainWindow window(api, power.get());
    window.show();

    std::unique_ptr<SpikeMetrics> metrics;
    if (parser.isSet(stats_option)) {
        metrics = std::make_unique<SpikeMetrics>(window, parser.value(stats_option).toInt());
    }
    if (parser.isSet(quit_option)) {
        QTimer::singleShot(parser.value(quit_option).toInt() * 1000, &window,
                           [&window] { window.close(); });
    }
    if (parser.isSet(connect_option)) {
        window.connect_to(parser.value(connect_option));
    }
    return QApplication::exec();
}
