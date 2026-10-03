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

#ifndef BEEBIUM_SERVICE_EXTENSION_RPC_SERVICE_HPP
#define BEEBIUM_SERVICE_EXTENSION_RPC_SERVICE_HPP

// The single, core-hosted gRPC service through which clients drive
// extension-provided APIs. It carries opaque serialized request/response
// bytes and routes them to an extension's ExtensionRpcDispatcher (a plain
// C++ ABI, no gRPC). This keeps the one gRPC runtime in the core: a plugin
// never links gRPC, so the duplicate-runtime crash (gRPC #39198) cannot
// happen. See docs/discussion/extension-rpc-channel.md.
//
// Unlike ExtensionUiService (whose generated stub lives in a shared DLL,
// forcing the cross-DLL co-location workaround), extension_rpc.proto is an
// ordinary core service: its stub and this impl both compile into
// beebium_service and link into the server executable, with no module
// boundary. Extensions are reached only through virtual ABI calls.

#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionRpc.hpp"

#include "extension_rpc.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <string>

namespace beebium::service {

class ExtensionRpcServiceImpl final : public ::beebium::ExtensionRpc::Service {
public:
    ExtensionRpcServiceImpl(EconetTransportRegistry& transport_registry,
                            ExtensionRegistry& peripheral_registry);
    ~ExtensionRpcServiceImpl() override;

    grpc::Status Invoke(grpc::ServerContext* context,
                        const ::beebium::InvokeRequest* request,
                        ::beebium::InvokeResponse* response) override;

    grpc::Status ServerStream(
        grpc::ServerContext* context,
        const ::beebium::InvokeRequest* request,
        grpc::ServerWriter<::beebium::InvokeResponse>* writer) override;

private:
    // Resolve (extension_id, service) to a dispatcher. A non-empty
    // extension_id selects that instance, whether it is a peripheral
    // extension or an Econet transport (the id EconetTransportService
    // reports). An empty extension_id routes by service name when exactly
    // one loaded instance offers the service, and is FAILED_PRECONDITION
    // naming the candidate ids when more than one does. On failure, leaves
    // a grpc::Status in *status and returns nullptr.
    ExtensionRpcDispatcher* find_dispatcher(const std::string& extension_id,
                                            const std::string& service,
                                            grpc::Status* status) const;

    // Visit every loaded extension (peripheral + econet transport).
    template <typename Fn>
    void for_each_extension(Fn&& fn) const;

    EconetTransportRegistry& transport_registry_;
    ExtensionRegistry& peripheral_registry_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_EXTENSION_RPC_SERVICE_HPP
