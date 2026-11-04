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
#include "connection/RpcWorker.hpp"

#include "FakeBeebiumServer.hpp"

#include <QElapsedTimer>
#include <QPointer>
#include <QToolButton>
#include <QProcess>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

struct Answers {
    int shared_asked = 0;
    int unsaved_asked = 0;
    int unsaved_remote_asked = 0;
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
        prompts.unsaved_remote = [this](QWidget*, const QString&) {
            ++unsaved_remote_asked;
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

TEST_CASE("Unsaved changes on a server on another host can only be lost, never saved",
          "[close][quit][gating]") {
    FakeServer server;
    server.system.host_fingerprint = "somewhere-else";
    Answers answers;
    QPointer<MainWindow> window = launched_window(server, answers);
    auto written = event(beebium::SERVER_STATUS_HEARTBEAT);
    written.set_template_media_written(true);
    server.system.push(written);
    REQUIRE(wait_until([&] { return window->session().template_media_written(); }));

    // Asked the question with no Save in it (#263); cancelling keeps it.
    answers.unsaved = ClosePrompts::Unsaved::cancel;
    window->set_quitting(true);
    window->close();
    REQUIRE(wait_until([&] { return answers.unsaved_remote_asked == 1; }));
    CHECK(answers.unsaved_asked == 0);
    wait_until([] { return false; }, 100ms);
    REQUIRE(window);
    CHECK(window->isVisible());

    // Closing anyway shuts it down unsaved.
    answers.unsaved = ClosePrompts::Unsaved::discard;
    window->set_quitting(true);
    window->close();
    REQUIRE(wait_until([&] { return window.isNull(); }, 5000ms));
    std::lock_guard lock(server.system.calls_mutex);
    CHECK(server.system.shutdown_requests == 1);
    CHECK(server.system.save_requests == 0);
}

TEST_CASE("The link control shows only for a machine this client launched, and toggles",
          "[close][link]") {
    FakeServer server;
    {
        // Connected to, not launched: nothing to link.
        MainWindow window(QRhiWidget::Api::OpenGL);
        window.show();
        window.connect_to(server.endpoint());
        REQUIRE(wait_until([&] { return window.session().connected(); }));
        auto* link = window.findChild<QToolButton*>(QStringLiteral("linkControl"));
        REQUIRE(link);
        CHECK_FALSE(link->isVisibleTo(&window));
        window.set_quitting(true);
        window.close();
    }
    Answers answers;
    QPointer<MainWindow> window = launched_window(server, answers);
    auto* link = window->findChild<QToolButton*>(QStringLiteral("linkControl"));
    REQUIRE(link);
    CHECK(link->isVisibleTo(window));
    CHECK(link->toolTip() ==
          QStringLiteral("Linked: closing this window will shut down the machine. Click to unlink."));
    link->click();
    CHECK(link->toolTip() == QStringLiteral(
                                 "Unlinked: the machine will keep running when this window closes. "
                                 "Click to re-link."));
    link->click();
    CHECK(link->toolTip().startsWith(QStringLiteral("Linked:")));
    window->set_quitting(true);
    window->close();
    REQUIRE(wait_until([&] { return window.isNull(); }, 10000ms));
}

TEST_CASE("An unlinked machine is released and left running when its window closes",
          "[close][link]") {
    FakeServer server;
    Answers answers;
    QPointer<MainWindow> window = launched_window(server, answers);
    window->findChild<QToolButton*>(QStringLiteral("linkControl"))->click();
    window->close();
    REQUIRE(wait_until([&] { return window.isNull(); }, 5000ms));
    std::lock_guard lock(server.system.calls_mutex);
    CHECK(server.system.release_requests == 1);
    CHECK(server.system.release_authority == "prov-close");
    CHECK(server.system.shutdown_requests == 0);
    CHECK(answers.unsaved_asked == 0);
    CHECK(answers.shared_asked == 0);
}

namespace {

std::chrono::milliseconds elapsed_since(const QElapsedTimer& timer) {
    return std::chrono::milliseconds(timer.elapsed());
}

}  // namespace

TEST_CASE("Stopping the worker cancels a stalled call at once", "[close][stall]") {
    FakeServer server;
    server.system.stall_system_info = true;
    auto channel = grpc::CreateChannel(server.endpoint().toStdString(),
                                       grpc::InsecureChannelCredentials());
    RpcWorker worker;
    std::atomic<bool> returned{false};
    worker.post([&](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + 10s);
        beebium::SystemInfo info;
        beebium::SystemService::NewStub(channel)->GetSystemInfo(&context, {}, &info);
        returned = true;
    });
    REQUIRE(wait_until([&] { return server.system.stalled_calls == 1; }));
    QElapsedTimer timer;
    timer.start();
    worker.stop();
    CHECK(elapsed_since(timer) < 1000ms);
    CHECK(returned);
    server.system.stall_system_info = false;
}

TEST_CASE("A disconnect does not wait for a stalled call", "[close][stall]") {
    FakeServer server;
    MachineSession session;
    bool connected = false;
    QObject::connect(&session, &MachineSession::connected_to_server, [&] { connected = true; });
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return connected; }));
    server.system.stall_system_info = true;
    session.refresh_system_info();
    REQUIRE(wait_until([&] { return server.system.stalled_calls == 1; }));
    QElapsedTimer timer;
    timer.start();
    session.disconnect_from_server();
    CHECK(elapsed_since(timer) < 1000ms);
    server.system.stall_system_info = false;
}

TEST_CASE("A disconnect does not wait for a stalled handshake", "[close][stall]") {
    FakeServer server;
    server.system.stall_system_info = true;
    MachineSession session;
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return server.system.stalled_calls == 1; }));
    QElapsedTimer timer;
    timer.start();
    session.disconnect_from_server();
    CHECK(elapsed_since(timer) < 1000ms);
    server.system.stall_system_info = false;
}

TEST_CASE("Closing while the server stalls leaves the GUI responsive and the window goes",
          "[close][stall]") {
    FakeServer server;
    Answers answers;
    QPointer<MainWindow> window = launched_window(server, answers);
    server.system.stall_system_info = true;

    // The event loop's longest pause while the close runs.
    QElapsedTimer since_tick;
    since_tick.start();
    qint64 longest_pause = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        longest_pause = std::max(longest_pause, since_tick.restart());
    });
    ticker.start(50);

    QElapsedTimer closing;
    closing.start();
    window->close();
    REQUIRE(wait_until([&] { return window.isNull(); }, 6000ms));
    CHECK(elapsed_since(closing) < 4500ms);
    CHECK(longest_pause < 1000);
    // No answer, so the server was left running rather than shut down.
    CHECK(server.system.shutdown_requests == 0);
    server.system.stall_system_info = false;
}

TEST_CASE("A launched server that is not there to ask is signalled if its process lives",
          "[close][hung]") {
    // A stand-in for the server's process: a child whose pid the window
    // believes it launched.
    QProcess hung;
    hung.start(QStringLiteral("sleep"), {QStringLiteral("60")});
    REQUIRE(hung.waitForStarted());

    FakeServer server;
    Answers answers;
    QPointer<MainWindow> window = launched_window(server, answers, hung.processId());
    // The heartbeats stop: the window reads unreachable.
    wait_until([] { return false; }, 100ms);
    window->session().tick_liveness(LivenessTracker::Clock::now() + 3s);
    REQUIRE(window->session().liveness() == Liveness::unreachable);

    window->close();
    REQUIRE(wait_until([&] { return hung.state() == QProcess::NotRunning; }, 5000ms));
    CHECK(hung.exitStatus() == QProcess::CrashExit);  // ended by a signal
    REQUIRE(wait_until([&] { return window.isNull(); }, 5000ms));
    CHECK(server.system.shutdown_requests == 0);
}

TEST_CASE("A launched server whose process has gone is simply left", "[close][hung]") {
    FakeServer server;
    Answers answers;
    // A pid that is not running.
    QPointer<MainWindow> window = launched_window(server, answers, 999999);
    wait_until([] { return false; }, 100ms);
    window->session().tick_liveness(LivenessTracker::Clock::now() + 3s);
    window->close();
    REQUIRE(wait_until([&] { return window.isNull(); }, 2000ms));
}
