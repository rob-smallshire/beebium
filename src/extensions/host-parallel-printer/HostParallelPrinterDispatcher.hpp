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

#ifndef BEEBIUM_EXT_HOST_PARALLEL_PRINTER_DISPATCHER_HPP
#define BEEBIUM_EXT_HOST_PARALLEL_PRINTER_DISPATCHER_HPP

#include "HostParallelPrinterSink.hpp"
#include "PrinterDevice.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "host_parallel_printer.pb.h"

#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the HostParallelPrinter service:
// GetStatus, the typed twin of the sidebar panel, and ListDevices, which
// enumerates the printer devices on the host. It parses the extension's
// own protobuf request message from the opaque request bytes, reads the
// sink's thread-safe status and serializes the reply. No gRPC.
class HostParallelPrinterDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit HostParallelPrinterDispatcher(printer::HostParallelPrinterSink& sink) : sink_(sink) {}

    std::string_view service_name() const override { return "HostParallelPrinter"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetStatus") {
            HostParallelPrinterGetStatusRequest req;
            if (!req.ParseFromArray(request.data(), static_cast<int>(request.size()))) {
                return RpcStatus::error(kRpcInvalidArgument,
                                        "malformed HostParallelPrinter.GetStatus request");
            }
            const auto status = sink_.status();
            HostParallelPrinterStatus resp;
            resp.set_device(status.device);
            resp.set_open(status.open);
            resp.set_bytes_received(status.bytes_received);
            resp.set_bytes_sent(status.bytes_sent);
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "ListDevices") {
            HostParallelPrinterListDevicesRequest req;
            if (!req.ParseFromArray(request.data(), static_cast<int>(request.size()))) {
                return RpcStatus::error(kRpcInvalidArgument,
                                        "malformed HostParallelPrinter.ListDevices request");
            }
            HostParallelPrinterDeviceList resp;
            for (const auto& found : printer::list_printer_devices()) {
                auto* device = resp.add_devices();
                device->set_path(found.path);
                device->set_name(found.name);
            }
            return serialized(resp.SerializeToString(&response));
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "HostParallelPrinter has no method '" + std::string(method) + "'");
    }

private:
    printer::HostParallelPrinterSink& sink_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_HOST_PARALLEL_PRINTER_DISPATCHER_HPP
