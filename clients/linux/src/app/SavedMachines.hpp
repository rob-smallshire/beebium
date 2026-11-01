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

#ifndef BEEBIUM_LINUX_APP_SAVED_MACHINES_HPP
#define BEEBIUM_LINUX_APP_SAVED_MACHINES_HPP

#include "servers/CoreCli.hpp"

#include <QObject>
#include <QHash>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace beebium::linux_client {

class ClientSettings;

// The saved machines the home window offers (client specification 5.3,
// 13.4): the client's recent machine directories, each described by the
// server's describe-machine, in recents order. A directory that does not
// describe is left out of the list but kept among the recents, since it may
// only be unreadable for now; one that has gone from the disk for
// FORGET_AFTER_PASSES passes in a row is forgotten, so the first-page rule
// does not count it.
class SavedMachines : public QObject {
    Q_OBJECT

public:
    static constexpr int FORGET_AFTER_PASSES = 3;

    using Described = std::function<void(const std::optional<MachineDescription>&)>;
    // Describe one machine directory, answering once, asynchronously or not.
    using Describe = std::function<void(const QString& machine_dirpath, Described done)>;

    SavedMachines(ClientSettings& settings, Describe describe, QObject* parent = nullptr);

    // Describe every recent machine again. One pass at a time: a reload asked
    // for during a pass becomes one more pass after it.
    void reload();

    const std::vector<MachineDescription>& machines() const { return machines_; }
    bool loading() const { return loading_; }
    bool has_recent_machines() const;

    // Drop one from the list, or all: only the list. The machines on disk
    // are untouched, and opening one again brings it back.
    void forget(const QString& machine_dirpath);
    void clear();

Q_SIGNALS:
    void changed();

private:
    void start_pass();
    void describe_next(std::size_t index, int pass);

    ClientSettings& settings_;
    Describe describe_;
    std::vector<MachineDescription> machines_;
    std::vector<MachineDescription> pending_;
    QStringList pass_dirpaths_;
    bool loading_ = false;
    bool reload_queued_ = false;
    int pass_ = 0;
    // Per directory, the passes in a row it has been gone from the disk.
    QHash<QString, int> gone_passes_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SAVED_MACHINES_HPP
