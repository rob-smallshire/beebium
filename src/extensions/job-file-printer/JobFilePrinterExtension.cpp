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

#include "JobFilePrinterExtension.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "JobFilePrinterDispatcher.hpp"
#endif

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace beebium {

namespace {

// VIA ticks in an emulated second: the port is ticked at 1 MHz.
constexpr uint64_t TICKS_PER_SECOND = 1'000'000;

}  // namespace

JobFilePrinterExtension::JobFilePrinterExtension() = default;
JobFilePrinterExtension::~JobFilePrinterExtension() = default;

std::span<const std::string_view> JobFilePrinterExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"printer-port"};
    return deps;
}

std::span<const std::string_view> JobFilePrinterExtension::provides() const {
    return {};
}

void JobFilePrinterExtension::init(ExtensionContext& ctx) {
    printer::JobFilePrinterSink::Options options;

    const std::string directory = std::string(config_value("directory").value_or(""));
    if (directory.empty()) {
        throw std::runtime_error("job-file-printer: directory=<dir> is required");
    }
    options.dirpath = directory;

    if (auto value = config_value("idle"); value && !value->empty()) {
        uint64_t seconds = 0;
        auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), seconds);
        if (ec != std::errc{} || ptr != value->data() + value->size() || seconds == 0 ||
            seconds > UINT64_MAX / TICKS_PER_SECOND) {
            throw std::runtime_error(
                "job-file-printer: idle must be a positive whole number of seconds, got '" +
                std::string(*value) + "'");
        }
        options.idle_ticks = seconds * TICKS_PER_SECOND;
    }

    options.line_ending = printer::parse_line_ending_option("job-file-printer",
                                                            config_value("line_ending"));
    options.buffer = printer::parse_buffer_option("job-file-printer", config_value("buffer"));
    options.on_activity = [this] { ui_.mark_dirty(); };

    sink_ = std::make_unique<printer::JobFilePrinterSink>(std::move(options));
    std::cout << "job-file-printer: writing jobs to " << sink_->options().dirpath.string()
              << "\n";
    ctx.get<PrinterPort>().attach(sink_->device());
}

void JobFilePrinterExtension::shutdown() {
#ifdef BEEBIUM_BUILD_SERVICE
    // Drop the dispatcher before the sink it references.
    dispatcher_.reset();
#endif
    // Drops the sink, which writes what is queued, closes the open job, and
    // stops and joins its thread. The machine is being torn down, so the VIA
    // is not ticking the device.
    sink_.reset();
}

std::vector<ExtensionRpcDispatcher*> JobFilePrinterExtension::rpc_dispatchers() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!sink_) {
        return {};  // not initialised (or init failed): no dispatcher to expose
    }
    if (!dispatcher_) {
        dispatcher_ = std::make_unique<JobFilePrinterDispatcher>(*sink_);
    }
    return {dispatcher_.get()};
#else
    return {};
#endif
}

}  // namespace beebium
