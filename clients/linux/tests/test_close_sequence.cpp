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

// The close sequence of client specification 4.5, driven through a machine
// window against an in-process server, with its questions answered by the
// test.

#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"

#include "FakeBeebiumServer.hpp"

#include <QElapsedTimer>
#include <QPointer>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

struct Answers {
    int shared_asked = 0;
    int unsaved_asked = 0;
    ClosePrompts::Shared shared = ClosePrompts::Shared::cancel;
    ClosePrompts::Unsaved unsaved = ClosePrompts::Unsaved::cancel;

    ClosePrompts prompts() {
        ClosePrompts prompts;
        prompts.shared = [this](QWidget*, const QString&) {
            ++shared_asked;
            return shared;
        };
        prompts.unsaved = [this](QWidget*, const QString&) {
            ++unsaved_asked;
            return unsaved;
        };
        return prompts;
    }
};

// A window on the fake server as if this client had launched it.
MainWindow* launched_window(FakeServer& server, Answers& answers, qint64 pid = 0) {
    auto* window = new MainWindow(QRhiWidget::Api::OpenGL);
    window->set_close_prompts(answers.prompts());
    window->session().set_launched_server(
        LaunchedServer{pid, server.port, QStringLiteral("prov-close"), {}});
    window->show();
    window->connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return window->session().connected(); }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    return window;
}

}  // namespace

TEST_CASE("A cancelled quit leaves later closes ordinary closes", "[close][quit]") {
    FakeServer server;
    Answers answers;
    QPointer<MainWindow> window = launched_window(server, answers);

    // Unsaved media, sole client: quitting asks to save, and the user cancels.
    auto written = event(beebium::SERVER_STATUS_HEARTBEAT);
    written.set_template_media_written(true);
    server.system.push(written);
    REQUIRE(wait_until([&] { return window->session().template_media_written(); }));
    window->set_quitting(true);
    window->close();
    REQUIRE(wait_until([&] { return answers.unsaved_asked == 1; }));
    wait_until([] { return false; }, 100ms);
    REQUIRE(window);
    CHECK(window->isVisible());

    // Another client arrives; closing from the title bar is not a quit, so a
    // shared server this client launched is asked about.
    server.system.client_count = 2;
    window->close();
    REQUIRE(wait_until([&] { return answers.shared_asked == 1; }));
    CHECK(window->isVisible());

    answers.shared = ClosePrompts::Shared::leave_running;
    window->close();
    REQUIRE(wait_until([&] { return window.isNull(); }, 5000ms));
}
