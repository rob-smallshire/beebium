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

// Catch2's main, preceded by the QApplication that widget tests need.
//
// The platform defaults to offscreen when QT_QPA_PLATFORM is unset. ctest sets
// it, but catch_discover_tests runs the executable at build time to list its
// cases without the test's environment, and with no display QApplication would
// abort there. An explicit QT_QPA_PLATFORM still wins, for running a test
// against a real display by hand.

#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtGlobal>

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // Settings and other standard locations go to a test area, never the
    // user's own configuration; and to this process's own, since ctest runs
    // each case as its own process, in parallel, and cases that set the
    // client's settings must not see each other's.
    QTemporaryDir home;
    if (home.isValid()) {
        qputenv("HOME", home.path().toLocal8Bit());
    }
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
