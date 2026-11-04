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

#ifndef BEEBIUM_LINUX_APP_PRESET_RESOLVER_HPP
#define BEEBIUM_LINUX_APP_PRESET_RESOLVER_HPP

#include "preset/MachineDraft.hpp"
#include "preset/PresetResolution.hpp"
#include "servers/CoreCli.hpp"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <memory>

namespace beebium::linux_client {

// Asks the server what it makes of a draft (client specification 5.2): the
// draft written to a neutral .json under a temporary directory and given
// to the model's executable's resolve-preset --json. Edits come in bursts,
// so a resolution waits for DEBOUNCE_MS of quiet, as the macOS client's
// does, and only the latest draft's answer is ever reported.
class PresetResolver : public QObject {
    Q_OBJECT

public:
    static constexpr int DEBOUNCE_MS = 300;
    // Runs a server subcommand; CoreCli by default, scripted in tests.
    using Runner = std::function<void(const QString& executable_filepath,
                                      const QStringList& arguments, CoreCli::Done done)>;

    explicit PresetResolver(QObject* parent = nullptr);
    PresetResolver(Runner runner, QObject* parent = nullptr);
    ~PresetResolver() override;

    // The model's executable, which resolves its own presets.
    void set_executable(const QString& executable_filepath) { executable_ = executable_filepath; }

    // Resolve a draft once edits pause; a later call supersedes it.
    void resolve(const MachineDraft& draft);
    // Resolve a stored preset (an id or a file) now: where editing starts.
    void resolve_preset(const QString& id_or_filepath);

Q_SIGNALS:
    // The latest request's answer.
    void resolved(const beebium::linux_client::PresetResolution& resolution);
    // The server could not be asked, or said nothing a resolution reads.
    void failed(const QString& message);

private:
    void run(const QString& subject, std::function<void()> after);

    Runner runner_;
    std::unique_ptr<CoreCli> cli_;
    QString executable_;
    QTemporaryDir directory_;
    QTimer debounce_;
    MachineDraft pending_;
    unsigned generation_ = 0;
    unsigned files_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PRESET_RESOLVER_HPP
