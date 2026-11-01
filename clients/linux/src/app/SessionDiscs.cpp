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

#include "app/SessionDiscs.hpp"

#include "connection/DiscStream.hpp"
#include "connection/RpcWorker.hpp"

#include "disc.grpc.pb.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(10);

DriveInfo::State state_of(beebium::DiscDriveState state) {
    switch (state) {
    case beebium::DISC_DRIVE_STATE_LOADED: return DriveInfo::State::loaded;
    case beebium::DISC_DRIVE_STATE_EJECTING: return DriveInfo::State::ejecting;
    default: return DriveInfo::State::empty;
    }
}

void take_metadata(DriveInfo& drive, const beebium::DiscMetadata& disc) {
    if (!disc.name().empty()) {
        drive.disc_name = QString::fromStdString(disc.name());
    }
    drive.format = QString::fromStdString(disc.format());
    drive.sides = static_cast<int>(disc.sides());
    drive.write_protected = disc.write_protected() || drive.write_protected;
}

DiscOutcome failed_call(const grpc::Status& status) {
    return {false, 0, QString::fromStdString(status.error_message()), {}};
}

}  // namespace

QString disc_url_for_path(const QString& path) {
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

QString path_for_disc_url(const QString& url) {
    const QUrl parsed(url, QUrl::StrictMode);
    if (parsed.isValid() && !parsed.scheme().isEmpty()) {
        return parsed.isLocalFile() ? parsed.toLocalFile() : url;
    }
    return url;  // a bare path, as a preset or the command line mounts one
}

QString BlankDiscTemplate::extension() const {
    const int dot = filename.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : filename.mid(dot).toLower();
}

SessionDiscs::SessionDiscs(QObject* parent) : QObject(parent) {}

SessionDiscs::~SessionDiscs() {
    stop();
}

const DriveInfo* SessionDiscs::drive(int number) const {
    for (const auto& drive : drives_) {
        if (drive.number == number) {
            return &drive;
        }
    }
    return nullptr;
}

void SessionDiscs::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = channel;
    stub_ = std::shared_ptr<beebium::DiscService::Stub>(beebium::DiscService::NewStub(channel));
    rpc_ = std::make_unique<RpcWorker>();
    const unsigned generation = ++generation_;
    stream_ = std::make_unique<DiscStream>(
        channel,
        DiscStream::Callbacks{
            [this, generation](const beebium::GetDriveStatusResponse& status) {
                QMetaObject::invokeMethod(
                    this, [this, generation, status] { on_status(generation, status); },
                    Qt::QueuedConnection);
            },
            [this, generation](const beebium::DiscEvent& event) {
                QMetaObject::invokeMethod(
                    this, [this, generation, event] { on_event(generation, event); },
                    Qt::QueuedConnection);
            },
            {},
        });
    stream_->start();
    // The templates once, on connect; a failure only leaves the menu empty.
    post<std::vector<BlankDiscTemplate>>(
        [stub = stub_](grpc::ClientContext& context) {
            beebium::ListBlankDiscsResponse response;
            std::vector<BlankDiscTemplate> templates;
            if (stub->ListBlankDiscs(&context, {}, &response).ok()) {
                for (const auto& disc : response.discs()) {
                    if (disc.kind().empty() || disc.kind() == "floppy") {
                        templates.push_back({QString::fromStdString(disc.label()),
                                             QString::fromStdString(disc.filename())});
                    }
                }
            }
            return templates;
        },
        [this](const std::vector<BlankDiscTemplate>& templates) {
            blank_discs_ = templates;
            Q_EMIT blank_discs_changed();
        });
}

void SessionDiscs::stop() {
    ++generation_;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    channel_.reset();
    stub_.reset();
    if (loaded_) {
        loaded_ = false;
        Q_EMIT loaded_changed(false);
    }
}

template <typename Result>
void SessionDiscs::post(std::function<Result(grpc::ClientContext&)> call,
                        std::function<void(const Result&)> deliver) {
    if (!rpc_) {
        return;
    }
    const unsigned generation = generation_;
    QPointer<SessionDiscs> guard(this);
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

void SessionDiscs::on_status(unsigned generation, const beebium::GetDriveStatusResponse& status) {
    if (generation != generation_) {
        return;
    }
    has_controller_ = status.has_disc_controller();
    controller_type_ = QString::fromStdString(status.controller_type());
    drives_.clear();
    for (const auto& reported : status.drives()) {
        DriveInfo drive;
        drive.number = static_cast<int>(reported.drive());
        drive.state = state_of(reported.state());
        drive.disc_name = QString::fromStdString(reported.disc_name());
        drive.disc_url = QString::fromStdString(reported.disc_url());
        drive.motor_on = reported.motor_on();
        drive.write_protected = reported.write_protected();
        if (reported.has_disc()) {
            take_metadata(drive, reported.disc());
        }
        drives_.push_back(drive);
    }
    loaded_ = true;
    Q_EMIT loaded_changed(true);
    Q_EMIT drives_changed();
}

void SessionDiscs::on_event(unsigned generation, const beebium::DiscEvent& event) {
    if (generation != generation_) {
        return;
    }
    DriveInfo* drive = nullptr;
    for (auto& candidate : drives_) {
        if (candidate.number == static_cast<int>(event.drive())) {
            drive = &candidate;
        }
    }
    if (!drive) {
        return;  // before the snapshot, or a drive it did not report
    }
    switch (event.type()) {
    case beebium::DISC_EVENT_INSERTED:
        drive->state = DriveInfo::State::loaded;
        drive->disc_url = QString::fromStdString(event.disc_url());
        drive->write_protected = false;
        take_metadata(*drive, event.disc());
        break;
    case beebium::DISC_EVENT_EJECTED:
    case beebium::DISC_EVENT_FORCE_EJECTED: {
        const int number = drive->number;
        *drive = DriveInfo{};
        drive->number = number;
        break;
    }
    case beebium::DISC_EVENT_EJECT_REQUESTED:
        drive->state = DriveInfo::State::ejecting;
        break;
    case beebium::DISC_EVENT_EJECT_CANCELLED:
        drive->state = DriveInfo::State::loaded;
        break;
    case beebium::DISC_EVENT_MOTOR_ON:
        drive->motor_on = true;
        break;
    case beebium::DISC_EVENT_MOTOR_OFF:
        drive->motor_on = false;
        break;
    default:
        return;
    }
    Q_EMIT drives_changed();
}

void SessionDiscs::insert(int drive, const QString& path) {
    const std::string url = disc_url_for_path(path).toStdString();
    post<DiscOutcome>(
        [stub = stub_, drive, url](grpc::ClientContext& context) {
            beebium::InsertDiscRequest request;
            request.set_drive(static_cast<std::uint32_t>(drive));
            request.set_url(url);
            beebium::InsertDiscResponse response;
            const grpc::Status status = stub->InsertDisc(&context, request, &response);
            if (!status.ok()) {
                return failed_call(status);
            }
            return DiscOutcome{response.success(), static_cast<int>(response.kind()),
                               QString::fromStdString(response.error()), {}};
        },
        [this, drive, path](const DiscOutcome& outcome) { Q_EMIT insert_finished(drive, path, outcome); });
}

void SessionDiscs::eject(int drive, bool immediate) {
    post<DiscOutcome>(
        [stub = stub_, drive, immediate](grpc::ClientContext& context) {
            beebium::EjectDiscRequest request;
            request.set_drive(static_cast<std::uint32_t>(drive));
            request.set_immediate(immediate);
            beebium::EjectDiscResponse response;
            const grpc::Status status = stub->EjectDisc(&context, request, &response);
            if (!status.ok()) {
                return failed_call(status);
            }
            return DiscOutcome{response.accepted(), static_cast<int>(response.kind()),
                               QString::fromStdString(response.error()), {}};
        },
        [this, drive](const DiscOutcome& outcome) { Q_EMIT eject_finished(drive, outcome); });
}

void SessionDiscs::cancel_eject(int drive) {
    post<DiscOutcome>(
        [stub = stub_, drive](grpc::ClientContext& context) {
            beebium::CancelEjectRequest request;
            request.set_drive(static_cast<std::uint32_t>(drive));
            beebium::CancelEjectResponse response;
            const grpc::Status status = stub->CancelEject(&context, request, &response);
            if (!status.ok()) {
                return failed_call(status);
            }
            return DiscOutcome{response.cancelled(), static_cast<int>(response.kind()),
                               QString::fromStdString(response.error()), {}};
        },
        [this, drive](const DiscOutcome& outcome) { Q_EMIT cancel_finished(drive, outcome); });
}

void SessionDiscs::create_disc(const QString& label, const QString& path) {
    const std::string label_text = label.toStdString();
    const std::string destination = path.toStdString();
    post<DiscOutcome>(
        [stub = stub_, label_text,
         destination](grpc::ClientContext& context) {
            beebium::CreateDiscRequest request;
            request.set_label(label_text);
            request.set_destination_path(destination);
            beebium::CreateDiscResponse response;
            const grpc::Status status = stub->CreateDisc(&context, request, &response);
            if (!status.ok()) {
                return failed_call(status);
            }
            return DiscOutcome{response.success(), static_cast<int>(response.kind()),
                               QString::fromStdString(response.error()),
                               QString::fromStdString(response.created_path())};
        },
        [this, label](const DiscOutcome& outcome) { Q_EMIT create_finished(label, outcome); });
}

}  // namespace beebium::linux_client
