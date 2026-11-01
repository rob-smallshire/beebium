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

#include <QApplication>
#include <QCommandLineParser>

#include <cstdio>
#include <cstring>

int main(int argc, char* argv[]) {
    // Answered before QApplication exists, so it works with no display: a
    // packaging script or a remote shell asking which build this is.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::puts(beebium::linux_client::version_text().c_str());
            return 0;
        }
    }

    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Beebium"));
    QApplication::setApplicationVersion(
        QString::fromUtf8(beebium::linux_client::client_version().data(),
                          static_cast<qsizetype>(beebium::linux_client::client_version().size())));
    QApplication::setDesktopFileName(QStringLiteral("beebium"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Beebium BBC Micro emulator client"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringLiteral("version"),
                                        QStringLiteral("Show version information.")));
    parser.process(application);

    beebium::linux_client::MainWindow window;
    window.show();
    return QApplication::exec();
}
