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

#ifndef BEEBIUM_EXTENSION_SCRATCH_RAM_DISPATCHER_HPP
#define BEEBIUM_EXTENSION_SCRATCH_RAM_DISPATCHER_HPP

#include "TestScratchRam.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "scratch_ram.pb.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the framework-test scratch RAM,
// served through the core's ExtensionRpc channel rather than a plugin-hosted
// gRPC service. The plugin therefore links protobuf (for these messages) but
// not gRPC. See docs/discussion/extension-rpc-channel.md.
class ScratchRamDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit ScratchRamDispatcher(TestScratchRam& ram) : ram_(ram) {}

    std::string_view service_name() const override { return "ScratchRamService"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "Read") {
            ScratchRamReadRequest req;
            if (!parse(request, req)) {
                return bad_request("Read");
            }
            if (req.offset() >= 8) {
                return RpcStatus::error(kRpcInvalidArgument, "offset must be 0-7");
            }
            ScratchRamReadResponse resp;
            resp.set_value(ram_.peek(static_cast<std::uint16_t>(req.offset())));
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "Write") {
            ScratchRamWriteRequest req;
            if (!parse(request, req)) {
                return bad_request("Write");
            }
            if (req.offset() >= 8) {
                return RpcStatus::error(kRpcInvalidArgument, "offset must be 0-7");
            }
            if (req.value() > 255) {
                return RpcStatus::error(kRpcInvalidArgument, "value must be 0-255");
            }
            // The emulation thread reads this RAM through the memory map every
            // cycle; halt it across the write (see with_bus_stopped).
            with_bus_stopped([&] {
                ram_.poke(static_cast<std::uint16_t>(req.offset()),
                          static_cast<std::uint8_t>(req.value()));
            });
            ScratchRamWriteResponse resp;
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "ReadAll") {
            std::string data(8, '\0');
            for (int i = 0; i < 8; ++i) {
                data[i] = static_cast<char>(ram_.peek(static_cast<std::uint16_t>(i)));
            }
            ScratchRamReadAllResponse resp;
            resp.set_data(std::move(data));
            return serialized(resp.SerializeToString(&response));
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "ScratchRamService has no method '" + std::string(method) + "'");
    }

private:
    template <typename Msg>
    static bool parse(std::string_view bytes, Msg& out) {
        return out.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    static RpcStatus bad_request(const char* method) {
        return RpcStatus::error(
            kRpcInvalidArgument,
            std::string("malformed ScratchRamService.") + method + " request");
    }

    TestScratchRam& ram_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_SCRATCH_RAM_DISPATCHER_HPP
