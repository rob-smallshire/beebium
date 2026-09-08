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

#pragma once

#include "beebium/extension/CoprocessorExtension.hpp"
#include "beebium/tube/CoprocessorRunner.hpp"
#include "beebium/tube/TubeSocket.hpp"
#include "beebium/tube/TubeUla.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace beebium {

// Acorn 65C02-family second processor, implemented as a Peripheral Extension.
//
// Owns everything on the coprocessor side of the Tube cable:
//   - TubeUla (register bridge)
//   - CoprocessorRunner (CPU, memory map, boot ROM, breakpoints)
//
// The TubeUla is installed into the host's TubeSocket as the backend.
// The CoprocessorRunner is installed as the Coprocessor so that Machine::step()
// drives the coprocessor in host time (single-threaded model).
//
// One class serves both members of the family: the 3 MHz 65C02 second
// processor (ratio 3/2) and the 4 MHz 65C102 second processor (ratio 2/1).
// They are software-identical -- same 2 KB Tube client ROM, same 64 KB RAM --
// so only the clock ratio and the display identity differ, and each plugin
// entry point constructs this class with the right pair. The clock ratio lives
// with the runner.
//
// Lifecycle:
//   init()     -- load ROM, create components, install backend + coprocessor
//   shutdown() -- remove coprocessor, uninstall backend

class SecondProcessor65C02Extension : public CoprocessorExtension {
public:
    // clock_ratio: coprocessor/host cycle ratio (3/2 for the 65C02, 2/1 for the
    // 65C102). cpu_label: short identity for the startup log line.
    explicit SecondProcessor65C02Extension(ClockRatio clock_ratio = ClockRatio{3, 2},
                                           std::string cpu_label = "65C02 (3 MHz)")
        : clock_ratio_(clock_ratio), cpu_label_(std::move(cpu_label)) {}
    ~SecondProcessor65C02Extension() override { shutdown(); }

    // --- PeripheralExtension interface ---

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"tube"};
        return deps;
    }

    std::span<const std::string_view> provides() const override {
        return {};
    }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // --- CoprocessorExtension interface ---

    Coprocessor* coprocessor() override { return runner_.get(); }
    TubeHostBackend* tube_backend() override { return tube_ula_.get(); }
    CpuDebugTarget* debug_target() override { return runner_.get(); }

    // --- Accessors (for tests linking the extension directly) ---

    TubeUla* tube_ula() { return tube_ula_.get(); }
    CoprocessorRunner* runner() { return runner_.get(); }
    bool running() const { return runner_ != nullptr; }

private:
    // Load the Tube client ROM into `rom`: an explicit `rom` config override,
    // else the "client" ROM declared in the manifest (Extension::load_rom).
    bool load_client_rom(std::array<uint8_t, 2048>& rom) const;

    ClockRatio clock_ratio_;
    std::string cpu_label_;
    std::unique_ptr<TubeUla> tube_ula_;
    std::unique_ptr<CoprocessorRunner> runner_;
    TubeSocket* tube_socket_ = nullptr;  // non-owning, from ExtensionContext
};

}  // namespace beebium
