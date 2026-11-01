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
#include "app/MainWindow.hpp"
#include "app/SpikeMetrics.hpp"

#include <QApplication>
#include <QCommandLineParser>
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
    QApplication::setDesktopFileName(QStringLiteral("beebium"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Beebium BBC Micro emulator client"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("version"), QStringLiteral("Show version information.")});
    const QCommandLineOption connect_option(
        QStringLiteral("connect"), QStringLiteral("Connect to the server at <host:port>."),
        QStringLiteral("host:port"));
    const QCommandLineOption api_option(
        QStringLiteral("graphics-api"),
        QStringLiteral("Draw with <api>: vulkan (default) or opengl."), QStringLiteral("api"),
        QStringLiteral("vulkan"));
    const QCommandLineOption stats_option(
        QStringLiteral("stats-interval"),
        QStringLiteral("Print frame pacing and CPU use every <seconds>."),
        QStringLiteral("seconds"));
    const QCommandLineOption quit_option(
        QStringLiteral("quit-after"), QStringLiteral("Quit after <seconds>."),
        QStringLiteral("seconds"));
    parser.addOptions({connect_option, api_option, stats_option, quit_option});
    parser.process(application);

    const QString api_name = parser.value(api_option).toLower();
    QRhiWidget::Api api = QRhiWidget::Api::Vulkan;
    if (api_name == QStringLiteral("opengl") || api_name == QStringLiteral("gles")) {
        api = QRhiWidget::Api::OpenGL;
    } else if (api_name != QStringLiteral("vulkan")) {
        std::fprintf(stderr, "beebium: unknown graphics API '%s'\n", qPrintable(api_name));
        return 2;
    }

    MainWindow window(api);
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
