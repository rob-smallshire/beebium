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

#ifndef BEEBIUM_EXT_TCP_PRINTER_DISPATCHER_HPP
#define BEEBIUM_EXT_TCP_PRINTER_DISPATCHER_HPP

#include "TcpPrinterSink.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "tcp_printer.pb.h"

#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the TcpPrinter service: GetStatus,
// the typed twin of the sidebar panel. It parses the extension's own protobuf
// request message from the opaque request bytes, reads the sink's
// thread-safe status and serializes the reply. No gRPC.
class TcpPrinterDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit TcpPrinterDispatcher(printer::TcpPrinterSink& sink) : sink_(sink) {}

    std::string_view service_name() const override { return "TcpPrinter"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetStatus") {
            TcpPrinterGetStatusRequest req;
            if (!req.ParseFromArray(request.data(), static_cast<int>(request.size()))) {
                return RpcStatus::error(kRpcInvalidArgument,
                                        "malformed TcpPrinter.GetStatus request");
            }
            const auto status = sink_.status();
            TcpPrinterStatus resp;
            resp.set_endpoint(status.endpoint);
            resp.set_connected(status.connected);
            resp.set_bytes_received(status.bytes_received);
            resp.set_bytes_sent(status.bytes_sent);
            return serialized(resp.SerializeToString(&response));
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "TcpPrinter has no method '" + std::string(method) + "'");
    }

private:
    printer::TcpPrinterSink& sink_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_TCP_PRINTER_DISPATCHER_HPP
