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

#include "HostParallelPrinterExtension.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "HostParallelPrinterDispatcher.hpp"
#endif

#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beebium {

namespace {

// Parse the whole of `value` as an unsigned integer, or throw naming `key`.
std::uint64_t parse_unsigned(std::string_view key, std::string_view value,
                             std::string_view expected) {
    std::uint64_t parsed = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        throw std::runtime_error("host-parallel-printer: " + std::string(key) + " must be " +
                                 std::string(expected) + ", got '" + std::string(value) + "'");
    }
    return parsed;
}

}  // namespace

HostParallelPrinterExtension::HostParallelPrinterExtension() = default;
HostParallelPrinterExtension::~HostParallelPrinterExtension() = default;

std::span<const std::string_view> HostParallelPrinterExtension::attaches_to() const {
    static constexpr std::string_view deps[] = {"printer-port"};
    return deps;
}

std::span<const std::string_view> HostParallelPrinterExtension::provides() const {
    return {};
}

void HostParallelPrinterExtension::init(ExtensionContext& ctx) {
    printer::HostParallelPrinterSink::Options options;

    options.device = std::string(config_value("device").value_or(""));
    if (options.device.empty()) {
        throw std::runtime_error("host-parallel-printer: device=<path> is required");
    }

    if (auto value = config_value("buffer"); value && !value->empty()) {
        const auto parsed = parse_unsigned("buffer", *value, "a positive integer");
        if (parsed == 0) {
            throw std::runtime_error(
                "host-parallel-printer: buffer must be a positive integer, got '" +
                std::string(*value) + "'");
        }
        options.buffer = static_cast<std::size_t>(parsed);
    }

    if (auto value = config_value("reconnect_ms"); value && !value->empty()) {
        options.reconnect = std::chrono::milliseconds(
            parse_unsigned("reconnect_ms", *value, "a non-negative integer"));
    }

    options.on_activity = [this] { ui_.mark_dirty(); };

    sink_ = std::make_unique<printer::HostParallelPrinterSink>(std::move(options));
    std::cout << "host-parallel-printer: printing to " << sink_->options().device
              << " (opening)\n";
    ctx.get<PrinterPort>().attach(sink_->receiver());
}

void HostParallelPrinterExtension::shutdown() {
#ifdef BEEBIUM_BUILD_SERVICE
    // Drop the dispatcher before the sink it references.
    dispatcher_.reset();
#endif
    // Drops the sink, which stops and joins its thread. The machine is being
    // torn down, so the VIA is not ticking the receiver.
    sink_.reset();
}

std::vector<ExtensionRpcDispatcher*> HostParallelPrinterExtension::rpc_dispatchers() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!sink_) {
        return {};  // not initialised (or init failed): no dispatcher to expose
    }
    if (!dispatcher_) {
        dispatcher_ = std::make_unique<HostParallelPrinterDispatcher>(*sink_);
    }
    return {dispatcher_.get()};
#else
    return {};
#endif
}

}  // namespace beebium
