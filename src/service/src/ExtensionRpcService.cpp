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

#include "beebium/service/ExtensionRpcService.hpp"

#include <map>
#include <string>
#include <vector>

namespace beebium::service {

namespace {

// Translate an extension's RpcStatus to a grpc::Status. code 0 == OK.
grpc::Status to_grpc_status(const ::beebium::RpcStatus& s) {
    if (s.is_ok()) {
        return grpc::Status::OK;
    }
    return grpc::Status(static_cast<grpc::StatusCode>(s.code), s.message);
}

// RpcContext over a live grpc::ServerContext. Metadata is left empty for now
// (Phase 0); the seam is here for when an extension needs request headers.
class RpcContextImpl final : public ::beebium::RpcContext {
public:
    explicit RpcContextImpl(grpc::ServerContext* ctx) : ctx_(ctx) {}
    bool is_cancelled() const override { return ctx_->IsCancelled(); }
    const std::map<std::string, std::string>& metadata() const override {
        return metadata_;
    }

private:
    grpc::ServerContext* ctx_;
    std::map<std::string, std::string> metadata_;
};

// RpcResponseWriter over a grpc::ServerWriter<InvokeResponse>. Each serialized
// response from the extension is wrapped in an InvokeResponse and written.
// Write() reporting false (peer gone) is propagated so the handler stops.
class RpcResponseWriterImpl final : public ::beebium::RpcResponseWriter {
public:
    explicit RpcResponseWriterImpl(
        grpc::ServerWriter<::beebium::InvokeResponse>* writer)
        : writer_(writer) {}

    bool write(std::string_view serialized_response) override {
        ::beebium::InvokeResponse resp;
        resp.set_payload(serialized_response.data(), serialized_response.size());
        return writer_->Write(resp);
    }

private:
    grpc::ServerWriter<::beebium::InvokeResponse>* writer_;
};

}  // namespace

ExtensionRpcServiceImpl::ExtensionRpcServiceImpl(
    EconetTransportRegistry& transport_registry,
    ExtensionRegistry& peripheral_registry)
    : transport_registry_(transport_registry),
      peripheral_registry_(peripheral_registry) {}

ExtensionRpcServiceImpl::~ExtensionRpcServiceImpl() = default;

template <typename Fn>
void ExtensionRpcServiceImpl::for_each_extension(Fn&& fn) const {
    for (auto* ext : peripheral_registry_.extensions()) {
        fn(static_cast<Extension*>(ext));
    }
    for (const auto& ext : transport_registry_.extensions()) {
        fn(static_cast<Extension*>(ext.get()));
    }
}

ExtensionRpcDispatcher* ExtensionRpcServiceImpl::find_dispatcher(
    const std::string& extension_id, const std::string& service,
    grpc::Status* status) const {
    // Each match remembers the id of the instance that owns the dispatcher,
    // so an ambiguity can name the candidates the caller must choose between.
    struct Match {
        ExtensionRpcDispatcher* dispatcher;
        std::string instance_id;
    };
    std::vector<Match> matches;
    bool extension_seen = false;

    for_each_extension([&](Extension* ext) {
        if (!extension_id.empty()) {
            if (ext->id() != extension_id) {
                return;
            }
            extension_seen = true;
        }
        for (auto* d : ext->rpc_dispatchers()) {
            if (d != nullptr && d->service_name() == service) {
                matches.push_back({d, std::string(ext->id())});
            }
        }
    });

    if (matches.size() == 1) {
        *status = grpc::Status::OK;
        return matches.front().dispatcher;
    }
    if (matches.empty()) {
        if (!extension_id.empty() && !extension_seen) {
            *status = grpc::Status(grpc::StatusCode::NOT_FOUND,
                                   "unknown extension instance: " + extension_id);
        } else {
            *status = grpc::Status(
                grpc::StatusCode::NOT_FOUND,
                "no extension offers service '" + service + "'" +
                    (extension_id.empty() ? "" : " on instance " + extension_id));
        }
        return nullptr;
    }
    if (!extension_id.empty()) {
        // Instance ids are unique within a registry, so this needs the same
        // id configured on instances in both registries, or one instance
        // registering the service twice.
        *status = grpc::Status(
            grpc::StatusCode::FAILED_PRECONDITION,
            "service '" + service + "' is offered more than once by instance id '" +
                extension_id + "'");
        return nullptr;
    }
    // More than one instance offers the service and none was named.
    std::string candidates;
    for (const auto& m : matches) {
        if (!candidates.empty()) {
            candidates += ", ";
        }
        candidates += m.instance_id;
    }
    *status = grpc::Status(
        grpc::StatusCode::FAILED_PRECONDITION,
        "service '" + service + "' is offered by " +
            std::to_string(matches.size()) +
            " extension instances; specify extension_id, one of: " + candidates);
    return nullptr;
}

grpc::Status ExtensionRpcServiceImpl::Invoke(
    grpc::ServerContext* context, const ::beebium::InvokeRequest* request,
    ::beebium::InvokeResponse* response) {
    grpc::Status route;
    ExtensionRpcDispatcher* dispatcher =
        find_dispatcher(request->extension_id(), request->service(), &route);
    if (dispatcher == nullptr) {
        return route;
    }
    RpcContextImpl ctx(context);
    std::string out;
    ::beebium::RpcStatus s =
        dispatcher->invoke(request->method(), request->payload(), out, ctx);
    if (!s.is_ok()) {
        return to_grpc_status(s);
    }
    response->set_payload(std::move(out));
    return grpc::Status::OK;
}

grpc::Status ExtensionRpcServiceImpl::ServerStream(
    grpc::ServerContext* context, const ::beebium::InvokeRequest* request,
    grpc::ServerWriter<::beebium::InvokeResponse>* writer) {
    grpc::Status route;
    ExtensionRpcDispatcher* dispatcher =
        find_dispatcher(request->extension_id(), request->service(), &route);
    if (dispatcher == nullptr) {
        return route;
    }
    RpcContextImpl ctx(context);
    RpcResponseWriterImpl out(writer);
    ::beebium::RpcStatus s =
        dispatcher->server_stream(request->method(), request->payload(), out, ctx);
    return to_grpc_status(s);
}

}  // namespace beebium::service
