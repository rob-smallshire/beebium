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


#include "FilePrinterExtension.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "FilePrinterDispatcher.hpp"
#endif

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace beebium {

FilePrinterExtension::FilePrinterExtension() = default;
FilePrinterExtension::~FilePrinterExtension() = default;

std::span<const std::string_view> FilePrinterExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"printer-port"};
    return deps;
}

std::span<const std::string_view> FilePrinterExtension::provides() const {
    return {};
}

void FilePrinterExtension::init(ExtensionContext& ctx) {
    printer::FilePrinterSink::Options options;

    const std::string path = std::string(config_value("path").value_or(""));
    if (path.empty()) {
        throw std::runtime_error("file-printer: path=<file> is required");
    }
    options.path = path;

    options.line_ending = printer::parse_line_ending_option("file-printer",
                                                            config_value("line_ending"));
    options.buffer = printer::parse_buffer_option("file-printer", config_value("buffer"));

    options.online = config_bool("online", true);
    options.on_activity = [this] { ui_.mark_dirty(); };

    sink_ = std::make_unique<printer::FilePrinterSink>(std::move(options));
    std::cout << "file-printer: spooling to " << sink_->options().path.string() << "\n";
    ctx.get<PrinterPort>().attach(sink_->receiver());
}

void FilePrinterExtension::shutdown() {
#ifdef BEEBIUM_BUILD_SERVICE
    // Drop the dispatcher before the sink it references.
    dispatcher_.reset();
#endif
    // Drops the sink, which drains, stops and joins its thread. The machine
    // is being torn down, so the VIA is not ticking the receiver.
    sink_.reset();
}

std::vector<ExtensionRpcDispatcher*> FilePrinterExtension::rpc_dispatchers() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!sink_) {
        return {};  // not initialised (or init failed): no dispatcher to expose
    }
    if (!dispatcher_) {
        dispatcher_ = std::make_unique<FilePrinterDispatcher>(*sink_);
    }
    return {dispatcher_.get()};
#else
    return {};
#endif
}

}  // namespace beebium
