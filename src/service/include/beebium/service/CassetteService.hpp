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
#include "beebium/cassette/BlankTapes.hpp"
#include "beebium/cassette/CassetteDeck.hpp"
#include "beebium/cassette/LiveSource.hpp"
#include "beebium/cassette/TapeImage.hpp"
#include "beebium/disc/DiscUrl.hpp"

#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

namespace beebium::service {

// A tape's length as the service reports it: seconds to the millisecond. The
// residue below that is the ULA clock's quantisation, not the tape's.
inline double reported_seconds(TapeTime length) {
    return std::round(static_cast<double>(length) / ULA_CLOCK_HZ * 1000.0) / 1000.0;
}

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
// Map a blank tape copy failure to its wire enum value.
inline beebium::CreateTapeErrorKind to_wire_create_tape_error_kind(beebium::CreateBlankDiscErrorKind kind) {
    switch (kind) {
        case beebium::CreateBlankDiscErrorKind::None:
            return beebium::CREATE_TAPE_ERROR_KIND_UNSPECIFIED;
        case beebium::CreateBlankDiscErrorKind::UnknownLabel:
            return beebium::CREATE_TAPE_ERROR_KIND_UNKNOWN_LABEL;
        case beebium::CreateBlankDiscErrorKind::DestinationExists:
            return beebium::CREATE_TAPE_ERROR_KIND_DESTINATION_EXISTS;
        case beebium::CreateBlankDiscErrorKind::WriteFailed:
            return beebium::CREATE_TAPE_ERROR_KIND_WRITE_FAILED;
        case beebium::CreateBlankDiscErrorKind::WrongExtension:
            return beebium::CREATE_TAPE_ERROR_KIND_WRONG_EXTENSION;
    }
    return beebium::CREATE_TAPE_ERROR_KIND_UNSPECIFIED;
}

template <typename MachineType>
class CassetteServiceImpl final : public CassetteService::Service {
    using Memory = typename MachineType::Memory;

public:
    explicit CassetteServiceImpl(MachineType& machine) : machine_(machine) {}

    // Where the blank tape templates are; none means no templates.
    void set_blank_tapes_dirpath(std::optional<std::filesystem::path> dirpath) {
        blank_tapes_dirpath_ = std::move(dirpath);
    }

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

            std::optional<WavChannel> channel;
            if (request->has_channel()) {
                if (request->channel() == "left") {
                    channel = WavChannel::Left;
                } else if (request->channel() == "right") {
                    channel = WavChannel::Right;
                } else {
                    fail(response, beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT,
                         "A WAV's channel is left or right, not " + request->channel() + ".");
                    return grpc::Status::OK;
                }
            }

            // Open and parse off the emulation thread; only the swap pauses it.
            std::unique_ptr<TapeSignal> tape;
            try {
                tape = open_tape_file(filepath, channel);
            } catch (const TapeImageError& e) {
                fail(response, to_wire_error_kind(e.kind()), e.what());
                return grpc::Status::OK;
            } catch (const std::exception& e) {
                fail(response, beebium::CASSETTE_ERROR_KIND_UNREADABLE, e.what());
                return grpc::Status::OK;
            }

            // A recording is written back to the file the tape came from.
            machine_.with_emulation_paused([&] { deck.insert(std::move(tape), name, request->url(), filepath); });
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
        return with_tape_not_live(response, "rewound", [](CassetteDeck& deck) { deck.rewind(); });
    }

    grpc::Status FastForward(grpc::ServerContext*, const FastForwardRequest*,
                             CassetteResponse* response) override {
        return with_tape_not_live(response, "fast-forwarded", [](CassetteDeck& deck) { deck.fast_forward(); });
    }

    grpc::Status Record(grpc::ServerContext*, const RecordRequest*, CassetteResponse* response) override {
        std::optional<std::string> refused;
        const grpc::Status status = with_tape_not_live(response, "recorded on", [&](CassetteDeck& deck) {
            refused = deck.why_not_recordable();
            if (refused) return;
            deck.record();
        });
        if (refused) fail(response, beebium::CASSETTE_ERROR_KIND_READ_ONLY, *refused);
        return status;
    }

    grpc::Status StreamAudio(grpc::ServerContext* context, grpc::ServerReader<AudioInputChunk>* reader,
                             StreamAudioSummary* summary) override {
        auto refuse = [&](beebium::CassetteErrorKind kind, const std::string& sentence) {
            summary->set_error(sentence);
            summary->set_kind(kind);
            return grpc::Status::OK;
        };
        if constexpr (!HasCassette<Memory>) {
            return refuse(beebium::CASSETTE_ERROR_KIND_NO_CASSETTE_INTERFACE, "This machine has no cassette interface.");
        } else {
            AudioInputChunk chunk;
            if (!reader->Read(&chunk) || !chunk.has_format()) {
                return refuse(beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT, "A live stream begins with its format.");
            }
            const AudioInputFormat& format = chunk.format();
            if (format.sample_rate() < 8000 || format.sample_rate() > 192000) {
                return refuse(beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT,
                              "A live stream's rate is 8000 to 192000 Hz, not " +
                                  std::to_string(format.sample_rate()) + ".");
            }
            if (format.sample_format() != beebium::AUDIO_SAMPLE_FORMAT_S16 &&
                format.sample_format() != beebium::AUDIO_SAMPLE_FORMAT_F32) {
                return refuse(beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT,
                              "A live stream's samples are signed 16-bit or 32-bit float.");
            }
            if (format.channels() != 1 && format.channels() != 2) {
                return refuse(beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT,
                              "A live stream has one or two channels, not " + std::to_string(format.channels()) + ".");
            }
            if (!format.channel().empty() && format.channel() != "left" && format.channel() != "right") {
                return refuse(beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT,
                              "A live stream's channel is left or right, not " + format.channel() + ".");
            }
            const bool right = format.channels() == 2 && format.channel() == "right";
            const std::string name = format.device_name().empty() ? "Live input" : format.device_name();

            auto& deck = machine_.state().memory.cassette_deck;
            auto source = std::make_shared<LiveSource>(format.sample_rate());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (deck.published().has_tape) {
                    std::string current;
                    machine_.with_emulation_paused([&] { current = deck.name(); });
                    return refuse(beebium::CASSETTE_ERROR_KIND_DECK_OCCUPIED,
                                  "The deck already holds " + current + "; eject it before streaming " + name + ".");
                }
                machine_.with_emulation_paused([&] {
                    // A channel only for a stereo stream; a mono one has none to report.
                    const std::string channel = format.channels() == 2 ? (right ? "right" : "left") : "";
                    deck.insert(std::make_unique<LiveTape>(source, channel), name);
                });
                live_source_ = source;
                live_active_ = true;
                refresh_tape_locked();
            }

            // The samples: decoded here, on this thread, and pushed to the
            // ring, the only seam between the stream and the emulation.
            AudioDecoder decoder(format.sample_format() == beebium::AUDIO_SAMPLE_FORMAT_S16 ? AudioDecoder::Format::S16
                                                                                             : AudioDecoder::Format::F32,
                                 format.channels(), right ? 1 : 0);
            std::vector<float> samples;
            while (!source->closed() && reader->Read(&chunk)) {
                if (chunk.has_format()) {
                    summary->set_error("A live stream's format comes once, at its start.");
                    summary->set_kind(beebium::CASSETTE_ERROR_KIND_INVALID_FORMAT);
                    break;
                }
                samples.clear();
                decoder.decode(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(chunk.samples().data()),
                                                        chunk.samples().size()),
                               samples);
                source->push(samples);
            }

            // The stream is over. A tape that runs out is not one pulled from
            // the deck: a playing deck plays out what the ring holds, and the
            // tape goes when it is empty. A stopped deck plays nothing, so the
            // ring is let go with the tape at once.
            source->end_stream();
            while (!source->closed() && !source->empty() &&
                   deck.published().transport == CassetteDeck::Transport::Playing && !context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!source->closed()) {
                    machine_.with_emulation_paused([&] { deck.eject(); });
                }
                if (live_source_ == source) {
                    live_source_.reset();
                    live_active_ = false;
                }
            }
            summary->set_samples_received(source->received());
            summary->set_samples_dropped(source->overruns());
            summary->set_silence_samples_inserted(source->underruns());
            return grpc::Status::OK;
        }
    }

    grpc::Status ListBlankTapes(grpc::ServerContext*, const ListBlankTapesRequest*,
                                ListBlankTapesResponse* response) override {
        for (const auto& tape : beebium::list_blank_tapes(blank_tapes_dirpath_)) {
            auto* item = response->add_tapes();
            item->set_label(tape.label);
            item->set_filename(tape.filename);
            item->set_kind(tape.kind);
            item->set_size_bytes(tape.size_bytes);
            item->set_duration_seconds(tape.duration_seconds);
        }
        return grpc::Status::OK;
    }

    grpc::Status CreateTape(grpc::ServerContext*, const CreateTapeRequest* request,
                            CreateTapeResponse* response) override {
        const auto result = beebium::create_blank_tape(blank_tapes_dirpath_, request->label(),
                                                       std::filesystem::path(request->destination_path()),
                                                       request->overwrite());
        if (!result) {
            response->set_success(false);
            response->set_error(result.error);
            response->set_kind(to_wire_create_tape_error_kind(result.kind));
            return grpc::Status::OK;
        }
        response->set_success(true);
        response->set_path(result.created_filepath.string());
        return grpc::Status::OK;
    }

    grpc::Status Seek(grpc::ServerContext*, const SeekRequest* request, CassetteResponse* response) override {
        const double seconds = request->position_seconds();
        if (!(seconds >= 0.0)) {
            std::ostringstream text;
            text << "A position of " << seconds << " seconds is before the start of the tape.";
            fail(response, beebium::CASSETTE_ERROR_KIND_INVALID_POSITION, text.str());
            return grpc::Status::OK;
        }
        return with_tape_not_live(response, "seeked", [seconds](CassetteDeck& deck) {
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
            // A live tape moves whether or not the deck plays or the relay is closed.
            const bool live = live_active_.load(std::memory_order_relaxed);
            const bool playing = live || ((now.transport == CassetteDeck::Transport::Playing ||
                                           now.transport == CassetteDeck::Transport::Recording) &&
                                          now.relay_closed);
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

    // As with_tape, refused with LIVE_SOURCE for a live tape, which cannot be
    // `done` (seeked, rewound, ...).
    template <typename Action>
    grpc::Status with_tape_not_live(CassetteResponse* response, const char* done, Action action) {
        std::optional<std::string> live;
        const grpc::Status status = with_tape(response, [&](CassetteDeck& deck) {
            if (deck.live()) {
                live = "The live input " + deck.name() + " cannot be " + done + ".";
                return;
            }
            action(deck);
        });
        if (live) fail(response, beebium::CASSETTE_ERROR_KIND_LIVE_SOURCE, *live);
        return status;
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
                metadata.set_format(kind ? tape_kind_text(*kind) : "UEF");
                metadata.set_title(deck.metadata().title);
                metadata.set_origin(deck.metadata().origin);
                metadata.set_target_machine(deck.metadata().target_machine);
                if (const auto length = deck.duration()) {
                    metadata.set_duration_seconds(reported_seconds(*length));
                }
                metadata.set_recordable(deck.recordable());
                metadata.set_sample_rate(deck.metadata().sample_rate);
                metadata.set_channel(deck.metadata().channel);
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
            case CassetteDeck::Transport::Recording: return beebium::TAPE_TRANSPORT_RECORDING;
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
            if (live_source_) {
                status.set_live_ring_fill(live_source_->fill());
                status.set_live_overruns(live_source_->overruns());
                status.set_live_underruns(live_source_->underruns());
            }
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
    TapeMetadata tape_metadata_;
    std::optional<uint64_t> tape_generation_;
    std::optional<std::filesystem::path> blank_tapes_dirpath_;
    // The live tape's source while a stream plays one (under mutex_), and
    // whether one does (for the status stream's pacing, without the lock).
    std::shared_ptr<LiveSource> live_source_;
    std::atomic<bool> live_active_{false};
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_CASSETTE_SERVICE_HPP
