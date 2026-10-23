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


#ifndef BEEBIUM_EXT_FILE_PRINTER_DISPATCHER_HPP
#define BEEBIUM_EXT_FILE_PRINTER_DISPATCHER_HPP

#include "FilePrinterSink.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "file_printer.pb.h"

#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the FilePrinter service: GetStatus
// and SetOnline, the typed twin of the sidebar panel. It parses the
// extension's own protobuf request messages from the opaque request bytes,
// reads the sink's thread-safe status and serializes the reply. No gRPC.
class FilePrinterDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit FilePrinterDispatcher(printer::FilePrinterSink& sink) : sink_(sink) {}

    std::string_view service_name() const override { return "FilePrinter"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetStatus") {
            FilePrinterGetStatusRequest req;
            if (!parse(request, req)) {
                return bad_request("GetStatus");
            }
            return reply_status(response);
        }
        if (method == "SetOnline") {
            FilePrinterSetOnlineRequest req;
            if (!parse(request, req)) {
                return bad_request("SetOnline");
            }
            sink_.set_online(req.online());
            return reply_status(response);
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "FilePrinter has no method '" + std::string(method) + "'");
    }

private:
    RpcStatus reply_status(std::string& response) {
        const auto status = sink_.status();
        FilePrinterStatus resp;
        resp.set_path(status.path);
        resp.set_bytes_received(status.bytes_received);
        resp.set_bytes_written(status.bytes_written);
        resp.set_online(status.online);
        return serialized(resp.SerializeToString(&response));
    }

    template <typename Msg>
    static bool parse(std::string_view bytes, Msg& out) {
        return out.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    static RpcStatus bad_request(const char* method) {
        return RpcStatus::error(
            kRpcInvalidArgument,
            std::string("malformed FilePrinter.") + method + " request");
    }

    printer::FilePrinterSink& sink_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_FILE_PRINTER_DISPATCHER_HPP
