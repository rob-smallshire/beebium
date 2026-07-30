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

#ifndef BEEBIUM_SERVICE_ECONET_SERVICE_HPP
#define BEEBIUM_SERVICE_ECONET_SERVICE_HPP

#include <beebium/econet/ObservableBackend.hpp>
#include "econet.grpc.pb.h"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/EconetSocket.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/econet/AunPacket.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/econet/Mc6854.hpp"
#include "beebium/econet/FourWayHandshake.hpp"

#include <grpcpp/grpcpp.h>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace beebium::service {

using beebium::HasEconetSocket;

namespace {

inline std::string frame_field_to_string(Mc6854::FrameField field) {
    switch (field) {
        case Mc6854::FrameField::Idle:     return "idle";
        case Mc6854::FrameField::Flag:     return "flag";
        case Mc6854::FrameField::Address:  return "address";
        case Mc6854::FrameField::Control:  return "control";
        case Mc6854::FrameField::ExtCtrl:  return "ext_ctrl";
        case Mc6854::FrameField::Lcf:      return "lcf";
        case Mc6854::FrameField::Data:     return "data";
    }
    return "unknown";
}

inline std::string handshake_stage_to_string(FourWayHandshake::Stage stage) {
    switch (stage) {
        case FourWayHandshake::Stage::Idle:              return "idle";
        case FourWayHandshake::Stage::ScoutSent:         return "scout_sent";
        case FourWayHandshake::Stage::ScoutAckReceived:  return "scout_ack_received";
        case FourWayHandshake::Stage::DataSent:          return "data_sent";
        case FourWayHandshake::Stage::WaitForIdle:       return "wait_for_idle";
        case FourWayHandshake::Stage::ScoutReceived:     return "scout_received";
        case FourWayHandshake::Stage::ScoutAckSent:      return "scout_ack_sent";
        case FourWayHandshake::Stage::DataReceived:      return "data_received";
        case FourWayHandshake::Stage::ImmediateSent:     return "immediate_sent";
        case FourWayHandshake::Stage::ImmediateReceived: return "immediate_received";
    }
    return "unknown";
}

inline std::string ip_addr_to_string(uint32_t ip_addr) {
    char buf[INET_ADDRSTRLEN];
    struct in_addr addr;
    addr.s_addr = ip_addr;
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
        return buf;
    }
    return "0.0.0.0";
}

}  // anonymous namespace

template<typename MachineType>
class EconetServiceImpl final : public EconetService::Service {
public:
    explicit EconetServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~EconetServiceImpl() override = default;

    EconetServiceImpl(const EconetServiceImpl&) = delete;
    EconetServiceImpl& operator=(const EconetServiceImpl&) = delete;

    grpc::Status GetEconetStatus(
        grpc::ServerContext* context,
        const GetEconetStatusRequest* request,
        GetEconetStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);
        populate_status_(*response);
        return grpc::Status::OK;
    }

    grpc::Status WatchEconetStatus(
        grpc::ServerContext* context,
        const WatchEconetStatusRequest* request,
        grpc::ServerWriter<GetEconetStatusResponse>* writer) override
    {
        using Memory = typename MachineType::Memory;

        auto interval = std::chrono::milliseconds(
            request->min_interval_ms() > 0 ? request->min_interval_ms() : 50);

        // Initial push: send a snapshot immediately so the client has
        // something to render without waiting for the first state change.
        uint64_t last_seq = 0;
        {
            GetEconetStatusResponse snapshot;
            std::lock_guard<std::mutex> lock(mutex_);
            populate_status_(snapshot);
            if constexpr (HasEconetSocket<Memory>) {
                last_seq = machine_.state().memory.econet_socket.status_sequence();
            }
            if (!writer->Write(snapshot)) {
                return grpc::Status::OK;  // Client disconnected during initial push.
            }
        }

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(interval);

            uint64_t curr_seq = 0;
            if constexpr (HasEconetSocket<Memory>) {
                curr_seq = machine_.state().memory.econet_socket.status_sequence();
            }
            if (curr_seq == last_seq) {
                continue;
            }

            GetEconetStatusResponse snapshot;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                populate_status_(snapshot);
            }
            if (!writer->Write(snapshot)) {
                break;  // Client disconnected.
            }
            last_seq = curr_seq;
        }

        return grpc::Status::OK;
    }

    grpc::Status EnableEconet(
        grpc::ServerContext* context,
        const EnableEconetRequest* request,
        EnableEconetResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasEconetSocket<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no Econet socket");
            return grpc::Status::OK;
        } else {
            auto& econet = machine_.state().memory.econet_socket;

            if (econet.enabled()) {
                response->set_success(false);
                response->set_error("Econet already enabled");
                return grpc::Status::OK;
            }

            uint32_t station = request->station_id();
            if (station < 1 || station > 254) {
                response->set_success(false);
                response->set_error("Station number must be between 1 and 254");
                return grpc::Status::OK;
            }

            auto station_id = static_cast<uint8_t>(station);

            if (request->no_network()) {
                // Fit hardware with no network connection
                auto backend = std::make_unique<TestBackend>();
                backend->set_connected(false);
                econet.enable(station_id, std::move(backend), true);
                response->set_success(true);
                return grpc::Status::OK;
            }

            // AUN networking: create UDP socket
            uint16_t port = (request->aun_port() == 0)
                ? AUN_DEFAULT_PORT
                : static_cast<uint16_t>(request->aun_port());

            auto backend = std::make_unique<AunBackend>(0, station_id, port);

            if (!backend->is_connected()) {
                response->set_success(false);
                response->set_error("Failed to bind AUN socket to port " + std::to_string(port));
                return grpc::Status::OK;
            }

            response->set_actual_aun_port(backend->local_port());
            econet.enable(station_id, std::move(backend), true);
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status DisableEconet(
        grpc::ServerContext* context,
        const DisableEconetRequest* request,
        DisableEconetResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasEconetSocket<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no Econet socket");
            return grpc::Status::OK;
        } else {
            auto& econet = machine_.state().memory.econet_socket;

            if (!econet.enabled()) {
                response->set_success(false);
                response->set_error("Econet is not enabled");
                return grpc::Status::OK;
            }

            econet.disable();
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status SetStationId(
        grpc::ServerContext* context,
        const SetStationIdRequest* request,
        SetStationIdResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasEconetSocket<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no Econet socket");
            return grpc::Status::OK;
        } else {
            auto& econet = machine_.state().memory.econet_socket;

            if (!econet.enabled()) {
                response->set_success(false);
                response->set_error("Econet is not enabled");
                return grpc::Status::OK;
            }

            uint32_t station = request->station_id();
            if (station < 1 || station > 254) {
                response->set_success(false);
                response->set_error("Station number must be between 1 and 254");
                return grpc::Status::OK;
            }

            econet.set_station_id(static_cast<uint8_t>(station));
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status SubscribeEconetEvents(
        grpc::ServerContext* context,
        const SubscribeEconetEventsRequest* request,
        grpc::ServerWriter<EconetEvent>* writer) override
    {
        using Memory = typename MachineType::Memory;

        if constexpr (!HasEconetSocket<Memory>) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                "This machine has no Econet socket");
        } else {
            auto* observable =
                machine_.state().memory.econet_socket.observable();
            if (observable == nullptr) {
                return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                                    "Econet hardware is not fitted");
            }

            auto interval = std::chrono::milliseconds(
                request->min_interval_ms() > 0 ? request->min_interval_ms() : 20);
            const std::size_t max_batch =
                request->max_batch() > 0 ? request->max_batch() : 64;

            // Start from now rather than replaying the ring: a subscriber
            // asks what happens next, and history it never asked for would be
            // indistinguishable from live traffic.
            uint64_t next = observable->next_sequence();

            while (!context->IsCancelled()) {
                auto events = observable->collect_since(next, max_batch);
                if (events.empty()) {
                    std::this_thread::sleep_for(interval);
                    continue;
                }

                for (const auto& event : events) {
                    EconetEvent message;
                    populate_event_(event, message);
                    if (!writer->Write(message)) {
                        return grpc::Status::OK;  // Client disconnected.
                    }
                }
                next = events.back().sequence + 1;

                // Only pause once caught up, so a burst drains promptly
                // instead of being metered out one interval at a time.
                if (events.size() < max_batch) {
                    std::this_thread::sleep_for(interval);
                }
            }
            return grpc::Status::OK;
        }
    }

private:
    // Translate a recorded frame event into its wire form.
    static void populate_event_(const beebium::ObservableBackend::Event& event,
                                EconetEvent& message) {
        message.set_sequence(event.sequence);
        switch (event.type) {
            case beebium::ObservableBackend::EventType::FrameSent:
                message.set_type(ECONET_EVENT_FRAME_SENT);
                break;
            case beebium::ObservableBackend::EventType::FrameReceived:
                message.set_type(ECONET_EVENT_FRAME_RECEIVED);
                break;
            case beebium::ObservableBackend::EventType::ConnectionChanged:
                message.set_type(ECONET_EVENT_CONNECTION_CHANGE);
                message.set_connected(event.connected);
                return;
        }

        auto* frame = message.mutable_frame();
        frame->set_frame_type(frame_type_to_string(event.frame_type));
        frame->set_dest_net(event.dest_net);
        frame->set_dest_stn(event.dest_stn);
        frame->set_src_net(event.src_net);
        frame->set_src_stn(event.src_stn);
        frame->set_port(event.port);
        frame->set_control_byte(event.control_byte);
        frame->set_handle(event.handle);
        frame->set_data_length(event.data_length);
        frame->set_data(event.data, event.data_captured);
    }

    static const char* frame_type_to_string(beebium::FrameType type) {
        switch (type) {
            case beebium::FrameType::RawFrame:  return "raw";
            case beebium::FrameType::Broadcast: return "broadcast";
            case beebium::FrameType::Unicast:   return "unicast";
            case beebium::FrameType::Ack:       return "ack";
            case beebium::FrameType::Nack:      return "nack";
            case beebium::FrameType::Immediate: return "immediate";
            case beebium::FrameType::ImmReply:  return "imm_reply";
        }
        return "unknown";
    }

    // Fill `response` with the current Econet status snapshot. Caller
    // must hold mutex_ while any mutating RPC may run; this helper
    // reads state without taking the lock itself so it can be reused
    // by both the unary GetEconetStatus path and the streaming
    // WatchEconetStatus path (each of which decides when to lock).
    void populate_status_(GetEconetStatusResponse& response) {
        using Memory = typename MachineType::Memory;

        if constexpr (!HasEconetSocket<Memory>) {
            response.set_has_econet_socket(false);
            return;
        } else {
            response.set_has_econet_socket(true);

            auto& econet = machine_.state().memory.econet_socket;
            response.set_enabled(econet.enabled());

            if (!econet.enabled()) {
                return;
            }

            response.set_station_id(econet.station_id());
            response.set_aun_mode(econet.aun_mode());
            response.set_requires_real_time(econet.requires_real_time());
            response.set_gated_by_speed(econet.gated_by_speed());

            if (econet.backend()) {
                response.set_connected(econet.backend()->is_connected());
            }

            if (auto* adlc = econet.adlc()) {
                auto* adlc_status = response.mutable_adlc();
                adlc_status->set_cr1(adlc->cr1());
                adlc_status->set_cr2(adlc->cr2());
                adlc_status->set_cr3(adlc->cr3());
                adlc_status->set_cr4(adlc->cr4());
                adlc_status->set_sr1(adlc->sr1());
                adlc_status->set_sr2(adlc->sr2());
                adlc_status->set_irq_output(adlc->irq_output());
                adlc_status->set_tx_fifo_empty(adlc->tx_fifo_empty());
                adlc_status->set_tx_fifo_full(adlc->tx_fifo_full());
                adlc_status->set_rx_fifo_empty(adlc->rx_fifo_empty());
                adlc_status->set_rx_fifo_full(adlc->rx_fifo_full());
                adlc_status->set_tx_frame_field(frame_field_to_string(adlc->tx_frame_field()));
                adlc_status->set_rx_frame_field(frame_field_to_string(adlc->rx_frame_field()));
                adlc_status->set_pse_level(adlc->pse_level());
                adlc_status->set_cts_input(adlc->cts_input());
            }

            if (auto* hs = econet.handshake()) {
                auto* hs_status = response.mutable_handshake();
                hs_status->set_stage(handshake_stage_to_string(hs->stage()));
                hs_status->set_flag_fill_active(hs->flag_fill_active());
                hs_status->set_frames_held(
                    static_cast<uint32_t>(hs->held_frame_count()));
                hs_status->set_frames_redelivered(
                    hs->held_frames_redelivered_count());
                hs_status->set_frames_expired(hs->held_frames_expired_count());
                hs_status->set_frames_dropped(hs->held_frames_dropped_count());
            }

            response.set_tick_count(econet.tick_count());
            response.set_cr1_0x82_write_count(econet.cr1_0x82_write_count());
            response.set_rx_frames_received_count(econet.rx_frames_received_count());
            response.set_rx_blocked_by_reset_count(econet.rx_blocked_by_reset_count());
            response.set_scout_ack_generated_count(econet.scout_ack_generated_count());
            response.set_tx_frames_from_beeb_count(econet.tx_frames_from_beeb_count());
            response.set_unexpected_tx_reset_count(econet.unexpected_tx_reset_count());
            response.set_tx_from_idle_count(econet.tx_from_idle_count());
            response.set_max_handshake_timer_seen(econet.max_handshake_timer_seen());
            response.set_watchdog_timeout_count(econet.watchdog_timeout_count());
            response.set_send_stage_log(econet.send_stage_log_string());
            response.set_ticks_with_timer_active(econet.ticks_with_timer_active());
            response.set_read_stretch_parasite_ticks(
                machine_.memory().tube_socket.read_stretch_parasite_ticks());
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_ECONET_SERVICE_HPP
