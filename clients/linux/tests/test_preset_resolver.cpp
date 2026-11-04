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

// Asking the server about a draft (client specification 5.2): written to a
// file for resolve-preset --json, once edits pause, with only the latest
// draft's answer reported.

#include "app/PresetResolver.hpp"

#include "WaitUntil.hpp"

#include <QFile>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

struct Call {
    QString executable;
    QStringList arguments;
    QByteArray file;  // the draft as the server would read it
    CoreCli::Done done;
};

QByteArray answer(const char* severity) {
    return QByteArray("{\"preset\": {\"model\": \"model-b\"}, \"diagnostics\": [{\"severity\": \"") +
           severity + "\", \"path\": \"/speed\", \"location\": \"Speed\", \"message\": \"m\"}]}";
}

}  // namespace

TEST_CASE("A burst of edits is resolved once, from the last draft, after a pause",
          "[preset][resolver]") {
    std::vector<Call> calls;
    PresetResolver resolver([&](const QString& executable, const QStringList& arguments,
                                CoreCli::Done done) {
        QFile file(arguments.value(1));
        REQUIRE(file.open(QIODevice::ReadOnly));
        calls.push_back({executable, arguments, file.readAll(), std::move(done)});
    });
    resolver.set_executable(QStringLiteral("/opt/beebium/bin/beebium-model-b"));
    std::vector<PresetResolution> heard;
    QObject::connect(&resolver, &PresetResolver::resolved,
                     [&](const PresetResolution& r) { heard.push_back(r); });

    MachineDraft draft(ordered_json::parse(R"({"model": "model-b", "speed": 1.0})"));
    for (double speed : {2.0, 3.0, 4.0}) {
        draft.set("/speed", speed);
        resolver.resolve(draft);
    }
    CHECK(calls.empty());  // waiting for quiet
    REQUIRE(wait_until([&] { return calls.size() == 1; }));
    wait_until([] { return false; }, std::chrono::milliseconds(PresetResolver::DEBOUNCE_MS + 100));
    CHECK(calls.size() == 1);
    CHECK(calls[0].executable == QStringLiteral("/opt/beebium/bin/beebium-model-b"));
    CHECK(calls[0].arguments.value(0) == QStringLiteral("resolve-preset"));
    CHECK(calls[0].arguments.value(1).endsWith(QStringLiteral(".json")));
    CHECK(calls[0].arguments.value(2) == QStringLiteral("--json"));
    CHECK(ordered_json::parse(calls[0].file.toStdString())["speed"] == 4.0);

    // A non-zero exit still carries the answer.
    calls[0].done(CliResult{true, 78, answer("error"), QString()});
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].blocked());
    // The file is gone once answered.
    CHECK_FALSE(QFile::exists(calls[0].arguments.value(1)));
}

TEST_CASE("An answer to a superseded draft is never reported", "[preset][resolver]") {
    std::vector<Call> calls;
    PresetResolver resolver([&](const QString& e, const QStringList& a, CoreCli::Done d) {
        calls.push_back({e, a, {}, std::move(d)});
    });
    std::vector<PresetResolution> heard;
    QObject::connect(&resolver, &PresetResolver::resolved,
                     [&](const PresetResolution& r) { heard.push_back(r); });
    MachineDraft draft(ordered_json::parse(R"({"model": "model-b"})"));
    resolver.resolve(draft);
    REQUIRE(wait_until([&] { return calls.size() == 1; }));
    // A newer edit while the first is with the server.
    draft.set("/speed", 2.0);
    resolver.resolve(draft);
    calls[0].done(CliResult{true, 0, answer("warning"), QString()});
    CHECK(heard.empty());
    REQUIRE(wait_until([&] { return calls.size() == 2; }));
    calls[1].done(CliResult{true, 0, answer("warning"), QString()});
    REQUIRE(heard.size() == 1);
    CHECK_FALSE(heard[0].blocked());
}

TEST_CASE("A stored preset is resolved at once, and failures are said", "[preset][resolver]") {
    std::vector<Call> calls;
    PresetResolver resolver([&](const QString& e, const QStringList& a, CoreCli::Done d) {
        calls.push_back({e, a, {}, std::move(d)});
    });
    std::optional<QString> failure;
    QObject::connect(&resolver, &PresetResolver::failed,
                     [&](const QString& message) { failure = message; });
    resolver.resolve_preset(QStringLiteral("model-b-dfs"));
    REQUIRE(calls.size() == 1);
    CHECK(calls[0].arguments ==
          QStringList({QStringLiteral("resolve-preset"), QStringLiteral("model-b-dfs"),
                       QStringLiteral("--json")}));
    calls[0].done(CliResult{true, 66, QByteArray("not json"), QStringLiteral("no such preset\n")});
    REQUIRE(failure);
    CHECK(*failure == QStringLiteral("no such preset"));
    failure.reset();
    resolver.resolve_preset(QStringLiteral("x"));
    calls[1].done(CliResult{false, -1, {}, {}});
    REQUIRE(failure);
    CHECK(*failure == QStringLiteral("The Beebium server could not be run."));
}
