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

#ifndef BEEBIUM_ACORN_RTC_DISPATCHER_HPP
#define BEEBIUM_ACORN_RTC_DISPATCHER_HPP

#include "Saf3019p.hpp"
#include "TimeParser.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "acorn_rtc.pb.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace beebium {

// Hand-written ExtensionRpcDispatcher for the Acorn RTC: register peek/poke,
// time get/set, and a streamed activity feed, served through the core's
// ExtensionRpc channel rather than a plugin-hosted gRPC service. The plugin
// links protobuf (for these messages) but not gRPC. The activity-event queue
// (fed by the chip's callback) lives here, as it did in the old service.
// See docs/discussion/extension-rpc-channel.md.
class AcornRtcDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit AcornRtcDispatcher(Saf3019p& chip) : chip_(chip) {
        chip_.set_activity_callback(
            [this](bool is_read, int reg, std::uint8_t value) {
                push_activity_event(is_read, reg, value);
            });
    }

    ~AcornRtcDispatcher() override { chip_.set_activity_callback(nullptr); }

    std::string_view service_name() const override { return "AcornRtcService"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "GetTime") {
            // The chip is memory-mapped; the emulation thread reads and clocks
            // it every cycle. Advance and snapshot it with that thread halted so
            // the counters cannot tick between advance and read.
            Saf3019p::DateTime dt;
            with_bus_stopped([&] {
                chip_.advance_counters();
                dt = chip_.current_datetime();
            });
            GetRtcTimeResponse resp;
            resp.set_year(dt.year);
            resp.set_month(dt.month);
            resp.set_day(dt.day);
            resp.set_hour(dt.hour);
            resp.set_minute(dt.minute);
            std::ostringstream iso;
            iso << dt.year << "-" << std::setfill('0') << std::setw(2) << dt.month
                << "-" << std::setfill('0') << std::setw(2) << dt.day << "T"
                << std::setfill('0') << std::setw(2) << dt.hour << ":"
                << std::setfill('0') << std::setw(2) << dt.minute;
            resp.set_iso8601(iso.str());
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "SetTime") {
            SetRtcTimeRequest req;
            if (!parse(request, req)) return bad_request("SetTime");
            Saf3019p::DateTime dt;
            try {
                dt = parse_absolute_time(req.iso8601());
            } catch (const std::invalid_argument& e) {
                return RpcStatus::error(kRpcInvalidArgument, e.what());
            }
            // initialise() validates the year against the active register
            // layout's representable range (which differs per layout), so let
            // it be the single source of truth rather than a fixed cap here.
            // It rewrites the chip's registers, so halt the emulation thread.
            bool initialised = false;
            with_bus_stopped([&] { initialised = chip_.initialise(dt); });
            if (!initialised) {
                auto [min_year, max_year] = Saf3019p::year_range(chip_.layout());
                return RpcStatus::error(
                    kRpcInvalidArgument,
                    "Year " + std::to_string(dt.year) +
                        " cannot be represented by the SAF3019P in its current "
                        "register layout (valid range: " +
                        std::to_string(min_year) + "-" +
                        std::to_string(max_year) + ")");
            }
            SetRtcTimeResponse resp;
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "GetRegisters") {
            // Advance and read all eight registers as one snapshot with the
            // emulation thread halted.
            std::uint8_t regs[8];
            with_bus_stopped([&] {
                chip_.advance_counters();
                for (int i = 0; i < 8; i++) {
                    regs[i] = chip_.read_register(i);
                }
            });
            GetRtcRegistersResponse resp;
            for (int i = 0; i < 8; i++) {
                resp.add_registers(regs[i]);
            }
            return serialized(resp.SerializeToString(&response));
        }
        if (method == "SetRegister") {
            SetRtcRegisterRequest req;
            if (!parse(request, req)) return bad_request("SetRegister");
            auto reg = static_cast<int>(req.register_index());
            if (reg < 0 || reg > 7) {
                return RpcStatus::error(
                    kRpcInvalidArgument,
                    "Register index must be 0-7, got " + std::to_string(reg));
            }
            with_bus_stopped([&] {
                chip_.write_register(reg, static_cast<std::uint8_t>(req.bcd_value()));
            });
            SetRtcRegisterResponse resp;
            return serialized(resp.SerializeToString(&response));
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "AcornRtcService has no method '" + std::string(method) + "'");
    }

    RpcStatus server_stream(std::string_view method, std::string_view /*request*/,
                            RpcResponseWriter& writer, RpcContext& ctx) override {
        if (method != "WatchActivity") {
            return RpcStatus::error(
                kRpcUnimplemented,
                "AcornRtcService has no streaming method '" + std::string(method) +
                    "'");
        }
        while (!ctx.is_cancelled()) {
            RtcActivityEvent event;
            {
                std::unique_lock lock(event_mutex_);
                event_cv_.wait_for(lock, std::chrono::milliseconds(100),
                                   [this] { return !event_queue_.empty(); });
                if (event_queue_.empty()) continue;
                event = std::move(event_queue_.front());
                event_queue_.pop_front();
            }
            if (!writer.write(event.SerializeAsString())) break;
        }
        return RpcStatus::ok();
    }

private:
    template <typename Msg>
    static bool parse(std::string_view bytes, Msg& out) {
        return out.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    static RpcStatus bad_request(const char* method) {
        return RpcStatus::error(
            kRpcInvalidArgument,
            std::string("malformed AcornRtcService.") + method + " request");
    }

    void push_activity_event(bool is_read, int reg, std::uint8_t value) {
        auto now = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      now.time_since_epoch())
                      .count();
        RtcActivityEvent event;
        event.set_type(is_read ? RtcActivityEvent::REGISTER_READ
                               : RtcActivityEvent::REGISTER_WRITE);
        event.set_register_number(reg);
        event.set_value(value);
        event.set_timestamp_us(us);
        {
            std::lock_guard lock(event_mutex_);
            if (event_queue_.size() >= kMaxQueuedEvents) {
                event_queue_.pop_front();
            }
            event_queue_.push_back(std::move(event));
        }
        event_cv_.notify_one();
    }

    Saf3019p& chip_;

    static constexpr std::size_t kMaxQueuedEvents = 256;
    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::deque<RtcActivityEvent> event_queue_;
};

}  // namespace beebium

#endif  // BEEBIUM_ACORN_RTC_DISPATCHER_HPP
