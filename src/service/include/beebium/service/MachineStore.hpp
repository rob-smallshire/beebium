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

#ifndef BEEBIUM_SERVICE_MACHINE_STORE_HPP
#define BEEBIUM_SERVICE_MACHINE_STORE_HPP

#include <string>
#include <vector>

namespace beebium::service {

// Where a served machine is kept on disk, and what SystemService asks of
// it: its directory, saving it and forgetting its battery-backed state. The
// server, which owns the machine directory and knows its layout, implements
// it; the service knows neither. Every member may be called from any gRPC
// thread.
class MachineStore {
public:
    virtual ~MachineStore() = default;

    // The machine's directory, saved or transient; empty for none.
    virtual std::string machine_dirpath() const = 0;

    // True when the directory outlives the server.
    virtual bool machine_saved() const = 0;

    // True when a machine not yet saved has written to the copies of media
    // its template named, which are lost unless it is saved.
    virtual bool template_media_written() const = 0;

    struct SaveOutcome {
        enum class Error { None, InvalidArgument, AlreadyExists, FailedPrecondition, Internal };
        Error error = Error::None;
        std::string message;          // why, when error is not None
        std::string machine_dirpath;  // the machine's directory now
        std::string machine_uuid;     // its uuid now: a Save As gives a new one
    };

    // Saves the machine, as SystemService.SaveMachine describes, given its
    // uuid and name template now. An empty `destination_dirpath` saves a
    // saved machine in place; `replace_existing` replaces a saved machine
    // there.
    virtual SaveOutcome save(const std::string& destination_dirpath, bool replace_existing,
                             const std::string& machine_uuid,
                             const std::string& name_template) = 0;

    // Forgets the battery-backed state, from the next start; returns the
    // units forgotten.
    virtual std::vector<std::string> reset_battery() = 0;

    // Records a new name template, for a saved machine in its directory.
    virtual void renamed(const std::string& name_template) = 0;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_MACHINE_STORE_HPP
