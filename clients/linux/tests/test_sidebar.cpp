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

// The sidebar (client specification 13.1) and its Sound mode (8.2, 8.3),
// and how a machine window shows them, immersive included (6.4).

#include "app/ConnectionRecovery.hpp"
#include "app/DisplayFrame.hpp"
#include "app/DisplayWidget.hpp"
#include "app/ImmersiveExit.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/SessionAudio.hpp"
#include "app/Sidebar.hpp"
#include "app/SidebarMode.hpp"
#include "app/SoundSidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

std::shared_ptr<grpc::Channel> channel_to(const FakeServer& server) {
    return grpc::CreateChannel(server.endpoint().toStdString(), grpc::InsecureChannelCredentials());
}

QAction* action_named(QWidget& window, const QString& text) {
    for (QAction* action : window.actions()) {
        if (action->text().remove(QLatin1Char('&')) == text) {
            return action;
        }
    }
    return nullptr;
}

}  // namespace

// --- the shell ---------------------------------------------------------------------

TEST_CASE("A mode showing the server's state is marked stale while the server is not live",
          "[sidebar]") {
    CHECK_FALSE(stale_presentation(SidebarMode::sound, ConnectionVerdict::live).stale);
    const auto lost = stale_presentation(SidebarMode::sound, ConnectionVerdict::reconnecting);
    CHECK(lost.stale);
    CHECK(lost.title == QStringLiteral("Connection lost"));
    CHECK(stale_presentation(SidebarMode::storage, ConnectionVerdict::died).title ==
          QStringLiteral("Emulator stopped unexpectedly"));
    // This client's own settings are never stale.
    CHECK_FALSE(stale_presentation(SidebarMode::video, ConnectionVerdict::died).stale);
    CHECK_FALSE(stale_presentation(SidebarMode::keyboard, ConnectionVerdict::stopped).stale);
}

TEST_CASE("The sidebar's banner and dimmed content follow the verdict", "[sidebar]") {
    Sidebar sidebar;
    sidebar.add_mode(SidebarMode::sound, new QLabel(QStringLiteral("mixer")));
    sidebar.show();
    CHECK(sidebar.mode() == SidebarMode::sound);
    CHECK_FALSE(sidebar.showing_banner());
    CHECK(sidebar.content_enabled());
    sidebar.set_verdict(ConnectionVerdict::could_not_reconnect);
    CHECK(sidebar.showing_banner());
    CHECK_FALSE(sidebar.content_enabled());
    sidebar.set_verdict(ConnectionVerdict::live);
    CHECK_FALSE(sidebar.showing_banner());
    CHECK(sidebar.content_enabled());
}

// --- the Sound mode -----------------------------------------------------------------

TEST_CASE("The Sound mode shows each group the server reports, with its channels",
          "[sidebar][sound]") {
    FakeServer server;
    SessionAudio audio;
    SoundSidebar sound(audio);
    sound.show();
    CHECK(sound.group_sections() == 0);
    CHECK(sound.status_text() == QStringLiteral("Connecting..."));
    audio.start(channel_to(server));
    REQUIRE(wait_until([&] { return sound.group_sections() == 1; }));
    for (std::size_t c = 0; c < 4; ++c) {
        CHECK(sound.channel_slider(c) != nullptr);
    }
    // No sink in the tests: the mixer is shown under a line saying why.
    CHECK_FALSE(sound.status_text().isEmpty());
    audio.stop();
}

TEST_CASE("The Sound mode's controls set the mixer, and mutes grey their sliders",
          "[sidebar][sound]") {
    FakeServer server;
    SessionAudio audio;
    SoundSidebar sound(audio);
    sound.show();
    audio.start(channel_to(server));
    REQUIRE(wait_until([&] { return sound.group_sections() == 1; }));

    sound.channel_slider(3)->setValue(0);  // Noise
    CHECK(audio.controls().channel_volume[3].load() == 0.0f);
    sound.group_slider(0)->setValue(SoundSidebar::SLIDER_STEPS / 2);
    CHECK(audio.controls().group_volume[0].load() == 0.5f);

    sound.group_mute(0)->click();
    CHECK(audio.controls().group_muted[0].load());
    CHECK_FALSE(sound.group_slider(0)->isEnabled());
    CHECK(sound.master_slider()->isEnabled());

    sound.master_mute()->click();
    CHECK(audio.controls().muted.load());
    CHECK_FALSE(sound.master_slider()->isEnabled());
    CHECK_FALSE(sound.channel_slider(0)->isEnabled());
    audio.stop();
}

TEST_CASE("The health line appears only once a glitch has happened", "[sidebar][sound]") {
    FakeServer server;
    SessionAudio audio;
    SoundSidebar sound(audio);
    sound.show();
    audio.start(channel_to(server));
    REQUIRE(wait_until([&] { return sound.group_sections() == 1; }));
    REQUIRE(wait_until([&] { return server.audio.subscriptions == 1; }));
    CHECK_FALSE(sound.showing_health());
    server.audio.push(audio_chunk(0, 1024));
    server.audio.push(audio_chunk(1536, 1024));  // 512 never sent
    REQUIRE(wait_until([&] { return sound.showing_health(); }));
    CHECK(sound.health_line() == QStringLiteral("0 underruns, 512 dropped"));
    audio.stop();
}

// --- in a machine window --------------------------------------------------------------

namespace {

struct LiveWindow {
    FakeServer server;
    MainWindow window{QRhiWidget::Api::OpenGL};

    LiveWindow() {
        window.show();
        window.connect_to(server.endpoint());
        REQUIRE(wait_until([&] { return window.session().connected(); }));
        REQUIRE(wait_until([&] { return is_live(window.recovery().verdict()); }));
    }
    ~LiveWindow() {
        window.set_quitting(true);
        window.close();
    }
};

}  // namespace

TEST_CASE("A machine window shows the sidebar beside the picture, and Sound toggles it",
          "[sidebar][window]") {
    LiveWindow live;
    CHECK(live.window.sidebar().isVisible());
    CHECK(live.window.sidebar().mode() == SidebarMode::sound);
    QAction* sound = action_named(live.window, QStringLiteral("Sound"));
    REQUIRE(sound);
    CHECK(sound->shortcut() == QKeySequence(QStringLiteral("Ctrl+5")));
    // Pressed with Sound showing, it hides the sidebar; again, it shows it.
    sound->trigger();
    CHECK_FALSE(live.window.sidebar().isVisible());
    sound->trigger();
    CHECK(live.window.sidebar().isVisible());
    QAction* show_sidebar = action_named(live.window, QStringLiteral("Show Sidebar"));
    REQUIRE(show_sidebar);
    show_sidebar->trigger();
    CHECK_FALSE(live.window.sidebar().isVisible());
    show_sidebar->trigger();
    CHECK(live.window.sidebar().isVisible());
}

TEST_CASE("Immersive hides the sidebar; shown again it lies over the picture without "
          "reflowing it",
          "[sidebar][window][immersive]") {
    LiveWindow live;
    QAction* immersive = action_named(live.window, QStringLiteral("Enter Immersive Mode"));
    REQUIRE(immersive);
    immersive->trigger();
    REQUIRE(live.window.presentation() == Presentation::immersive);
    CHECK_FALSE(live.window.sidebar().isVisible());
    CHECK_FALSE(live.window.status_shown());

    auto* frame = live.window.findChild<DisplayFrame*>();
    REQUIRE(frame);
    QCoreApplication::processEvents();
    const QSize picture = live.window.display().size();
    auto* exit_bar = live.window.findChild<ImmersiveExit*>();
    auto* sidebar_toggle = exit_bar->findChild<QPushButton*>(QStringLiteral("immersiveSidebar"));
    auto* status_toggle = exit_bar->findChild<QPushButton*>(QStringLiteral("immersiveStatusBar"));
    REQUIRE(sidebar_toggle);
    REQUIRE(status_toggle);
    sidebar_toggle->click();
    status_toggle->click();
    QCoreApplication::processEvents();
    CHECK(live.window.sidebar().isVisible());
    CHECK(live.window.sidebar().parentWidget() == frame);
    CHECK(live.window.status_shown());
    CHECK(live.window.display().size() == picture);  // not reflowed
    CHECK(sidebar_toggle->isChecked());

    // Leaving puts both back where the window had them.
    immersive->trigger();
    CHECK(live.window.presentation() == Presentation::windowed);
    CHECK(live.window.sidebar().isVisible());
    CHECK(live.window.sidebar().parentWidget() != frame);
    CHECK(live.window.status_shown());
}

TEST_CASE("Losing the server marks the Sound mode stale", "[sidebar][window]") {
    LiveWindow live;
    CHECK_FALSE(live.window.sidebar().showing_banner());
    live.server.system.push(event(beebium::SERVER_STATUS_SHUTTING_DOWN));
    live.server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return !is_live(live.window.recovery().verdict()); }));
    CHECK(live.window.sidebar().showing_banner());
    CHECK_FALSE(live.window.sidebar().content_enabled());
}
