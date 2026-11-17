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

#include "app/SessionCassette.hpp"

#include "app/SessionDiscs.hpp"
#include "connection/CassetteStatusStream.hpp"
#include "connection/RpcWorker.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(10);
// A second at least half signal lights its column.
constexpr unsigned SIGNAL_LEVEL = 128;

DeckState::Transport transport_of(beebium::TapeTransport transport) {
    switch (transport) {
    case beebium::TAPE_TRANSPORT_PLAYING: return DeckState::Transport::playing;
    case beebium::TAPE_TRANSPORT_RECORDING: return DeckState::Transport::recording;
    case beebium::TAPE_TRANSPORT_REWINDING: return DeckState::Transport::rewinding;
    case beebium::TAPE_TRANSPORT_FAST_FORWARDING: return DeckState::Transport::fast_forwarding;
    default: return DeckState::Transport::stopped;
    }
}

CassetteOutcome failed_call(const grpc::Status& status) {
    CassetteOutcome outcome;
    outcome.reached = false;
    outcome.message = QString::fromStdString(status.error_message());
    return outcome;
}

CassetteOutcome outcome_of(const beebium::CassetteResponse& response) {
    CassetteOutcome outcome;
    outcome.ok = response.success();
    outcome.kind = static_cast<int>(response.kind());
    outcome.message = QString::fromStdString(response.error());
    return outcome;
}

}  // namespace

DeckState deck_state_from(const beebium::DeckStatus& status) {
    DeckState deck;
    deck.known = true;
    deck.has_interface = status.has_cassette_interface();
    deck.tape_inserted = status.tape_inserted();
    if (deck.tape_inserted) {
        const beebium::TapeMetadata& tape = status.tape();
        deck.tape.name = QString::fromStdString(tape.name());
        deck.tape.url = QString::fromStdString(tape.url());
        deck.tape.format = QString::fromStdString(tape.format());
        deck.tape.title = QString::fromStdString(tape.title());
        deck.tape.origin = QString::fromStdString(tape.origin());
        deck.tape.target_machine = QString::fromStdString(tape.target_machine());
        deck.tape.duration_seconds = std::max(0.0, tape.duration_seconds());
        deck.tape.recordable = tape.recordable();
        deck.tape.sample_rate = tape.sample_rate();
        deck.tape.channel = QString::fromStdString(tape.channel());
        deck.tape.record_refusal = QString::fromStdString(tape.record_refusal());
    }
    deck.transport = transport_of(status.transport());
    deck.position_seconds = std::max(0.0, status.position_seconds());
    deck.motor_on = status.motor_on();
    deck.live_ring_fill = std::clamp(status.live_ring_fill(), 0.0, 1.0);
    deck.live_overruns = status.live_overruns();
    deck.live_underruns = status.live_underruns();
    deck.signal_map_ready = status.signal_map_ready();
    deck.signal_map_generation = status.signal_map_generation();
    deck.append_refusal = QString::fromStdString(status.append_refusal());
    return deck;
}

std::vector<bool> signal_columns(unsigned seconds, const QByteArray& levels, int width) {
    std::vector<bool> columns(static_cast<std::size_t>(std::max(width, 0)), false);
    const qsizetype known = std::min<qsizetype>(seconds, levels.size());
    if (known <= 0 || width <= 0) {
        return columns;
    }
    for (int column = 0; column < width; ++column) {
        const double from = static_cast<double>(column) * seconds / width;
        const double to = static_cast<double>(column + 1) * seconds / width;
        const auto first = static_cast<qsizetype>(std::floor(from));
        const qsizetype last =
            std::min(known, std::max(first + 1, static_cast<qsizetype>(std::ceil(to))));
        for (qsizetype second = first; second < last; ++second) {
            if (static_cast<unsigned char>(levels[second]) >= SIGNAL_LEVEL) {
                columns[static_cast<std::size_t>(column)] = true;
                break;
            }
        }
    }
    return columns;
}

QStringList tape_description(const TapeInfo& tape) {
    QStringList parts;
    if (!tape.format.isEmpty()) {
        parts << tape.format;
    }
    if (tape.sample_rate > 0) {
        parts << QCoreApplication::translate("Cassette", "%1 kHz")
                     .arg(QString::number(tape.sample_rate / 1000.0, 'g', 4));
    }
    if (!tape.channel.isEmpty()) {
        parts << QCoreApplication::translate("Cassette", "%1 channel").arg(tape.channel);
    }
    for (const QString& part : {tape.title, tape.origin, tape.target_machine}) {
        if (!part.isEmpty()) {
            parts << part;
        }
    }
    return parts;
}

QString tape_counter_text(double seconds) {
    const long long whole = seconds > 0.0 ? static_cast<long long>(std::floor(seconds)) : 0;
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

QString deck_state_text(const DeckState& deck) {
    switch (deck.transport) {
    case DeckState::Transport::playing:
        return deck.motor_on ? QCoreApplication::translate("Cassette", "Playing")
                             : QCoreApplication::translate("Cassette", "Playing, motor off");
    case DeckState::Transport::recording:
        return deck.motor_on ? QCoreApplication::translate("Cassette", "Recording")
                             : QCoreApplication::translate("Cassette", "Recording, motor off");
    case DeckState::Transport::rewinding:
        return QCoreApplication::translate("Cassette", "Rewinding");
    case DeckState::Transport::fast_forwarding:
        return QCoreApplication::translate("Cassette", "Fast forwarding");
    case DeckState::Transport::stopped:
        break;
    }
    return QCoreApplication::translate("Cassette", "Stopped");
}

QString BlankTapeTemplate::extension() const {
    const int dot = filename.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QStringLiteral(".uef") : filename.mid(dot).toLower();
}

// One StreamAudio call on a thread of its own: the format first, then each
// chunk as the capture gives it, then, once finished, the end of the sending
// side and the server's summary. Cancelled with the connection.
class LiveStream {
public:
    // About two seconds of 10 ms chunks: more waiting than that is a server
    // not taking them, and the oldest go first, as the server's own ring
    // drops its oldest.
    static constexpr std::size_t QUEUE_CAP = 200;

    using Done = std::function<void(const LiveSummary&)>;

    LiveStream(std::shared_ptr<beebium::CassetteService::Stub> stub, const LiveInputFormat& format,
               Done done)
        : stub_(std::move(stub)), done_(std::move(done)) {
        beebium::AudioInputFormat* proto = first_.mutable_format();
        proto->set_sample_rate(format.sample_rate);
        proto->set_sample_format(format.sample == LiveInputFormat::Sample::s16
                                     ? beebium::AUDIO_SAMPLE_FORMAT_S16
                                     : beebium::AUDIO_SAMPLE_FORMAT_F32);
        proto->set_channels(format.channels);
        proto->set_channel(format.channel.toStdString());
        proto->set_device_name(format.device_name.toStdString());
        thread_ = std::thread([this] { run(); });
    }

    ~LiveStream() {
        {
            std::lock_guard lock(mutex_);
            cancelled_ = true;
        }
        context_.TryCancel();
        changed_.notify_all();
        thread_.join();
    }

    void push(const QByteArray& samples) {
        {
            std::lock_guard lock(mutex_);
            if (finishing_) {
                return;
            }
            queue_.emplace_back(samples.constData(), static_cast<std::size_t>(samples.size()));
            while (queue_.size() > QUEUE_CAP) {
                queue_.pop_front();
            }
        }
        changed_.notify_all();
    }

    void finish() {
        {
            std::lock_guard lock(mutex_);
            finishing_ = true;
        }
        changed_.notify_all();
    }

private:
    void run() {
        beebium::StreamAudioSummary summary;
        auto writer = stub_->StreamAudio(&context_, &summary);
        bool open = writer->Write(first_);
        while (open) {
            std::string samples;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [this] { return cancelled_ || finishing_ || !queue_.empty(); });
                if (cancelled_) {
                    break;
                }
                if (queue_.empty()) {
                    break;  // finishing, and all of it sent
                }
                samples = std::move(queue_.front());
                queue_.pop_front();
            }
            beebium::AudioInputChunk chunk;
            chunk.set_samples(std::move(samples));
            open = writer->Write(chunk);
        }
        if (open) {
            writer->WritesDone();
        }
        const grpc::Status status = writer->Finish();
        LiveSummary result;
        if (!status.ok()) {
            result.outcome = failed_call(status);
        } else {
            result.outcome.ok = summary.error().empty();
            result.outcome.kind = static_cast<int>(summary.kind());
            result.outcome.message = QString::fromStdString(summary.error());
        }
        result.samples_received = summary.samples_received();
        result.samples_dropped = summary.samples_dropped();
        result.silence_inserted = summary.silence_samples_inserted();
        bool cancelled = false;
        {
            std::lock_guard lock(mutex_);
            cancelled = cancelled_;
        }
        if (!cancelled) {
            done_(result);
        }
    }

    std::shared_ptr<beebium::CassetteService::Stub> stub_;
    Done done_;
    beebium::AudioInputChunk first_;
    grpc::ClientContext context_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::string> queue_;
    bool finishing_ = false;
    bool cancelled_ = false;
    std::thread thread_;
};

SessionCassette::SessionCassette(QObject* parent) : QObject(parent) {}

SessionCassette::~SessionCassette() {
    stop();
}

void SessionCassette::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    stub_ = std::shared_ptr<beebium::CassetteService::Stub>(beebium::CassetteService::NewStub(channel));
    rpc_ = std::make_unique<RpcWorker>();
    const unsigned generation = ++generation_;
    stream_ = std::make_unique<CassetteStatusStream>(
        channel,
        CassetteStatusStream::Callbacks{
            [this, generation](const beebium::DeckStatus& status) {
                QMetaObject::invokeMethod(
                    this, [this, generation, status] { on_status(generation, status); },
                    Qt::QueuedConnection);
            },
            [this, generation](const StreamEnd& end) {
                // A server without the service has no deck to show; any
                // other end is the connection's, which recovery handles.
                if (end.code == grpc::StatusCode::UNIMPLEMENTED) {
                    QMetaObject::invokeMethod(
                        this, [this, generation] { on_unavailable(generation); },
                        Qt::QueuedConnection);
                }
            },
        });
    stream_->start();
    // The templates once, on connect; a failure only leaves the menu empty.
    post<std::vector<BlankTapeTemplate>>(
        [stub = stub_](grpc::ClientContext& context) {
            beebium::ListBlankTapesResponse response;
            std::vector<BlankTapeTemplate> templates;
            if (stub->ListBlankTapes(&context, {}, &response).ok()) {
                for (const auto& tape : response.tapes()) {
                    templates.push_back({QString::fromStdString(tape.label()),
                                         QString::fromStdString(tape.filename()),
                                         tape.duration_seconds()});
                }
            }
            return templates;
        },
        [this](const std::vector<BlankTapeTemplate>& templates) {
            blank_tapes_ = templates;
            Q_EMIT blank_tapes_changed();
        });
}

void SessionCassette::stop() {
    ++generation_;
    // The connection's end is the stream's: cancelled, with no summary.
    live_.reset();
    live_finishing_ = false;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    stub_.reset();
    drop_signal_map();
    if (deck_.known) {
        deck_ = {};
        Q_EMIT deck_changed();
    }
}

template <typename Result>
void SessionCassette::post(std::function<Result(grpc::ClientContext&)> call,
                           std::function<void(const Result&)> deliver) {
    if (!rpc_) {
        return;
    }
    const unsigned generation = generation_;
    QPointer<SessionCassette> guard(this);
    rpc_->post([call = std::move(call), deliver = std::move(deliver), guard,
                generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        Result result = call(context);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, deliver, result] {
                if (guard && generation == guard->generation_) {
                    deliver(result);
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionCassette::on_status(unsigned generation, const beebium::DeckStatus& status) {
    if (generation != generation_) {
        return;
    }
    const DeckState deck = deck_state_from(status);
    if (deck == deck_) {
        return;
    }
    deck_ = deck;
    Q_EMIT deck_changed();
    follow_signal_map();
}

void SessionCassette::follow_signal_map() {
    // Fetched once the server has it ready, again for each generation it
    // publishes, never polled; gone with the tape, and none for a live one.
    if (!deck_.tape_inserted || deck_.live() || !deck_.signal_map_ready) {
        drop_signal_map();
        return;
    }
    const std::pair<QString, std::uint32_t> asked{deck_.tape.url, deck_.signal_map_generation};
    if (signal_map_asked_ == asked) {
        return;
    }
    signal_map_asked_ = asked;
    post<std::optional<SignalMap>>(
        [stub = stub_](grpc::ClientContext& context) -> std::optional<SignalMap> {
            beebium::TapeSignalMap response;
            if (!stub->GetTapeSignalMap(&context, {}, &response).ok() || !response.success() ||
                !response.ready()) {
                return std::nullopt;
            }
            return SignalMap{response.seconds(), QByteArray::fromStdString(response.levels())};
        },
        [this, asked](const std::optional<SignalMap>& map) {
            if (!map || signal_map_asked_ != asked || *map == signal_map_) {
                return;
            }
            signal_map_ = *map;
            Q_EMIT signal_map_changed();
        });
}

void SessionCassette::drop_signal_map() {
    signal_map_asked_.reset();
    if (!signal_map_.levels.isEmpty() || signal_map_.seconds != 0) {
        signal_map_ = {};
        Q_EMIT signal_map_changed();
    }
}

void SessionCassette::on_unavailable(unsigned generation) {
    if (generation != generation_) {
        return;
    }
    deck_ = {};
    deck_.known = true;
    Q_EMIT deck_changed();
    drop_signal_map();
}

void SessionCassette::insert(const QString& path) {
    const std::string url = disc_url_for_path(path).toStdString();
    post<CassetteOutcome>(
        [stub = stub_, url](grpc::ClientContext& context) {
            beebium::InsertTapeRequest request;
            request.set_url(url);
            beebium::InsertTapeResponse response;
            const grpc::Status status = stub->InsertTape(&context, request, &response);
            if (!status.ok()) {
                return failed_call(status);
            }
            CassetteOutcome outcome;
            outcome.ok = response.success();
            outcome.kind = static_cast<int>(response.kind());
            outcome.message = QString::fromStdString(response.error());
            return outcome;
        },
        [this, path](const CassetteOutcome& outcome) { Q_EMIT insert_finished(path, outcome); });
}

void SessionCassette::press(CassetteKey key) {
    call_key(key, 0.0);
}

void SessionCassette::seek(double position_seconds) {
    call_key(CassetteKey::seek, position_seconds);
}

void SessionCassette::call_key(CassetteKey key, double position_seconds) {
    post<CassetteOutcome>(
        [stub = stub_, key, position_seconds](grpc::ClientContext& context) {
            beebium::CassetteResponse response;
            grpc::Status status;
            switch (key) {
            case CassetteKey::play: status = stub->Play(&context, {}, &response); break;
            case CassetteKey::stop: status = stub->Stop(&context, {}, &response); break;
            case CassetteKey::rewind: status = stub->Rewind(&context, {}, &response); break;
            case CassetteKey::fast_forward: status = stub->FastForward(&context, {}, &response); break;
            case CassetteKey::record: status = stub->Record(&context, {}, &response); break;
            case CassetteKey::eject: status = stub->EjectTape(&context, {}, &response); break;
            case CassetteKey::seek: {
                beebium::SeekRequest request;
                request.set_position_seconds(position_seconds);
                status = stub->Seek(&context, request, &response);
                break;
            }
            case CassetteKey::append: {
                // The server finds where the recordings end and seeks
                // there; the counter and the marker follow the status.
                // At the end, with no room, the deck stops: its state
                // says so, and nothing more is shown.
                beebium::SeekToAppendPointResponse append;
                status = stub->SeekToAppendPoint(&context, {}, &append);
                response.set_success(append.success());
                response.set_kind(append.kind());
                response.set_error(append.error());
                break;
            }
            }
            return status.ok() ? outcome_of(response) : failed_call(status);
        },
        [this, key](const CassetteOutcome& outcome) { Q_EMIT key_finished(key, outcome); });
}

void SessionCassette::start_live(const LiveInputFormat& format) {
    if (!stub_ || live_) {
        return;
    }
    live_finishing_ = false;
    const unsigned generation = generation_;
    QPointer<SessionCassette> guard(this);
    live_ = std::make_unique<LiveStream>(stub_, format, [guard, generation](const LiveSummary& summary) {
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, summary] {
                if (!guard || generation != guard->generation_) {
                    return;
                }
                guard->live_.reset();
                guard->live_finishing_ = false;
                Q_EMIT guard->live_ended(summary);
            },
            Qt::QueuedConnection);
    });
}

void SessionCassette::push_live(const QByteArray& samples) {
    if (live_ && !live_finishing_) {
        live_->push(samples);
    }
}

void SessionCassette::finish_live() {
    if (live_ && !live_finishing_) {
        live_finishing_ = true;
        live_->finish();
        Q_EMIT deck_changed();
    }
}

void SessionCassette::create_tape(const QString& label, const QString& path) {
    const std::string label_text = label.toStdString();
    const std::string destination = path.toStdString();
    post<CassetteOutcome>(
        [stub = stub_, label_text, destination](grpc::ClientContext& context) {
            beebium::CreateTapeRequest request;
            request.set_label(label_text);
            request.set_destination_path(destination);
            beebium::CreateTapeResponse response;
            const grpc::Status status = stub->CreateTape(&context, request, &response);
            if (!status.ok()) {
                return failed_call(status);
            }
            CassetteOutcome outcome;
            outcome.ok = response.success();
            outcome.kind = static_cast<int>(response.kind());
            outcome.message = QString::fromStdString(response.error());
            outcome.created_path = QString::fromStdString(response.path());
            return outcome;
        },
        [this, label](const CassetteOutcome& outcome) { Q_EMIT create_finished(label, outcome); });
}

}  // namespace beebium::linux_client
