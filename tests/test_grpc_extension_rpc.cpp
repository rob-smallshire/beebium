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

// End-to-end gRPC tests for the generic ExtensionRpc channel. A fake
// extension registers a fake dispatcher; calls go through the real
// ExtensionRpcServiceImpl over a real in-process gRPC server. Exercises
// unary routing (by service name and by explicit instance id), error
// mapping (unknown service -> NOT_FOUND, dispatcher error -> its status),
// ambiguous routing, and the server-streaming path.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/econet/NetworkBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionRpc.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/service/Server.hpp"

#include "extension_rpc.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <span>
#include <string>
#include <utility>

namespace {

// Serves "Echo" (unary: echoes the request bytes), "WhoAmI" (unary: returns
// the tag of the instance that owns this dispatcher, so a test can tell which
// instance a call reached), "Fail" (unary: returns a chosen non-OK status),
// "Count" (server-stream: writes N single-byte responses, where N is the
// first request byte) and "WhoStream" (server-stream: writes the tag once).
class FakeDispatcher final : public beebium::ExtensionRpcDispatcher {
public:
    FakeDispatcher(std::string service, std::string tag)
        : service_(std::move(service)), tag_(std::move(tag)) {}

    std::string_view service_name() const override { return service_; }

    beebium::RpcStatus invoke(std::string_view method, std::string_view request,
                              std::string& response,
                              beebium::RpcContext&) override {
        if (method == "Echo") {
            response.assign(request);
            return beebium::RpcStatus::ok();
        }
        if (method == "WhoAmI") {
            response.assign(tag_);
            return beebium::RpcStatus::ok();
        }
        if (method == "Fail") {
            return beebium::RpcStatus::error(beebium::kRpcPermissionDenied, "denied");
        }
        return beebium::RpcStatus::error(beebium::kRpcUnimplemented,
                                         "no method " + std::string(method));
    }

    beebium::RpcStatus server_stream(std::string_view method,
                                     std::string_view request,
                                     beebium::RpcResponseWriter& writer,
                                     beebium::RpcContext& ctx) override {
        if (method == "WhoStream") {
            writer.write(tag_);
            return beebium::RpcStatus::ok();
        }
        if (method != "Count") {
            return beebium::RpcStatus::error(beebium::kRpcUnimplemented, "no stream");
        }
        const int n = request.empty() ? 0 : static_cast<unsigned char>(request[0]);
        for (int i = 0; i < n && !ctx.is_cancelled(); ++i) {
            const char byte = static_cast<char>(i);
            if (!writer.write(std::string_view(&byte, 1))) {
                break;  // peer gone
            }
        }
        return beebium::RpcStatus::ok();
    }

private:
    std::string service_;
    std::string tag_;
};

// A transport extension carrying one dispatcher, addressable by a fixed id.
class FakeRpcTransport : public beebium::EconetTransportExtension {
public:
    FakeRpcTransport(std::string id, std::string service)
        : dispatcher_(std::move(service), id) {
        beebium::ExtensionManifest m;
        m.name = "fake-rpc";
        m.cli_name = m.name;
        m.extension_kind = "econet-transport";
        set_manifest(std::move(m));
        set_config_value("id", std::move(id));
    }

    std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
        return nullptr;
    }

    std::vector<beebium::ExtensionRpcDispatcher*> rpc_dispatchers() override {
        return {&dispatcher_};
    }

private:
    FakeDispatcher dispatcher_;
};

class ExtensionRpcFixture {
public:
    explicit ExtensionRpcFixture(beebium::EconetTransportRegistry& transports)
        : service_(transports, peripheral_registry_) {
        services_.push_back(&service_);
        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start(beebium::service::Provenance{},
                       beebium::service::MachineIdentity{},
                       /*enable_advertisement=*/false,
                       /*policy_config=*/{},
                       /*shutdown_callback=*/nullptr,
                       std::span<grpc::Service*>(services_));
        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::ExtensionRpc::NewStub(channel_);
    }

    beebium::ExtensionRpc::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::ExtensionRegistry peripheral_registry_;
    beebium::service::ExtensionRpcServiceImpl service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionRpc::Stub> stub_;
};

beebium::InvokeRequest make_request(const std::string& service,
                                    const std::string& method,
                                    const std::string& payload,
                                    const std::string& ext_id = "") {
    beebium::InvokeRequest req;
    req.set_extension_id(ext_id);
    req.set_service(service);
    req.set_method(method);
    req.set_payload(payload);
    return req;
}

}  // namespace

TEST_CASE("ExtensionRpc unary call routes by service name and echoes",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status = fix.stub().Invoke(&ctx, make_request("Echo", "Echo", "hello"), &resp);
    REQUIRE(status.ok());
    CHECK(resp.payload() == "hello");
}

TEST_CASE("ExtensionRpc maps an unknown service to NOT_FOUND",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status = fix.stub().Invoke(&ctx, make_request("Nope", "Echo", ""), &resp);
    CHECK(status.error_code() == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("ExtensionRpc surfaces a dispatcher's non-OK status as the gRPC status",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status = fix.stub().Invoke(&ctx, make_request("Echo", "Fail", ""), &resp);
    CHECK(status.error_code() == grpc::StatusCode::PERMISSION_DENIED);
    CHECK(status.error_message() == "denied");
}

TEST_CASE("ExtensionRpc routes by explicit instance id", "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("echo-2", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status =
        fix.stub().Invoke(&ctx, make_request("Echo", "Echo", "x", "echo-2"), &resp);
    REQUIRE(status.ok());
    CHECK(resp.payload() == "x");
}

TEST_CASE("ExtensionRpc rejects an ambiguous service without an instance id",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("echo-2", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status = fix.stub().Invoke(&ctx, make_request("Echo", "Echo", ""), &resp);
    CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
}

TEST_CASE("ExtensionRpc addresses each of two transports behind one service name by id",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("echo-2", "Echo"));
    ExtensionRpcFixture fix(transports);

    for (const std::string id : {"echo-1", "echo-2"}) {
        grpc::ClientContext ctx;
        beebium::InvokeResponse resp;
        auto status =
            fix.stub().Invoke(&ctx, make_request("Echo", "WhoAmI", "", id), &resp);
        REQUIRE(status.ok());
        CHECK(resp.payload() == id);
    }
}

TEST_CASE("ExtensionRpc server-stream reaches the transport instance named by id",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("echo-2", "Echo"));
    ExtensionRpcFixture fix(transports);

    for (const std::string id : {"echo-1", "echo-2"}) {
        grpc::ClientContext ctx;
        auto reader =
            fix.stub().ServerStream(&ctx, make_request("Echo", "WhoStream", "", id));
        beebium::InvokeResponse resp;
        REQUIRE(reader->Read(&resp));
        CHECK(resp.payload() == id);
        CHECK_FALSE(reader->Read(&resp));
        CHECK(reader->Finish().ok());
    }
}

TEST_CASE("ExtensionRpc addresses registry-assigned transport ids",
          "[grpc][extension-rpc]") {
    // No explicit id: the registry assigns each instance a unique one, the
    // same id EconetTransportService reports, and ExtensionRpc routes by it.
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("", "Echo"));
    const std::string first_id(transports.extensions()[0]->id());
    const std::string second_id(transports.extensions()[1]->id());
    REQUIRE_FALSE(first_id.empty());
    REQUIRE(first_id != second_id);
    ExtensionRpcFixture fix(transports);

    for (const std::string& id : {first_id, second_id}) {
        grpc::ClientContext ctx;
        beebium::InvokeResponse resp;
        auto status =
            fix.stub().Invoke(&ctx, make_request("Echo", "Echo", "x", id), &resp);
        CHECK(status.ok());
    }
}

TEST_CASE("ExtensionRpc ambiguity error names the candidate instance ids",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("echo-2", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("other-1", "Other"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status = fix.stub().Invoke(&ctx, make_request("Echo", "WhoAmI", ""), &resp);
    CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
    const std::string message = status.error_message();
    CHECK(message.find("echo-1") != std::string::npos);
    CHECK(message.find("echo-2") != std::string::npos);
    CHECK(message.find("other-1") == std::string::npos);

    // The unambiguous service still routes by name alone.
    grpc::ClientContext ctx2;
    auto ok = fix.stub().Invoke(&ctx2, make_request("Other", "WhoAmI", ""), &resp);
    REQUIRE(ok.ok());
    CHECK(resp.payload() == "other-1");
}

TEST_CASE("ExtensionRpc ambiguous server-stream is rejected with the candidate ids",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    transports.add(std::make_unique<FakeRpcTransport>("echo-2", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    auto reader = fix.stub().ServerStream(&ctx, make_request("Echo", "WhoStream", ""));
    beebium::InvokeResponse resp;
    CHECK_FALSE(reader->Read(&resp));
    auto status = reader->Finish();
    CHECK(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION);
    CHECK(status.error_message().find("echo-1") != std::string::npos);
    CHECK(status.error_message().find("echo-2") != std::string::npos);
}

TEST_CASE("ExtensionRpc reports an unknown instance id as NOT_FOUND",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    beebium::InvokeResponse resp;
    auto status =
        fix.stub().Invoke(&ctx, make_request("Echo", "Echo", "", "nope"), &resp);
    CHECK(status.error_code() == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("ExtensionRpc server-streams a dispatcher's responses",
          "[grpc][extension-rpc]") {
    beebium::EconetTransportRegistry transports;
    transports.add(std::make_unique<FakeRpcTransport>("echo-1", "Echo"));
    ExtensionRpcFixture fix(transports);

    grpc::ClientContext ctx;
    const std::string count(1, static_cast<char>(5));  // ask for 5
    auto reader = fix.stub().ServerStream(&ctx, make_request("Echo", "Count", count));
    beebium::InvokeResponse resp;
    int received = 0;
    while (reader->Read(&resp)) {
        ++received;
    }
    CHECK(reader->Finish().ok());
    CHECK(received == 5);
}
