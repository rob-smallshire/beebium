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

#ifndef BEEBIUM_SERVICE_CASSETTE_SERVICE_HPP
#define BEEBIUM_SERVICE_CASSETTE_SERVICE_HPP

#include "cassette.grpc.pb.h"
#include "beebium/cassette/CassetteDeck.hpp"
#include "beebium/cassette/TapeImage.hpp"
#include "beebium/disc/DiscUrl.hpp"

#include <grpcpp/grpcpp.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace beebium::service {

inline beebium::CassetteErrorKind to_wire_error_kind(beebium::TapeImageError::Kind kind) {
    switch (kind) {
        case beebium::TapeImageError::Kind::CannotOpen:
            return beebium::CASSETTE_ERROR_KIND_CANNOT_OPEN;
        case beebium::TapeImageError::Kind::Unreadable:
            return beebium::CASSETTE_ERROR_KIND_UNREADABLE;
        case beebium::TapeImageError::Kind::UnsupportedFormat:
            return beebium::CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT;
        case beebium::TapeImageError::Kind::InvalidImage:
            return beebium::CASSETTE_ERROR_KIND_INVALID_IMAGE;
    }
    return beebium::CASSETTE_ERROR_KIND_UNSPECIFIED;
}

// gRPC face of the cassette deck.
//
// The transport changes run with the emulation paused, because the deck is
// the emulation thread's. Status is read from the atomics the deck publishes,
// so asking does not stop the machine; the tape's description is read under
// pause only when the deck's tape generation moves.
template <typename MachineType>
class CassetteServiceImpl final : public CassetteService::Service {
    using Memory = typename MachineType::Memory;

public:
    explicit CassetteServiceImpl(MachineType& machine) : machine_(machine) {}

    CassetteServiceImpl(const CassetteServiceImpl&) = delete;
    CassetteServiceImpl& operator=(const CassetteServiceImpl&) = delete;

    grpc::Status InsertTape(grpc::ServerContext*, const InsertTapeRequest* request,
                            InsertTapeResponse* response) override {
        if constexpr (!HasCassette<Memory>) {
            no_interface(response);
            return grpc::Status::OK;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::filesystem::path filepath = filepath_of(request->url());
            const std::string name = filepath.filename().string();
            auto& deck = machine_.state().memory.cassette_deck;

            if (deck.published().has_tape) {
                std::string current;
                machine_.with_emulation_paused([&] { current = deck.name(); });
                fail(response, beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED,
                     "The deck already holds " + current + "; eject it before inserting " + name + ".");
                return grpc::Status::OK;
            }

            // Open and parse off the emulation thread; only the swap pauses it.
            std::unique_ptr<TapeSignal> tape;
            try {
                tape = open_tape_file(filepath);
            } catch (const TapeImageError& e) {
                fail(response, to_wire_error_kind(e.kind()), e.what());
                return grpc::Status::OK;
            } catch (const std::exception& e) {
                fail(response, beebium::CASSETTE_ERROR_KIND_UNREADABLE, e.what());
                return grpc::Status::OK;
            }

            machine_.with_emulation_paused([&] { deck.insert(std::move(tape), name, request->url()); });
            refresh_tape_locked();
            response->set_success(true);
            *response->mutable_tape() = tape_metadata_;
            return grpc::Status::OK;
        }
    }

    grpc::Status EjectTape(grpc::ServerContext*, const EjectTapeRequest*,
                           CassetteResponse* response) override {
        return with_tape(response, [](CassetteDeck& deck) { deck.eject(); });
    }

    grpc::Status Play(grpc::ServerContext*, const PlayRequest*, CassetteResponse* response) override {
        return with_tape(response, [](CassetteDeck& deck) { deck.play(); });
    }

    grpc::Status Stop(grpc::ServerContext*, const StopRequest*, CassetteResponse* response) override {
        return with_tape(response, [](CassetteDeck& deck) { deck.stop(); });
    }

    grpc::Status Rewind(grpc::ServerContext*, const RewindRequest*, CassetteResponse* response) override {
        return with_tape(response, [](CassetteDeck& deck) { deck.rewind(); });
    }

    grpc::Status FastForward(grpc::ServerContext*, const FastForwardRequest*,
                             CassetteResponse* response) override {
        return with_tape(response, [](CassetteDeck& deck) { deck.fast_forward(); });
    }

    grpc::Status Seek(grpc::ServerContext*, const SeekRequest* request, CassetteResponse* response) override {
        const double seconds = request->position_seconds();
        if (!(seconds >= 0.0)) {
            std::ostringstream text;
            text << "A position of " << seconds << " seconds is before the start of the tape.";
            fail(response, beebium::CASSETTE_ERROR_KIND_INVALID_POSITION, text.str());
            return grpc::Status::OK;
        }
        return with_tape(response, [seconds](CassetteDeck& deck) {
            deck.seek(seconds_to_tape_time(seconds));
        });
    }

    grpc::Status GetDeckStatus(grpc::ServerContext*, const GetDeckStatusRequest*,
                               DeckStatus* response) override {
        std::lock_guard<std::mutex> lock(mutex_);
        fill_status_locked(*response, published_now());
        return grpc::Status::OK;
    }

    grpc::Status SubscribeDeckStatus(grpc::ServerContext* context, const SubscribeDeckStatusRequest* request,
                                     grpc::ServerWriter<DeckStatus>* writer) override {
        const uint32_t interval_ms = request->min_interval_ms() > 0 ? request->min_interval_ms() : 100;
        const auto interval_clocks = static_cast<TapeTime>(interval_ms * ULA_CLOCK_HZ / 1000.0);

        CassetteDeck::Published last = published_now();
        {
            DeckStatus status;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                fill_status_locked(status, last);
            }
            if (!writer->Write(status)) return grpc::Status::OK;
        }

        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const CassetteDeck::Published now = published_now();
            const bool changed = now.sequence != last.sequence || now.tape_generation != last.tape_generation;
            // A wind covers WIND_SPEED times the tape a clock that playing does.
            const bool playing = now.transport == CassetteDeck::Transport::Playing && now.relay_closed;
            const bool winding = now.transport == CassetteDeck::Transport::Rewinding ||
                                 now.transport == CassetteDeck::Transport::FastForwarding;
            const TapeTime travelled = now.position > last.position ? now.position - last.position
                                                                    : last.position - now.position;
            const bool moved = (playing && travelled >= interval_clocks) ||
                               (winding && travelled >= interval_clocks * CassetteDeck::WIND_SPEED);
            if (!changed && !moved) continue;

            DeckStatus status;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                fill_status_locked(status, now);
            }
            if (!writer->Write(status)) break;
            last = now;
        }
        return grpc::Status::OK;
    }

private:
    // A file:// URI or a path.
    static std::filesystem::path filepath_of(const std::string& url) {
        if (url.find("://") != std::string::npos) {
            if (const auto parsed = DiscUrl::parse(url); parsed && parsed->is_local()) {
                return parsed->to_filepath();
            }
        }
        return std::filesystem::path(url);
    }

    template <typename Response>
    static void fail(Response* response, beebium::CassetteErrorKind kind, const std::string& sentence) {
        response->set_success(false);
        response->set_error(sentence);
        response->set_kind(kind);
    }

    template <typename Response>
    static void no_interface(Response* response) {
        fail(response, beebium::CASSETTE_ERROR_KIND_NO_CASSETTE_INTERFACE,
             "This machine has no cassette interface.");
    }

    // Run `action` on the deck with the emulation paused, or report why not.
    template <typename Action>
    grpc::Status with_tape(CassetteResponse* response, Action action) {
        if constexpr (!HasCassette<Memory>) {
            no_interface(response);
            return grpc::Status::OK;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& deck = machine_.state().memory.cassette_deck;
            if (!deck.published().has_tape) {
                fail(response, beebium::CASSETTE_ERROR_KIND_DECK_EMPTY, "There is no tape in the deck.");
                return grpc::Status::OK;
            }
            machine_.with_emulation_paused([&] { action(deck); });
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    CassetteDeck::Published published_now() const {
        if constexpr (HasCassette<Memory>) {
            return machine_.state().memory.cassette_deck.published();
        } else {
            return {};
        }
    }

    // Re-read the tape's description if the deck's tape has changed.
    void refresh_tape_locked() {
        if constexpr (HasCassette<Memory>) {
            auto& deck = machine_.state().memory.cassette_deck;
            const uint64_t generation = deck.published().tape_generation;
            if (tape_generation_ && *tape_generation_ == generation) return;
            TapeMetadata metadata;
            machine_.with_emulation_paused([&] {
                if (!deck.has_tape()) return;
                metadata.set_name(deck.name());
                metadata.set_url(deck.url());
                const auto kind = deck.kind();
                metadata.set_format(kind && *kind == TapeKind::Csw ? "CSW" : "UEF");
                metadata.set_title(deck.metadata().title);
                metadata.set_origin(deck.metadata().origin);
                metadata.set_target_machine(deck.metadata().target_machine);
                if (const auto length = deck.duration()) {
                    metadata.set_duration_seconds(static_cast<double>(*length) / ULA_CLOCK_HZ);
                }
            });
            tape_metadata_ = std::move(metadata);
            tape_generation_ = generation;
        }
    }

    static beebium::TapeTransport proto_transport(CassetteDeck::Transport transport) {
        switch (transport) {
            case CassetteDeck::Transport::Playing: return beebium::TAPE_TRANSPORT_PLAYING;
            case CassetteDeck::Transport::Rewinding: return beebium::TAPE_TRANSPORT_REWINDING;
            case CassetteDeck::Transport::FastForwarding: return beebium::TAPE_TRANSPORT_FAST_FORWARDING;
            case CassetteDeck::Transport::Stopped: break;
        }
        return beebium::TAPE_TRANSPORT_STOPPED;
    }

    void fill_status_locked(DeckStatus& status, const CassetteDeck::Published& published) {
        status.set_has_cassette_interface(HasCassette<Memory>);
        if constexpr (HasCassette<Memory>) {
            refresh_tape_locked();
            status.set_tape_inserted(published.has_tape);
            if (published.has_tape) *status.mutable_tape() = tape_metadata_;
            status.set_transport(proto_transport(published.transport));
            status.set_position_seconds(static_cast<double>(published.position) / ULA_CLOCK_HZ);
            status.set_motor_on(published.relay_closed);
            status.set_ula_part(machine_.state().memory.serial_socket.ula().part() == SerialUla::Part::VLSI
                                    ? "vlsi" : "ferranti");
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
    TapeMetadata tape_metadata_;
    std::optional<uint64_t> tape_generation_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_CASSETTE_SERVICE_HPP
