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

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(10);

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
    }
    deck.transport = transport_of(status.transport());
    deck.position_seconds = std::max(0.0, status.position_seconds());
    deck.motor_on = status.motor_on();
    return deck;
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
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    stub_.reset();
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
}

void SessionCassette::on_unavailable(unsigned generation) {
    if (generation != generation_) {
        return;
    }
    deck_ = {};
    deck_.known = true;
    Q_EMIT deck_changed();
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
            }
            return status.ok() ? outcome_of(response) : failed_call(status);
        },
        [this, key](const CassetteOutcome& outcome) { Q_EMIT key_finished(key, outcome); });
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
