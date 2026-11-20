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

// A draft's MOS ROM against a real Master 128 (client specification 5.2,
// 5.8): the configurator's MOS field titled by the server's describe-rom,
// and the preset as resolve-preset gives it, its MOS changed as the
// configurator changes it, written and started as Create starts it,
// running the MOS chosen.

#include "app/SessionTyping.hpp"
#include "app/configuration/MemorySection.hpp"
#include "servers/CoreCli.hpp"
#include "preset/MachineDraft.hpp"

#include "ConfigurationFixture.hpp"
#include "RealMaster.hpp"

#include "debugger.grpc.pb.h"
#include "video.grpc.pb.h"

#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QPushButton>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

// resolve-preset's document for a preset, as the dialog's draft starts.
std::optional<ordered_json> resolved(const QString& executable_filepath, const QString& preset) {
    QProcess process;
    process.start(executable_filepath, {QStringLiteral("resolve-preset"), preset, QStringLiteral("--json")});
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        return std::nullopt;
    }
    const auto answer = ordered_json::parse(process.readAllStandardOutput().toStdString(), nullptr, false);
    if (answer.is_discarded() || !answer.contains("preset") || !answer["preset"].is_object()) {
        return std::nullopt;
    }
    return answer["preset"];
}

// The server's own output of a subcommand, as the client runs it.
QByteArray run(const QString& executable_filepath, const QStringList& arguments) {
    QProcess process;
    process.start(executable_filepath, arguments);
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
}

std::string screen_text(beebium::VideoService::Stub& video) {
    grpc::ClientContext context;
    beebium::GetTeletextScreenRequest request;
    beebium::TeletextScreen screen;
    if (!video.GetTeletextScreen(&context, request, &screen).ok() || !screen.active()) {
        return {};
    }
    return screen.text();
}

}  // namespace

TEST_CASE("The configurator titles a Master 128's MOS from the server, and an unknown image by its name",
          "[configuration][memory][mos][real-server]") {
    const QString executable_filepath = RealMaster::executable_filepath();
    if (executable_filepath.isEmpty()) {
        SKIP("No beebium-master-128 beside BEEBIUM_TEST_SERVER_FILEPATH to run.");
    }
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString unknown_filepath = dir.filePath(QStringLiteral("homebrew-mos.rom"));
    {
        QFile file(unknown_filepath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(16384, '\xff'));
    }
    const auto document = resolved(executable_filepath, QStringLiteral("master-128"));
    REQUIRE(document);
    const std::string mos = document->value("mos_image_uri", std::string());
    REQUIRE(mos == "acorn-mos_3_20.rom");
    // The dialog's describe-rom and schema answers, as the server gives them.
    ConfigurationFixture f(*document, [&](ConfigurationFixture& fixture) {
        fixture.schema_text = run(executable_filepath, {QStringLiteral("describe-preset-schema")}).toStdString();
        for (const QString& rom : {QString::fromStdString(mos), unknown_filepath}) {
            const auto described = parse_rom_description(run(executable_filepath, {QStringLiteral("describe-rom"), rom}));
            REQUIRE(described);
            fixture.roms[rom] = *described;
        }
    });
    auto* memory = f.section<MemorySection>();
    REQUIRE(wait_until([&] { return memory->mos_title()->text() == QStringLiteral("Acorn MOS 3.20"); }));
    CHECK(memory->mos_caption()->isVisibleTo(memory));
    CHECK(memory->mos_caption()->text() == QStringLiteral("acorn-mos_3_20.rom"));

    f.answer = unknown_filepath;
    memory->mos_browse()->click();
    REQUIRE(wait_until([&] { return memory->mos_title()->text() == QStringLiteral("homebrew-mos.rom"); }));
    CHECK_FALSE(memory->mos_caption()->isVisibleTo(memory));
}

TEST_CASE("A draft whose MOS ROM is MOS 3.50 starts a Master 128 running MOS 3.50",
          "[configuration][mos][real-server]") {
    const QString executable_filepath = RealMaster::executable_filepath();
    if (executable_filepath.isEmpty()) {
        SKIP("No beebium-master-128 beside BEEBIUM_TEST_SERVER_FILEPATH to run.");
    }
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const auto document = resolved(executable_filepath, QStringLiteral("master-128"));
    REQUIRE(document);
    MachineDraft draft(*document);
    REQUIRE(draft.get("/mos_image_uri") == ordered_json("acorn-mos_3_20.rom"));
    draft.set("/mos_image_uri", "acorn-mos_3_50.rom");
    CHECK(draft.changed("/mos_image_uri"));

    const QString preset_filepath = dir.filePath(QStringLiteral("draft.json"));
    {
        QFile file(preset_filepath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromStdString(draft.serialise()));
    }
    RealMaster master;
    master.start(preset_filepath);
    const auto channel = master.channel();
    {
        auto debugger = beebium::DebuggerControl::NewStub(channel);
        grpc::ClientContext context;
        beebium::RunResponse response;
        REQUIRE(debugger->Run(&context, beebium::RunRequest{}, &response).ok());
        REQUIRE(response.success());
    }

    SessionTyping typing;
    typing.start(channel);
    REQUIRE(wait_until([&] { return typing.guest_ready(); }, 30s));
    std::optional<PasteOutcome> outcome;
    QObject::connect(&typing, &SessionTyping::paste_finished, [&](const PasteOutcome& o) { outcome = o; });
    typing.paste(QStringLiteral("*HELP\n"));
    REQUIRE(wait_until([&] { return outcome.has_value(); }, 30s));
    REQUIRE(outcome->result == PasteOutcome::Result::typed);

    auto video = beebium::VideoService::NewStub(channel);
    std::string text;
    const bool shown = wait_until(
        [&] {
            text = screen_text(*video);
            return text.find("MOS 3.50") != std::string::npos;
        },
        10s);
    INFO(text);
    CHECK(shown);
    CHECK(text.find("MOS 3.20") == std::string::npos);
    typing.stop();
}
