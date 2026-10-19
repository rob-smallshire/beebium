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

#ifndef BEEBIUM_SERVICE_TUBE_SERVICE_HPP
#define BEEBIUM_SERVICE_TUBE_SERVICE_HPP

#include "tube.grpc.pb.h"
#include "beebium/extension/CoprocessorExtension.hpp"
#include "beebium/tube/TubeConcepts.hpp"
#include "beebium/tube/TubeSocket.hpp"

#include <grpcpp/grpcpp.h>
#include <string>

namespace beebium::service {

// TubeService implementation: reports the Tube and the coprocessor plugin
// attached to it at launch.
template<typename MachineType>
class TubeServiceImpl final : public TubeService::Service {
public:
    // `coprocessor` is the extension attached at launch, or nullptr; it must
    // outlive the service.
    TubeServiceImpl(MachineType& machine, const CoprocessorExtension* coprocessor)
        : machine_(machine), coprocessor_(coprocessor) {}

    ~TubeServiceImpl() override = default;

    TubeServiceImpl(const TubeServiceImpl&) = delete;
    TubeServiceImpl& operator=(const TubeServiceImpl&) = delete;

    grpc::Status GetStatus(
        grpc::ServerContext* /*context*/,
        const GetTubeStatusRequest* /*request*/,
        GetTubeStatusResponse* response) override
    {
        using Memory = typename MachineType::Memory;

        if constexpr (!HasTubeSocket<Memory>) {
            response->set_has_tube_socket(false);
        } else {
            response->set_has_tube_socket(true);
            response->set_enabled(machine_.state().memory.tube_socket.enabled());
            if (coprocessor_) {
                response->set_coprocessor_connected(true);
                response->set_coprocessor_cpu(coprocessor_->cpu_name());
                response->set_coprocessor_clock_hz(coprocessor_->clock_hz());
                response->set_coprocessor_extension_id(std::string(coprocessor_->id()));
            }
        }
        return grpc::Status::OK;
    }

private:
    MachineType& machine_;
    const CoprocessorExtension* coprocessor_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_TUBE_SERVICE_HPP
