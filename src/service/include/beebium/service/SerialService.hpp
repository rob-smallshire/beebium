// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#ifndef BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
#define BEEBIUM_SERVICE_SERIAL_SERVICE_HPP

#include "serial.grpc.pb.h"
#include "beebium/serial/SerialConcepts.hpp"
#include "beebium/serial/SerialSocket.hpp"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <string>
#include <thread>

namespace beebium::service {

using beebium::HasSerialSocket;

// gRPC service reporting the BBC's on-board serial hardware status (MC6850 ACIA
// + Serial ULA). What is attached to the far end of the serial port is owned by
// a serial PeripheralExtension (host-serial, rpc-serial, loopback-serial); this
// service only observes the chip registers and never attaches a device.
//
// GetSerialStatus reads the ACIA/ULA registers without pausing the emulation
// thread: it is a benign racy snapshot of a few scalar fields, sufficient for a
// status display.
template<typename MachineType>
class SerialServiceImpl final : public SerialService::Service {
public:
    explicit SerialServiceImpl(MachineType& machine) : machine_(machine) {}

    SerialServiceImpl(const SerialServiceImpl&) = delete;
    SerialServiceImpl& operator=(const SerialServiceImpl&) = delete;

    grpc::Status GetSerialStatus(
        grpc::ServerContext* /*context*/,
        const GetSerialStatusRequest* /*request*/,
        SerialStatus* response) override
    {
        populate_status_(*response);
        return grpc::Status::OK;
    }

    // Server-pushed status stream: an initial snapshot, then a fresh one whenever
    // the chip state changes. Sampled at min_interval_ms (default 50ms) so the
    // per-byte TDRE/RDRF toggling during a transfer is coalesced rather than
    // flooding the client. Reads are a benign racy snapshot (no machine pause).
    grpc::Status WatchSerialStatus(
        grpc::ServerContext* context,
        const WatchSerialStatusRequest* request,
        grpc::ServerWriter<SerialStatus>* writer) override
    {
        const auto interval = std::chrono::milliseconds(
            request->min_interval_ms() > 0 ? request->min_interval_ms() : 50);

        SerialStatus snapshot;
        populate_status_(snapshot);
        std::string last = snapshot.SerializeAsString();
        if (!writer->Write(snapshot)) {
            return grpc::Status::OK;  // Client disconnected during initial push.
        }

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(interval);

            SerialStatus current;
            populate_status_(current);
            std::string serialized = current.SerializeAsString();
            if (serialized == last) {
                continue;
            }
            if (!writer->Write(current)) {
                break;  // Client disconnected.
            }
            last = std::move(serialized);
        }
        return grpc::Status::OK;
    }

private:
    void populate_status_(SerialStatus& status) {
        using Memory = typename MachineType::Memory;
        if constexpr (!HasSerialSocket<Memory>) {
            status.set_has_serial_socket(false);
        } else {
            status.set_has_serial_socket(true);
            status.set_connector(
                std::string(beebium::serial_connector_label<Memory>()));
            auto& serial = machine_.state().memory.serial_socket;
            const auto& acia = serial.acia();
            const auto& ula = serial.ula();

            status.set_acia_control(acia.control());
            status.set_acia_status(acia.status());
            status.set_tdre(acia.tdre());
            status.set_rdrf(acia.rdrf());
            status.set_not_dcd(acia.not_dcd());
            status.set_not_cts(acia.not_cts());
            status.set_irq_pending(acia.irq_pending());

            status.set_ula_control(ula.control());
            status.set_tx_baud(ula.tx_baud());
            status.set_rx_baud(ula.rx_baud());
            status.set_rs423_selected(ula.rs423_selected());
            status.set_motor_on(ula.motor_on());
            status.set_tx_ula_divider(ula.tx_divider());
            status.set_rx_ula_divider(ula.rx_divider());
        }
    }

    MachineType& machine_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_SERIAL_SERVICE_HPP
