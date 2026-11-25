// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// AesExtension.hpp, the extension's interface
// version 2.0.0, 20 November 2026.

// AesExtension: the plugin as Beebium sees it, a coprocessor extension that owns the Tube chip,
// the board and the debugger's link while the machine runs.

#pragma once

#include "AesDebug.hpp"
#include "AesTube.hpp"

#include <beebium/extension/CoprocessorExtension.hpp>
#include <beebium/tube/TubeSocket.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>

// The plugin's extension. It implements Beebium's CoprocessorExtension interface; Beebium's loader
// makes it (plugin_entry.cpp), then calls init when the machine starts.
class AesExtension : public beebium::CoprocessorExtension {
public:
    // Stops the AES if Beebium has not already.
    ~AesExtension() override { shutdown(); }

    // The plugin plugs into the BBC's Tube socket, and offers nothing for others to attach to.
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view points[] = {"tube"};
        return points;
    }
    std::span<const std::string_view> provides() const override { return {}; }

    // Starts the AES with the machine, and stops it.
    void init(beebium::ExtensionContext& context) override;
    void shutdown() override;

    // The parts Beebium asks for: what it runs against its clock (AesTube); the Tube chip as
    // the host sees it (Beebium's TubeUla); what its debugger talks to (AesDebug); the
    // processor's name; the clock it shows.
    beebium::Coprocessor* coprocessor() override { return tube_.get(); }
    beebium::TubeHostBackend* tube_backend() override { return ula_.get(); }
    beebium::CpuDebugTarget* debug_target() override { return debug_.get(); }
    std::string cpu_name() const override { return "ARM1"; }
    uint64_t clock_hz() const override;

private:
    // The switches, from the plugin's settings.
    AesOptions options_from_settings() const;

    std::unique_ptr<beebium::TubeUla> ula_;
    std::unique_ptr<AesTube> tube_;
    std::unique_ptr<AesDebug> debug_;  // Over tube_, so made after it and freed before it.
    beebium::TubeSocket* socket_ = nullptr;  // Beebium's; not ours to free.
};
