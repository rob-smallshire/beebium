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

#include "beebium/extension/PeripheralExtension.hpp"
#include "beebium/tube/ParasiteRunner.hpp"
#include "beebium/tube/TubeSocket.hpp"
#include "beebium/tube/TubeUla.hpp"
#include "beebium/service/DebuggerService.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace beebium {

// Acorn 65C02 3 MHz second processor, implemented as a Peripheral Extension.
//
// Owns everything on the parasite side of the Tube cable:
//   - TubeUla (register bridge)
//   - ParasiteRunner (CPU, memory map, boot ROM, breakpoints)
//
// The TubeUla is installed into the host's TubeSocket as the backend.
// The ParasiteRunner is installed as the Coprocessor so that Machine::step()
// drives the parasite in host time (single-threaded model). The clock ratio
// is 3:2 (3 MHz parasite, 2 MHz host) and lives with the runner.
//
// Lifecycle:
//   init()     -- load ROM, create components, install backend + coprocessor
//   shutdown() -- remove coprocessor, uninstall backend

class SecondProcessor65C02Extension : public PeripheralExtension {
public:
    SecondProcessor65C02Extension() = default;
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

    // --- Cross-processor debugger coordination ---

    // Wire the counterpart stop callbacks between host and parasite
    // debugger services. Call after both debugger services exist.
    // host_pause: callback to pause the host Machine.
    void wire_counterpart_stop(std::function<void()> host_pause) {
        // When parasite hits a breakpoint with stop_counterpart,
        // pause the host.
        if (debugger_service_)
            debugger_service_->set_counterpart_stop_callback(std::move(host_pause));
    }

    // Get the callback that pauses the parasite.
    // The host's DebuggerService should use this as its counterpart stop callback.
    std::function<void()> parasite_pause_callback() {
        return [this] {
            if (runner_) runner_->pause();
        };
    }

    // --- Accessors ---

    TubeUla* tube_ula() { return tube_ula_.get(); }
    ParasiteRunner* runner() { return runner_.get(); }
    service::DebuggerControlServiceImpl<ParasiteRunner>* debugger_service() {
        return debugger_service_.get();
    }
    bool running() const { return runner_ != nullptr; }

private:
    bool load_rom(std::array<uint8_t, 2048>& rom) const;

    std::unique_ptr<TubeUla> tube_ula_;
    std::unique_ptr<ParasiteRunner> runner_;
    // The parasite debugger impl. The server wraps this in a
    // ParasiteDebuggerAdapter and registers it as the ParasiteDebuggerControl
    // gRPC service -- the extension does not host gRPC services itself.
    std::unique_ptr<service::DebuggerControlServiceImpl<ParasiteRunner>> debugger_service_;
    TubeSocket* tube_socket_ = nullptr;  // non-owning, from ExtensionContext
};

}  // namespace beebium
