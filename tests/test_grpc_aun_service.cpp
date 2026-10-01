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

// End-to-end test for the AUN-specific RPCs, tunnelled through the core's
// generic ExtensionRpc channel. Spins up a real gRPC server with the
// ExtensionRpc service registered over a registry holding the AUN extension,
// installs an AunBackend on an OS-chosen port, and drives the AUN operations
// (peer table management, cable plug, status query) by serializing each
// request, calling ExtensionRpc.Invoke with service="AunService", and parsing
// the reply -- exactly as the Python/TS clients do over the wire.

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "beebium/Machines.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/service/Server.hpp"

#include "aun.pb.h"
#include "extension_rpc.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <memory>
#include <span>
#include <string>

namespace {

class AunServiceFixture {
public:
    AunServiceFixture() : service_(transports_, peripherals_) {
        machine_.reset();

        // Build the AUN extension on an OS-assigned ephemeral port and hand its
        // backend to EconetSocket so the dispatcher has something to talk to.
        auto ext = std::make_unique<beebium::AunEconetTransportExtension>();
        ext->set_config({{"port", "0"}});
        auto backend = ext->create_backend(/*station=*/1);
        REQUIRE(backend != nullptr);
        machine_.state().memory.econet_socket.enable(
            /*station=*/1, std::move(backend), /*aun_mode=*/true);
        ext_ = ext.get();
        transports_.add(std::move(ext));

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

    ~AunServiceFixture() { server_->stop(); }

    // Serialize `req`, tunnel it via ExtensionRpc.Invoke (service=AunService,
    // the named method), and parse the reply into `resp`.
    template <typename Req, typename Resp>
    grpc::Status invoke(const std::string& method, const Req& req, Resp* resp) {
        grpc::ClientContext ctx;
        beebium::InvokeRequest ireq;
        ireq.set_service("AunService");
        ireq.set_method(method);
        ireq.set_payload(req.SerializeAsString());
        beebium::InvokeResponse iresp;
        grpc::Status status = stub_->Invoke(&ctx, ireq, &iresp);
        if (status.ok() && resp != nullptr) {
            REQUIRE(resp->ParseFromString(iresp.payload()));
        }
        return status;
    }

    // Reach the underlying AunBackend to simulate a discovered peer without
    // going through real mDNS.
    beebium::AunEconetTransportExtension& extension() { return *ext_; }

private:
    beebium::ModelB machine_;
    beebium::EconetTransportRegistry transports_;
    beebium::ExtensionRegistry peripherals_;
    beebium::service::ExtensionRpcServiceImpl service_;
    beebium::AunEconetTransportExtension* ext_ = nullptr;  // owned by transports_
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionRpc::Stub> stub_;
};

}  // namespace

TEST_CASE("AunService GetStatus on a freshly-bound AUN backend",
          "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    beebium::AunGetStatusRequest request;
    beebium::AunGetStatusResponse response;
    REQUIRE(fixture.invoke("GetStatus", request, &response).ok());
    REQUIRE(response.connected());
    REQUIRE(response.local_port() != 0);
    REQUIRE(response.peer_count() == 0);
}

TEST_CASE("AunService AddPeer then ListPeers", "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    {
        beebium::AunAddPeerRequest request;
        request.set_net(0);
        request.set_stn(254);
        request.set_ip_address("127.0.0.1");
        request.set_port(40001);
        beebium::AunAddPeerResponse response;
        REQUIRE(fixture.invoke("AddPeer", request, &response).ok());
        REQUIRE(response.success());
    }

    {
        beebium::AunListPeersRequest request;
        beebium::AunListPeersResponse response;
        REQUIRE(fixture.invoke("ListPeers", request, &response).ok());
        REQUIRE(response.peers_size() == 1);
        const auto& peer = response.peers(0);
        REQUIRE(peer.net() == 0);
        REQUIRE(peer.stn() == 254);
        REQUIRE(peer.ip_address() == "127.0.0.1");
        REQUIRE(peer.port() == 40001);
        CHECK(peer.source() == beebium::AUN_PEER_SOURCE_API);
    }
}

TEST_CASE("AunService ListPeers reports source for discovered entries",
          "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    // Inject a discovered peer through the transport's peer set, mimicking what
    // AunDiscoverySubscriber would do on receipt of an mDNS announcement.
    fixture.extension().peer_set().set_peer(
        0, 200, htonl(INADDR_LOOPBACK), 50001,
        beebium::AunPeerProvenance::Discovered);

    beebium::AunListPeersRequest request;
    beebium::AunListPeersResponse response;
    REQUIRE(fixture.invoke("ListPeers", request, &response).ok());
    REQUIRE(response.peers_size() == 1);
    const auto& peer = response.peers(0);
    CHECK(peer.stn() == 200);
    CHECK(peer.source() == beebium::AUN_PEER_SOURCE_DISCOVERED);
}

TEST_CASE("AunService AddPeer with default port (0) substitutes AUN_DEFAULT_PORT",
          "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    beebium::AunAddPeerRequest add_req;
    add_req.set_net(0);
    add_req.set_stn(254);
    add_req.set_ip_address("127.0.0.1");
    add_req.set_port(0);  // request default
    beebium::AunAddPeerResponse add_resp;
    REQUIRE(fixture.invoke("AddPeer", add_req, &add_resp).ok());
    REQUIRE(add_resp.success());

    beebium::AunListPeersRequest list_req;
    beebium::AunListPeersResponse list_resp;
    REQUIRE(fixture.invoke("ListPeers", list_req, &list_resp).ok());
    REQUIRE(list_resp.peers_size() == 1);
    REQUIRE(list_resp.peers(0).port() == 32768);  // AUN_DEFAULT_PORT
}

TEST_CASE("AunService RemovePeer", "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    {
        beebium::AunAddPeerRequest req;
        req.set_net(0); req.set_stn(254);
        req.set_ip_address("127.0.0.1"); req.set_port(40001);
        beebium::AunAddPeerResponse resp;
        REQUIRE(fixture.invoke("AddPeer", req, &resp).ok());
    }

    {
        beebium::AunRemovePeerRequest req;
        req.set_net(0); req.set_stn(254);
        beebium::AunRemovePeerResponse resp;
        REQUIRE(fixture.invoke("RemovePeer", req, &resp).ok());
        REQUIRE(resp.success());
    }

    {
        beebium::AunListPeersRequest req;
        beebium::AunListPeersResponse resp;
        REQUIRE(fixture.invoke("ListPeers", req, &resp).ok());
        REQUIRE(resp.peers_size() == 0);
    }
}

TEST_CASE("AunService SetConnected toggles backend state",
          "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    {
        beebium::AunSetConnectedRequest req;
        req.set_connected(false);
        beebium::AunSetConnectedResponse resp;
        REQUIRE(fixture.invoke("SetConnected", req, &resp).ok());
        REQUIRE(resp.success());
    }
    {
        beebium::AunGetStatusRequest req;
        beebium::AunGetStatusResponse resp;
        REQUIRE(fixture.invoke("GetStatus", req, &resp).ok());
        REQUIRE_FALSE(resp.connected());
    }
    {
        beebium::AunSetConnectedRequest req;
        req.set_connected(true);
        beebium::AunSetConnectedResponse resp;
        REQUIRE(fixture.invoke("SetConnected", req, &resp).ok());
    }
    {
        beebium::AunGetStatusRequest req;
        beebium::AunGetStatusResponse resp;
        REQUIRE(fixture.invoke("GetStatus", req, &resp).ok());
        REQUIRE(resp.connected());
    }
}

TEST_CASE("AunService AddPeer rejects invalid IP", "[grpc][aun][extension-rpc]") {
    AunServiceFixture fixture;

    beebium::AunAddPeerRequest req;
    req.set_net(0); req.set_stn(254);
    req.set_ip_address("not-an-ip"); req.set_port(40001);
    beebium::AunAddPeerResponse resp;
    REQUIRE(fixture.invoke("AddPeer", req, &resp).ok());
    REQUIRE_FALSE(resp.success());
    REQUIRE(resp.error().find("invalid") != std::string::npos);
}
