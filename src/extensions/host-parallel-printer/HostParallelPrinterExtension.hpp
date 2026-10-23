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

#ifndef BEEBIUM_EXT_HOST_PARALLEL_PRINTER_EXTENSION_HPP
#define BEEBIUM_EXT_HOST_PARALLEL_PRINTER_EXTENSION_HPP

#include "HostParallelPrinterSink.hpp"
#include "HostParallelPrinterUi.hpp"

#include <beebium/extension/PeripheralExtension.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class ExtensionRpcDispatcher;

#ifdef BEEBIUM_BUILD_SERVICE
class HostParallelPrinterDispatcher;  // defined when the service layer is built
#endif

// PeripheralExtension that sends everything the BBC prints to a printer
// device on the host: a parallel port or a USB printer (including a
// USB-to-parallel adapter), which the operating system presents the same way.
// It owns a HostParallelPrinterSink (a CentronicsReceiver drained into the
// device) and attaches the receiver to the printer port via the PrinterPort
// handle. The printer is online exactly while its device is open.
//
// CLI: --host-parallel-printer device=<path>[:buffer=<n>][:reconnect_ms=<n>]
//   device       : the host device (required): /dev/usb/lp0 or /dev/lp0 on
//                  Linux; on Windows a USB printer's \\?\usb#... interface
//                  path (ListDevices enumerates them) or LPTn, opened as
//                  \\.\LPTn
//   buffer       : receiver queue depth in bytes (default 256)
//   reconnect_ms : wait between open attempts (default 1000); 0 makes one
//                  attempt and never retries
class HostParallelPrinterExtension : public PeripheralExtension {
public:
    HostParallelPrinterExtension();
    ~HostParallelPrinterExtension() override;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // Typed API (GetStatus) over the core's ExtensionRpc channel.
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // Peripherals-sidebar panel.
    ExtensionUi* ui() override { return &ui_; }

    // Non-owning accessor used by HostParallelPrinterUi. Null before init() /
    // after shutdown().
    printer::HostParallelPrinterSink* sink() { return sink_.get(); }

private:
    // ui_ is declared before sink_ so sink_ is destroyed first: its thread
    // (which calls ui_.mark_dirty through on_activity) is joined before ui_
    // dies.
    HostParallelPrinterUi ui_{*this};
    std::unique_ptr<printer::HostParallelPrinterSink> sink_;
#ifdef BEEBIUM_BUILD_SERVICE
    // Declared after sink_ so it is destroyed first (it references the sink).
    std::unique_ptr<HostParallelPrinterDispatcher> dispatcher_;
#endif
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_HOST_PARALLEL_PRINTER_EXTENSION_HPP
