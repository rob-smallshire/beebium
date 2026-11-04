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

#include "app/PresetResolver.hpp"

#include <QFile>
#include <QPointer>

namespace beebium::linux_client {

PresetResolver::PresetResolver(QObject* parent) : PresetResolver(Runner{}, parent) {}

PresetResolver::PresetResolver(Runner runner, QObject* parent)
    : QObject(parent), runner_(std::move(runner)) {
    if (!runner_) {
        cli_ = std::make_unique<CoreCli>();
        runner_ = [cli = cli_.get()](const QString& executable, const QStringList& arguments,
                                     CoreCli::Done done) {
            cli->run(executable, arguments, std::move(done));
        };
    }
    debounce_.setSingleShot(true);
    debounce_.setInterval(DEBOUNCE_MS);
    connect(&debounce_, &QTimer::timeout, this, [this] {
        // The draft goes as a file the server reads as a preset.
        const QString filepath =
            directory_.filePath(QStringLiteral("draft-%1.json").arg(++files_));
        QFile file(filepath);
        if (!file.open(QIODevice::WriteOnly)) {
            Q_EMIT failed(tr("The draft could not be written for the server to check."));
            return;
        }
        file.write(QByteArray::fromStdString(pending_.serialise()));
        file.close();
        run(filepath, [filepath] { QFile::remove(filepath); });
    });
}

PresetResolver::~PresetResolver() = default;

void PresetResolver::resolve(const MachineDraft& draft) {
    pending_ = draft;
    ++generation_;  // whatever is in flight is now stale
    debounce_.start();
}

void PresetResolver::resolve_preset(const QString& id_or_filepath) {
    debounce_.stop();
    ++generation_;
    run(id_or_filepath, {});
}

void PresetResolver::run(const QString& subject, std::function<void()> after) {
    const unsigned generation = generation_;
    QPointer<PresetResolver> guard(this);
    runner_(executable_, {QStringLiteral("resolve-preset"), subject, QStringLiteral("--json")},
            [guard, generation, after = std::move(after)](const CliResult& result) {
                if (after) {
                    after();
                }
                if (!guard || generation != guard->generation_) {
                    return;  // superseded by a later edit
                }
                if (!result.started) {
                    Q_EMIT guard->failed(tr("The Beebium server could not be run."));
                    return;
                }
                // Written whatever the exit status.
                if (const auto resolution =
                        parse_resolution(result.standard_output.toStdString())) {
                    Q_EMIT guard->resolved(*resolution);
                } else {
                    Q_EMIT guard->failed(result.standard_error.trimmed().isEmpty()
                                             ? tr("The server's answer could not be read.")
                                             : result.standard_error.trimmed());
                }
            });
}

}  // namespace beebium::linux_client
