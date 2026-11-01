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

// Finding, querying and launching servers (client specification 5.1, 5.3,
// 5.5), against stand-in server executables.

#include "servers/CoreCli.hpp"
#include "servers/ServerLauncher.hpp"
#include "servers/ServerLocator.hpp"

#include "FakeBeebiumServer.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <set>

using namespace beebium::linux_client;
using beebium::linux_client::testing::wait_until;
using namespace std::chrono_literals;

namespace {

// Write an executable shell script.
QString script(const QTemporaryDir& directory, const QString& name, const QByteArray& body) {
    const QString filepath = directory.filePath(name);
    QFile file(filepath);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
    return filepath;
}

}  // namespace

TEST_CASE("Servers are found in settings, else on PATH, else in the default", "[servers]") {
    const std::set<QString> executables{QStringLiteral("/opt/beebium/bin/beebium-model-b"),
                                        QStringLiteral("/usr/bin/beebium-model-b"),
                                        QStringLiteral("/home/me/build/beebium-model-b")};
    const auto exists = [&](const QString& filepath) { return executables.count(filepath) != 0; };
    const QStringList path{QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin")};
    const QStringList fallback{QStringLiteral("/opt/beebium/bin")};

    auto found = locate_servers({}, path, fallback, exists);
    REQUIRE(found);
    CHECK(found->servers_dirpath == "/usr/bin");
    CHECK(found->found_by == "PATH");
    CHECK(found->executable_filepath(QStringLiteral("model-b-plus")) ==
          "/usr/bin/beebium-model-b-plus");

    found = locate_servers(QStringLiteral("/home/me/build"), path, fallback, exists);
    REQUIRE(found);
    CHECK(found->found_by == "settings");

    found = locate_servers({}, {QStringLiteral("/usr/local/bin")}, fallback, exists);
    REQUIRE(found);
    CHECK(found->found_by == "default");
}

TEST_CASE("A configured directory without servers is reported, not passed over",
          "[servers]") {
    const auto exists = [](const QString& filepath) {
        return filepath == QStringLiteral("/usr/bin/beebium-model-b");
    };
    CHECK_FALSE(locate_servers(QStringLiteral("/nowhere"), {QStringLiteral("/usr/bin")}, {}, exists)
                    .has_value());
    CHECK_FALSE(locate_servers({}, {}, {}, exists).has_value());
}

TEST_CASE("The preset listing parses, built-in apart from the user's", "[servers][presets]") {
    const QByteArray jsonl =
        R"({"id":"model-b","name":"Model B","source":"system","model":"model-b","description":"32K Model B, no disc","release_date":"1981-12","filepath":"/p/model-b.beebium-preset","thumbnail":true})"
        "\n"
        R"({"id":"mine","name":"Mine","source":"user","model":"model-b-plus","description":"","filepath":"/u/mine.beebium-preset","thumbnail":false})"
        "\nnot json\n";
    const auto presets = parse_preset_listing(jsonl);
    REQUIRE(presets.size() == 2);
    CHECK(presets[0].id == "model-b");
    CHECK(presets[0].built_in());
    CHECK(presets[0].has_thumbnail);
    CHECK(presets[0].description == "32K Model B, no disc");
    CHECK_FALSE(presets[1].built_in());
    CHECK_FALSE(presets[1].has_thumbnail);
    CHECK(presets[1].model == "model-b-plus");
}

TEST_CASE("A machine description parses, and a non-machine does not", "[servers][machines]") {
    const auto machine = parse_machine_description(
        R"({"machine_dirpath":"/m/Elite.beebium-machine","name":"Elite","uuid":"u-1","model":"model-b","created":"2026-11-01T10:00:00Z"})");
    REQUIRE(machine);
    CHECK(machine->name == "Elite");
    CHECK(machine->model == "model-b");
    CHECK_FALSE(parse_machine_description(R"({"error":"not a machine"})").has_value());
}

TEST_CASE("Launch arguments block the machine until Run and report an ephemeral port",
          "[servers][launch]") {
    const auto preset = preset_launch_arguments(QStringLiteral("/p/x.beebium-preset"), 2,
                                                QStringLiteral("uuid-1"), {});
    CHECK(preset.contains(QStringLiteral("--wait=api")));
    CHECK(preset.join(' ').contains(QStringLiteral("--port 0")));
    CHECK(preset.join(' ').contains(QStringLiteral("--machine-ordinal 2")));
    CHECK(preset.join(' ').contains(QStringLiteral("--provenance-type linux-gui")));
    CHECK(preset.join(' ').contains(QStringLiteral("--provenance-uuid uuid-1")));
    CHECK_FALSE(preset.contains(QStringLiteral("--rom-dir")));

    const auto machine = machine_launch_arguments(QStringLiteral("/m/E.beebium-machine"),
                                                  QStringLiteral("uuid-2"),
                                                  QStringLiteral("/roms"));
    CHECK(machine.join(' ').contains(QStringLiteral("--machine /m/E.beebium-machine")));
    CHECK_FALSE(machine.contains(QStringLiteral("--machine-ordinal")));
    CHECK(machine.join(' ').contains(QStringLiteral("--rom-dir /roms")));
}

TEST_CASE("The listening port is read from the server's output", "[servers][launch]") {
    CHECK(parse_listening_port("Starting gRPC server...\nListening on port 41234\n") == 41234);
    CHECK_FALSE(parse_listening_port("Starting gRPC server...\n").has_value());
}

TEST_CASE("A subcommand's output, and its failure in the server's own words",
          "[servers][cli]") {
    QTemporaryDir directory;
    const QString ok = script(directory, QStringLiteral("ok"), "echo '{\"id\":\"a\",\"model\":\"m\"}'\n");
    const QString bad = script(directory, QStringLiteral("bad"), "echo 'no such preset' >&2\nexit 2\n");
    CoreCli cli;
    std::optional<CliResult> first;
    std::optional<CliResult> second;
    std::optional<CliResult> missing;
    cli.run(ok, {}, [&](const CliResult& r) { first = r; });
    cli.run(bad, {}, [&](const CliResult& r) { second = r; });
    cli.run(directory.filePath(QStringLiteral("absent")), {},
            [&](const CliResult& r) { missing = r; });
    REQUIRE(wait_until([&] { return first && second && missing; }));
    CHECK(first->ok());
    CHECK(parse_preset_listing(first->standard_output).size() == 1);
    CHECK_FALSE(second->ok());
    CHECK(second->exit_code == 2);
    CHECK(second->standard_error == "no such preset");
    CHECK_FALSE(missing->started);
}

TEST_CASE("A launched server reports its port and outlives its launcher", "[servers][launch]") {
    QTemporaryDir directory;
    const QString server = script(directory, QStringLiteral("beebium-model-b"),
                                  "echo 'Starting gRPC server...'\n"
                                  "echo 'Listening on port 45678'\n"
                                  "exec sleep 30\n");
    std::optional<LaunchResult> result;
    {
        ServerLauncher launcher(directory.filePath(QStringLiteral("logs")));
        launcher.launch(server, {}, QStringLiteral("uuid-3"),
                        [&](const LaunchResult& r) { result = r; });
        REQUIRE(wait_until([&] { return result.has_value(); }));
    }
    const auto* launched = std::get_if<LaunchedServer>(&*result);
    REQUIRE(launched != nullptr);
    CHECK(launched->port == 45678);
    CHECK(launched->provenance_uuid == "uuid-3");
    // Detached: still running with the launcher gone.
    CHECK(process_alive(launched->pid));
    terminate_process(launched->pid);
    CHECK(wait_until([&] { return !process_alive(launched->pid); }));
}

TEST_CASE("A server that fails to start reports what it said", "[servers][launch]") {
    QTemporaryDir directory;
    const QString server = script(directory, QStringLiteral("beebium-model-b"),
                                  "echo 'error: ROM not found: acorn-mos_1_20.rom' >&2\nexit 1\n");
    ServerLauncher launcher(directory.filePath(QStringLiteral("logs")));
    std::optional<LaunchResult> result;
    launcher.launch(server, {}, QStringLiteral("uuid-4"),
                    [&](const LaunchResult& r) { result = r; });
    REQUIRE(wait_until([&] { return result.has_value(); }));
    const auto* failure = std::get_if<LaunchFailure>(&*result);
    REQUIRE(failure != nullptr);
    CHECK(failure->server_message == "error: ROM not found: acorn-mos_1_20.rom");
    CHECK_FALSE(failure->timed_out);
}

TEST_CASE("A server that never listens is stopped after the timeout", "[servers][launch]") {
    QTemporaryDir directory;
    const QString server = script(directory, QStringLiteral("beebium-model-b"), "exec sleep 30\n");
    ServerLauncher launcher(directory.filePath(QStringLiteral("logs")));
    std::optional<LaunchResult> result;
    launcher.launch(server, {}, QStringLiteral("uuid-5"),
                    [&](const LaunchResult& r) { result = r; }, 500);
    REQUIRE(wait_until([&] { return result.has_value(); }));
    const auto* failure = std::get_if<LaunchFailure>(&*result);
    REQUIRE(failure != nullptr);
    CHECK(failure->timed_out);
}

TEST_CASE("Old server logs beyond the newest twenty are pruned", "[servers][logs]") {
    QTemporaryDir directory;
    const QDateTime now = QDateTime::currentDateTime();
    for (int i = 0; i < 25; ++i) {
        QFile log(directory.filePath(QStringLiteral("%1.log").arg(i)));
        REQUIRE(log.open(QIODevice::WriteOnly));
        log.close();
        // Logs 0..4 are recent; 5..24 are older, each a day further back.
        const QDateTime when = i < 5 ? now.addSecs(-i * 60) : now.addDays(-(i + 3));
        REQUIRE(log.open(QIODevice::Append));
        REQUIRE(log.setFileTime(when, QFileDevice::FileModificationTime));
        log.close();
    }
    // Newest twenty kept whatever their age; of the other five, those older
    // than a week go (all of them here).
    CHECK(prune_server_logs(directory.path(), now) == 5);
    CHECK(QDir(directory.path()).entryList({QStringLiteral("*.log")}, QDir::Files).size() == 20);
    CHECK(prune_server_logs(directory.path(), now) == 0);
}

TEST_CASE("Recent logs are kept even beyond twenty", "[servers][logs]") {
    QTemporaryDir directory;
    for (int i = 0; i < 25; ++i) {
        QFile log(directory.filePath(QStringLiteral("%1.log").arg(i)));
        REQUIRE(log.open(QIODevice::WriteOnly));
    }
    CHECK(prune_server_logs(directory.path(), QDateTime::currentDateTime()) == 0);
}
