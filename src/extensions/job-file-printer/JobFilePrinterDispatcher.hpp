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

#ifndef BEEBIUM_EXT_JOB_FILE_PRINTER_DISPATCHER_HPP
#define BEEBIUM_EXT_JOB_FILE_PRINTER_DISPATCHER_HPP

#include "JobFilePrinterSink.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "job_file_printer.pb.h"

#include <string>
#include <string_view>

namespace beebium {

class JobFilePrinterDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit JobFilePrinterDispatcher(printer::JobFilePrinterSink& sink) : sink_(sink) {}

    std::string_view service_name() const override { return "JobFilePrinter"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetStatus") {
            JobFilePrinterGetStatusRequest req;
            if (!parse(request, req)) {
                return bad_request("GetStatus");
            }
            return reply_status(response);
        }
        if (method == "SetOnline") {
            JobFilePrinterSetOnlineRequest req;
            if (!parse(request, req)) {
                return bad_request("SetOnline");
            }
            sink_.set_online(req.online());
            return reply_status(response);
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "JobFilePrinter has no method '" + std::string(method) + "'");
    }

private:
    RpcStatus reply_status(std::string& response) {
        const auto status = sink_.status();
        JobFilePrinterStatus resp;
        resp.set_directory(status.dirpath);
        resp.set_online(status.online);
        resp.set_current_job_file(status.current_job_filepath);
        resp.set_bytes_in_job(status.bytes_in_job);
        resp.set_jobs_completed(status.jobs_completed);
        resp.set_total_bytes(status.total_bytes);
        return serialized(resp.SerializeToString(&response));
    }

    template <typename Msg>
    static bool parse(std::string_view bytes, Msg& out) {
        return out.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    static RpcStatus bad_request(const char* method) {
        return RpcStatus::error(
            kRpcInvalidArgument,
            std::string("malformed JobFilePrinter.") + method + " request");
    }

    printer::JobFilePrinterSink& sink_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_JOB_FILE_PRINTER_DISPATCHER_HPP
