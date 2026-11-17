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

// New Blank Tape, Record and SAVE against a real machine (client
// specification 9.0, The Record key; 9.0.1, Templates are never tapes):
// the recording lands in the tape the client created, never in the
// server's template, and the recent tapes hold the created tape.

#include "app/CassetteDeckRow.hpp"
#include "app/ClientSettings.hpp"
#include "app/SessionCassette.hpp"
#include "app/SessionDiscs.hpp"
#include "app/SessionTyping.hpp"

#include "RealServer.hpp"
#include "WaitUntil.hpp"

#include "debugger.grpc.pb.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

QByteArray contents(const QString& filepath) {
    QFile file(filepath);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

std::map<QString, QByteArray> files_in(const QString& dirpath) {
    std::map<QString, QByteArray> files;
    for (const QFileInfo& info : QDir(dirpath).entryInfoList(QDir::Files)) {
        files[info.fileName()] = contents(info.absoluteFilePath());
    }
    return files;
}

// A real machine with the client's deck on it, as a window has: the
// cassette session, the deck row with its settings, and the typing path.
struct Machine {
    RealServer server;
    QTemporaryDir dir;
    SessionCassette cassette;
    SessionTyping typing;
    ClientSettings settings{dir.filePath(QStringLiteral("Beebium.conf"))};
    CassetteDeckRow row{cassette, &settings, [] { return true; }};
    std::unique_ptr<beebium::DebuggerControl::Stub> debugger;

    // False, and the case skips, when there is no server to run.
    bool start() {
        if (!RealServer::available()) {
            return false;
        }
        REQUIRE(dir.isValid());
        const bool started = server.start();
        INFO(server.output().toStdString());
        REQUIRE(started);
        cassette.start(server.channel());
        typing.start(server.channel());
        debugger = beebium::DebuggerControl::NewStub(server.channel());
        REQUIRE(wait_until([&] { return cassette.deck().known && cassette.deck().has_interface; }, 10s));
        // BASIC's prompt: the machine wants input.
        REQUIRE(wait_until([&] { return typing.guest_ready(); }, 30s));
        return true;
    }
    ~Machine() {
        cassette.stop();
        typing.stop();
    }

    void type(const QString& text) {
        std::optional<PasteOutcome> outcome;
        auto connection = QObject::connect(&typing, &SessionTyping::paste_finished,
                                           [&](const PasteOutcome& o) { outcome = o; });
        typing.paste(text);
        REQUIRE(wait_until([&] { return outcome.has_value(); }, 30s));
        QObject::disconnect(connection);
        REQUIRE(outcome->result == PasteOutcome::Result::typed);
    }
    std::optional<std::uint8_t> peek(std::uint32_t address) {
        grpc::ClientContext context;
        beebium::ReadMemoryRequest request;
        request.set_address(address);
        request.set_length(1);
        beebium::ReadMemoryResponse response;
        if (!debugger->ReadMemory(&context, request, &response).ok() || response.data().empty()) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(response.data()[0]);
    }
    void poke(std::uint32_t address, std::uint8_t value) {
        grpc::ClientContext context;
        beebium::WriteMemoryRequest request;
        request.set_address(address);
        request.set_data(std::string(1, static_cast<char>(value)));
        beebium::WriteMemoryResponse response;
        REQUIRE(debugger->WriteMemory(&context, request, &response).ok());
    }
    QString tape_filepath() const { return path_for_disc_url(cassette.deck().tape.url); }
    void click(CassetteKey key) {
        REQUIRE(row.key(key)->isEnabled());
        row.key(key)->click();
    }
    // The MOS has finished with the tape: the relay closed for the
    // transfer and has opened again, the tape moved.
    void wait_for_transfer() {
        REQUIRE(wait_until([&] { return cassette.deck().motor_on; }, 30s));
        REQUIRE(wait_until([&] { return !cassette.deck().motor_on && cassette.deck().position_seconds > 0.0; },
                           90s));
    }
};

struct Recorded {
    QString created_filepath;
    QString template_filepath;
    std::map<QString, QByteArray> templates_before;
};

// New Blank Tape as the deck makes it, the name chosen in its save dialog,
// then Record and SAVE "CASS" typed as the user types it, then Stop.
Recorded record_cass(Machine& m) {
    REQUIRE(wait_until([&] { return !m.cassette.blank_tapes().empty(); }, 10s));
    Recorded recorded;
    const QString templates_dirpath = RealServer::blank_tapes_dirpath();
    recorded.templates_before = files_in(templates_dirpath);
    REQUIRE_FALSE(recorded.templates_before.empty());
    const BlankTapeTemplate blank = m.cassette.blank_tapes().front();
    recorded.template_filepath = QDir(templates_dirpath).filePath(blank.filename);
    REQUIRE(recorded.templates_before.count(blank.filename) == 1);

    // What the save dialog hands the deck: a new file of the user's.
    const QString chosen = m.dir.filePath(QStringLiteral("tapes/cass") + blank.extension());
    REQUIRE(QDir().mkpath(QFileInfo(chosen).absolutePath()));
    m.cassette.create_tape(blank.label, chosen);
    // The deck inserts what was made, stopped at the start.
    REQUIRE(wait_until([&] { return m.cassette.deck().tape_inserted; }, 10s));
    recorded.created_filepath = QFileInfo(chosen).absoluteFilePath();
    REQUIRE(m.tape_filepath() == recorded.created_filepath);
    CHECK(contents(recorded.created_filepath) == recorded.templates_before.at(blank.filename));

    m.type(QStringLiteral("NEW\n10 ?&70=42\n*TAPE\nSAVE \"CASS\"\n"));
    // "RECORD then RETURN".
    m.click(CassetteKey::record);
    REQUIRE(wait_until([&] { return m.cassette.deck().transport == DeckState::Transport::recording; }));
    m.type(QStringLiteral("\n"));
    m.wait_for_transfer();
    m.click(CassetteKey::stop);
    REQUIRE(wait_until([&] { return m.cassette.deck().transport == DeckState::Transport::stopped; }));
    return recorded;
}

}  // namespace

TEST_CASE("A SAVE recorded on a new blank tape lands in that tape, never its template",
          "[cassette][recording][real-server]") {
    Machine m;
    if (!m.start()) {
        SKIP("No beebium-model-b to run: set BEEBIUM_TEST_SERVER_FILEPATH to one.");
    }
    const Recorded recorded = record_cass(m);

    CHECK(m.tape_filepath() == recorded.created_filepath);
    // The recording is in the new tape: its header block names CASS.
    const QByteArray tape = contents(recorded.created_filepath);
    CHECK(tape != recorded.templates_before.at(QFileInfo(recorded.template_filepath).fileName()));
    CHECK(tape.contains(QByteArray("\x2a" "CASS", 5) + QByteArray(1, '\0')));
    // Every template is as it was.
    CHECK(files_in(RealServer::blank_tapes_dirpath()) == recorded.templates_before);
    // The recent tapes hold the tape the user made, and no template.
    const QStringList recent = m.settings.recent_tapes();
    CHECK(recent.contains(recorded.created_filepath));
    for (const QString& path : recent) {
        CHECK_FALSE(QFileInfo(path).absolutePath() ==
                    QFileInfo(RealServer::blank_tapes_dirpath()).absoluteFilePath());
    }
}

TEST_CASE("A tape recorded on is offered among the recent tapes and CHAIN loads from it",
          "[cassette][recording][real-server]") {
    Machine m;
    if (!m.start()) {
        SKIP("No beebium-model-b to run: set BEEBIUM_TEST_SERVER_FILEPATH to one.");
    }
    const Recorded recorded = record_cass(m);
    m.click(CassetteKey::eject);
    REQUIRE(wait_until([&] { return !m.cassette.deck().tape_inserted; }, 10s));
    m.poke(0x70, 0);
    REQUIRE(m.peek(0x70) == std::uint8_t{0});

    // Picked from the deck's Recent menu.
    Q_EMIT m.row.browse_menu()->aboutToShow();
    QMenu* recent = nullptr;
    for (QAction* action : m.row.browse_menu()->actions()) {
        if (action->text() == QStringLiteral("Recent")) {
            recent = action->menu();
        }
    }
    REQUIRE(recent);
    QAction* pick = nullptr;
    for (QAction* action : recent->actions()) {
        if (action->toolTip() == recorded.created_filepath) {
            pick = action;
        }
    }
    REQUIRE(pick);
    REQUIRE(pick->isEnabled());
    pick->trigger();
    REQUIRE(wait_until([&] { return m.cassette.deck().tape_inserted; }, 10s));
    CHECK(m.tape_filepath() == recorded.created_filepath);

    m.type(QStringLiteral("NEW\nCHAIN \"\"\n"));
    m.click(CassetteKey::play);
    m.wait_for_transfer();
    // The program ran: it left its mark.
    CHECK(wait_until([&] { return m.peek(0x70) == std::uint8_t{42}; }, 10s));
}
