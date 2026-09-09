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

#ifndef BEEBIUM_SCSI_HOST_ADAPTER_DISPATCHER_HPP
#define BEEBIUM_SCSI_HOST_ADAPTER_DISPATCHER_HPP

#include "AcornScsiHostAdapter.hpp"
#include "ScsiBusEventBuffer.hpp"
#include "ScsiConstants.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "scsi_host_adapter.pb.h"

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the Acorn SCSI host adapter: target
// enumeration, bus status, and a streamed bus-event tap, served through the
// core's ExtensionRpc channel rather than a plugin-hosted gRPC service. The
// plugin links protobuf (for these messages) but not gRPC.
// See docs/discussion/extension-rpc-channel.md.
class ScsiHostAdapterDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit ScsiHostAdapterDispatcher(AcornScsiHostAdapter& adapter)
        : adapter_(adapter) {}

    std::string_view service_name() const override {
        return "ScsiHostAdapterService";
    }

    RpcStatus invoke(std::string_view method, std::string_view /*request*/,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "ListTargets") {
            ListScsiTargetsResponse resp;
            for (const auto& info : adapter_.target_registry().enumerate()) {
                auto* target = resp.add_targets();
                target->set_id(info.id);
                target->set_present(info.present);
                target->set_device_type(std::string(info.device_type));
                target->set_description(std::string(info.description));
            }
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "GetBusStatus") {
            GetScsiBusStatusResponse resp;
            resp.set_phase(std::string(scsi_phase_name(adapter_.bus().phase())));
            resp.set_selected_target(adapter_.bus().selected_target_id());
            resp.set_status_register(adapter_.bus().status_register());
            resp.set_irq_pending(adapter_.bus().irq_pending());
            return serialized(resp.SerializeToString(&response));
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "ScsiHostAdapterService has no method '" + std::string(method) + "'");
    }

    RpcStatus server_stream(std::string_view method, std::string_view request,
                            RpcResponseWriter& writer, RpcContext& ctx) override {
        if (method != "WatchBusEvents") {
            return RpcStatus::error(
                kRpcUnimplemented,
                "ScsiHostAdapterService has no streaming method '" +
                    std::string(method) + "'");
        }
        WatchScsiBusEventsRequest req;
        if (!req.ParseFromArray(request.data(), static_cast<int>(request.size()))) {
            return RpcStatus::error(
                kRpcInvalidArgument,
                "malformed ScsiHostAdapterService.WatchBusEvents request");
        }

        // Attach an event buffer to the bus for the duration of this stream.
        // The emulation thread reads the buffer pointer and pushes into it every
        // cycle, so arm and detach it with that thread halted: the store must
        // not race the read, and -- crucially -- the detach must complete before
        // `buffer` (a stack local) is destroyed, so the emulation thread can
        // never push into freed storage after this handler returns.
        ScsiBusEventBuffer buffer;
        const bool include_register_access = req.include_register_access();
        with_bus_stopped([&] {
            adapter_.bus().set_event_buffer(&buffer);
            adapter_.bus().set_event_register_access(include_register_access);
        });

        ScsiInternalEvent internal_event;
        while (buffer.pop(internal_event)) {
            if (ctx.is_cancelled()) break;
            ScsiBusEvent proto_event;
            to_proto(internal_event, proto_event);
            if (!writer.write(proto_event.SerializeAsString())) break;
        }

        with_bus_stopped([&] {
            adapter_.bus().set_event_buffer(nullptr);
            adapter_.bus().set_event_register_access(false);
        });
        return RpcStatus::ok();
    }

private:
    static void to_proto(const ScsiInternalEvent& internal_event,
                         ScsiBusEvent& proto_event) {
        std::visit([&proto_event](auto&& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, ScsiPhaseChangeEvent>) {
                auto* e = proto_event.mutable_phase_change();
                e->set_from_phase(ev.from_phase);
                e->set_to_phase(ev.to_phase);
            } else if constexpr (std::is_same_v<T, ScsiSelectionEvent>) {
                auto* e = proto_event.mutable_selection();
                e->set_target_id(ev.target_id);
                e->set_success(ev.success);
            } else if constexpr (std::is_same_v<T, ScsiCommandEvent>) {
                auto* e = proto_event.mutable_command();
                e->set_target_id(ev.target_id);
                e->set_opcode(ev.opcode);
                e->set_opcode_name(ev.opcode_name);
                e->set_cdb(std::string(ev.cdb.begin(), ev.cdb.end()));
                e->set_lba(ev.lba);
                e->set_block_count(ev.block_count);
            } else if constexpr (std::is_same_v<T, ScsiDataTransferEvent>) {
                auto* e = proto_event.mutable_data_transfer();
                e->set_direction(ev.direction);
                e->set_bytes_expected(ev.bytes_expected);
                e->set_bytes_transferred(ev.bytes_transferred);
                e->set_complete(ev.complete);
            } else if constexpr (std::is_same_v<T, ScsiStatusEvent>) {
                auto* e = proto_event.mutable_status();
                e->set_target_id(ev.target_id);
                e->set_status_byte(ev.status_byte);
                e->set_status_name(ev.status_name);
                e->set_message_byte(ev.message_byte);
            } else if constexpr (std::is_same_v<T, ScsiRegisterAccessEvent>) {
                auto* e = proto_event.mutable_register_access();
                e->set_operation(ev.operation);
                e->set_register_index(ev.register_index);
                e->set_register_name(ev.register_name);
                e->set_value(ev.value);
                e->set_phase(ev.phase);
            }
        }, internal_event);
    }

    AcornScsiHostAdapter& adapter_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HOST_ADAPTER_DISPATCHER_HPP
