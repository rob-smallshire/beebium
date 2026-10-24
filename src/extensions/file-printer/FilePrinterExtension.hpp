// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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


#ifndef BEEBIUM_EXT_FILE_PRINTER_EXTENSION_HPP
#define BEEBIUM_EXT_FILE_PRINTER_EXTENSION_HPP

#include "FilePrinterSink.hpp"
#include "FilePrinterUi.hpp"

#include <beebium/extension/PeripheralExtension.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

class ExtensionRpcDispatcher;

#ifdef BEEBIUM_BUILD_SERVICE
class FilePrinterDispatcher;  // defined when the service layer is built
#endif

// Built-in PeripheralExtension that spools everything the BBC sends to its
// printer port into a file on the host. It owns a FilePrinterSink (a
// CentronicsReceiver drained by a sink thread into a file) and attaches the
// receiver to the printer port via the PrinterPort handle.
//
// CLI: --file-printer path=<file>[:line_ending=raw|lf|crlf][:buffer=<n>]
//   path        : the file to write, created or truncated at attach (required)
//   line_ending : raw (default), lf or crlf; see printer::LineEnding
//   buffer      : receiver queue depth in bytes (default 256)
class FilePrinterExtension : public PeripheralExtension {
public:
    FilePrinterExtension();
    ~FilePrinterExtension() override;

    std::span<const std::string_view> attaches_to() const override;
    std::span<const std::string_view> provides() const override;
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // Typed API (GetStatus / SetOnline) over the core's ExtensionRpc channel.
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // Peripherals-sidebar panel.
    ExtensionUi* ui() override { return &ui_; }

    // Non-owning accessor used by FilePrinterUi. Null before init() / after
    // shutdown().
    printer::FilePrinterSink* sink() { return sink_.get(); }

private:
    // ui_ is declared before sink_ so sink_ is destroyed first: its thread
    // (which calls ui_.mark_dirty through on_activity) is joined before ui_
    // dies.
    FilePrinterUi ui_{*this};
    std::unique_ptr<printer::FilePrinterSink> sink_;
#ifdef BEEBIUM_BUILD_SERVICE
    // Declared after sink_ so it is destroyed first (it references the sink).
    std::unique_ptr<FilePrinterDispatcher> dispatcher_;
#endif
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_FILE_PRINTER_EXTENSION_HPP
