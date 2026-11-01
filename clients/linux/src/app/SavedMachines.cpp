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

#include "app/SavedMachines.hpp"

#include "app/ClientSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>

namespace beebium::linux_client {

SavedMachines::SavedMachines(ClientSettings& settings, Describe describe, QObject* parent)
    : QObject(parent), settings_(settings), describe_(std::move(describe)) {}

bool SavedMachines::has_recent_machines() const {
    return !settings_.recent_machines().isEmpty();
}

void SavedMachines::reload() {
    if (loading_) {
        reload_queued_ = true;
        return;
    }
    start_pass();
}

void SavedMachines::start_pass() {
    loading_ = true;
    reload_queued_ = false;
    pending_.clear();
    pass_dirpaths_ = settings_.recent_machines();
    describe_next(0, ++pass_);
}

void SavedMachines::describe_next(std::size_t index, int pass) {
    if (pass != pass_) {
        return;  // a clear ended this pass
    }
    if (index >= static_cast<std::size_t>(pass_dirpaths_.size())) {
        loading_ = false;
        // Only a change is news: the page keeps its scroll and choice.
        if (pending_ != machines_) {
            machines_ = pending_;
            Q_EMIT changed();
        }
        if (reload_queued_) {
            start_pass();
        }
        return;
    }
    QPointer<SavedMachines> guard(this);
    describe_(pass_dirpaths_.at(static_cast<qsizetype>(index)),
              [this, guard, index, pass](const std::optional<MachineDescription>& machine) {
                  if (!guard || pass != pass_) {
                      return;
                  }
                  const QString dirpath = pass_dirpaths_.at(static_cast<qsizetype>(index));
                  if (machine) {
                      pending_.push_back(*machine);
                      gone_passes_.remove(dirpath);
                  } else if (!QFileInfo::exists(dirpath)) {
                      if (++gone_passes_[dirpath] >= FORGET_AFTER_PASSES) {
                          gone_passes_.remove(dirpath);
                          settings_.forget_machine(dirpath);
                      }
                  } else {
                      gone_passes_.remove(dirpath);
                  }
                  describe_next(index + 1, pass);
              });
}

void SavedMachines::forget(const QString& machine_dirpath) {
    settings_.forget_machine(machine_dirpath);
    const QString cleaned = QDir::cleanPath(machine_dirpath);
    const auto same = [&cleaned](const MachineDescription& machine) {
        return QDir::cleanPath(machine.machine_dirpath) == cleaned;
    };
    machines_.erase(std::remove_if(machines_.begin(), machines_.end(), same), machines_.end());
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), same), pending_.end());
    Q_EMIT changed();
}

void SavedMachines::clear() {
    settings_.clear_machines();
    machines_.clear();
    pending_.clear();
    if (loading_) {
        // End the pass under way: there is nothing left to describe.
        ++pass_;
        loading_ = false;
        reload_queued_ = false;
    }
    Q_EMIT changed();
}

}  // namespace beebium::linux_client
