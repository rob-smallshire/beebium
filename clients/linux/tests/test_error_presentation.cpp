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

#include "app/ErrorPresentation.hpp"
#include "app/FilesystemSharing.hpp"

#include <QPlainTextEdit>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Connection failures are explained in full sentences", "[errors]") {
    for (auto failure : {ConnectionFailure::unreachable, ConnectionFailure::incompatible,
                         ConnectionFailure::error}) {
        const QString sentence = connection_failure_sentence(failure, QStringLiteral("pi:48875"));
        INFO(sentence.toStdString());
        CHECK(sentence.at(0).isUpper());
        CHECK(sentence.endsWith(QLatin1Char('.')));
        CHECK(sentence.contains(QStringLiteral("pi:48875")));
    }
}

TEST_CASE("The server's message is shown verbatim and whole", "[errors]") {
    const QString long_message =
        QStringLiteral("protocol fingerprint mismatch: ") + QString(400, QLatin1Char('x'));
    ServerMessageDialog dialog(QStringLiteral("Title"), QStringLiteral("A sentence."),
                               long_message);
    auto* shown = dialog.findChild<QPlainTextEdit*>(QStringLiteral("serverMessage"));
    REQUIRE(shown != nullptr);
    CHECK(shown->toPlainText() == long_message);
    CHECK(shown->isReadOnly());
}

TEST_CASE("Filesystem sharing needs matching fingerprints and no sandbox", "[sharing]") {
    CHECK(shares_filesystem(true, Sandbox::none));
    CHECK_FALSE(shares_filesystem(false, Sandbox::none));
    // A sandbox shares the host's identity, not its files: fail closed.
    CHECK_FALSE(shares_filesystem(true, Sandbox::flatpak));
    CHECK_FALSE(shares_filesystem(true, Sandbox::snap));
}
