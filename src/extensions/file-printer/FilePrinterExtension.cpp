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

#include <charconv>
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

    const std::string line_ending = std::string(config_value("line_ending").value_or("raw"));
    if (line_ending == "raw") {
        options.line_ending = printer::LineEnding::Raw;
    } else if (line_ending == "lf") {
        options.line_ending = printer::LineEnding::Lf;
    } else if (line_ending == "crlf") {
        options.line_ending = printer::LineEnding::CrLf;
    } else {
        throw std::runtime_error("file-printer: unknown line_ending '" + line_ending +
                                 "' (expected 'raw', 'lf' or 'crlf')");
    }

    if (auto value = config_value("buffer"); value && !value->empty()) {
        std::size_t parsed = 0;
        auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), parsed);
        if (ec != std::errc{} || ptr != value->data() + value->size() || parsed == 0) {
            throw std::runtime_error("file-printer: buffer must be a positive integer, got '" +
                                     std::string(*value) + "'");
        }
        options.buffer = parsed;
    }

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
