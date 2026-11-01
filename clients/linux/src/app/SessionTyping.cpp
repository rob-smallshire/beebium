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

#include "app/SessionTyping.hpp"

#include "connection/RpcWorker.hpp"

#include <QMetaObject>
#include <QPointer>

#include <chrono>
#include <cstdio>

namespace beebium::linux_client {

SessionTyping::SessionTyping(QObject* parent) : QObject(parent) {}

SessionTyping::~SessionTyping() {
    stop();
}

void SessionTyping::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    const unsigned generation = ++generation_;
    stub_ = std::shared_ptr<beebium::KeyboardService::Stub>(beebium::KeyboardService::NewStub(channel));
    rpc_ = std::make_unique<RpcWorker>();
    stream_ = std::make_unique<StreamReader<beebium::TypingStatus>>(
        "bbm-typing",
        [stub = stub_](grpc::ClientContext& context) {
            beebium::WatchTypingStatusRequest request;
            request.set_min_interval_ms(MIN_INTERVAL_MS);
            return stub->WatchTypingStatus(&context, request);
        },
        StreamReader<beebium::TypingStatus>::Callbacks{
            [this, generation](beebium::TypingStatus& status) {
                QMetaObject::invokeMethod(
                    this, [this, generation, status] { on_status(generation, status); },
                    Qt::QueuedConnection);
            },
            {},
        });
    stream_->start();
}

void SessionTyping::stop() {
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
    guest_ready_.reset();
    if (pasting_) {
        // The connection went with the paste: say so rather than leave it
        // waiting for a drain that will never be reported.
        finish({PasteOutcome::Result::failed, tr("the connection to the machine was lost"), 0});
    }
}

void SessionTyping::on_status(unsigned generation, const beebium::TypingStatus& status) {
    if (generation != generation_) {
        return;
    }
    pending_ = status.pending_characters();
    // The rising edge, or a guest already ready when the stream begins.
    if (status.guest_ready() && guest_ready_ != true) {
        std::fprintf(stderr, "beebium: the guest is ready for input\n");
        Q_EMIT guest_ready_rose();
    }
    guest_ready_ = status.guest_ready();
    if (!pasting_) {
        return;
    }
    if (!status.idle()) {
        paste_seen_pending_ = true;
        return;
    }
    if (!paste_seen_pending_ && status.discarded_by_reset() == 0) {
        return;  // an idle status from before the text was queued
    }
    if (status.discarded_by_reset() > 0) {
        finish({PasteOutcome::Result::cut_short_by_reset, {}, status.discarded_by_reset()});
        return;
    }
    std::fprintf(stderr, "beebium: the paste has been typed\n");
    finish({PasteOutcome::Result::typed, {}, 0});
}

void SessionTyping::paste(const QString& text) {
    if (!rpc_ || pasting_ || text.isEmpty()) {
        return;
    }
    pasting_ = true;
    paste_seen_pending_ = false;
    cancelling_ = false;
    Q_EMIT pasting_changed(true);
    const unsigned generation = generation_;
    const std::string utf8 = text.toStdString();
    QPointer<SessionTyping> guard(this);
    rpc_->post([stub = stub_, utf8, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        beebium::TypeQuicklyRequest request;
        request.set_text(utf8);
        request.set_translate(true);
        beebium::TypeQuicklyResponse response;
        const grpc::Status status = stub->TypeQuickly(&context, request, &response);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, status, response] {
                if (!guard || generation != guard->generation_ || !guard->pasting_) {
                    return;
                }
                if (!status.ok()) {
                    guard->finish({PasteOutcome::Result::failed,
                                   QString::fromStdString(status.error_message()), 0});
                } else if (!response.accepted()) {
                    guard->finish({PasteOutcome::Result::refused,
                                   QString::fromStdString(response.error()), 0});
                } else if (response.pending_characters() == 0) {
                    // Typed already, within one status interval: no status
                    // will show it pending, so none would show it draining.
                    std::fprintf(stderr, "beebium: the paste has been typed\n");
                    guard->finish({PasteOutcome::Result::typed, {}, 0});
                } else {
                    std::fprintf(stderr, "beebium: pasting %u characters\n",
                                 response.pending_characters());
                    guard->paste_seen_pending_ = true;
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionTyping::cancel_paste() {
    if (!rpc_ || !pasting_ || cancelling_) {
        return;
    }
    cancelling_ = true;
    const unsigned generation = generation_;
    QPointer<SessionTyping> guard(this);
    rpc_->post([stub = stub_, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        beebium::ClearTypingResponse response;
        const grpc::Status status = stub->ClearTyping(&context, {}, &response);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, status, response] {
                if (!guard || generation != guard->generation_ || !guard->pasting_) {
                    return;
                }
                if (status.ok()) {
                    guard->finish({PasteOutcome::Result::cancelled, {},
                                   response.characters_cleared()});
                } else {
                    guard->cancelling_ = false;
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionTyping::finish(PasteOutcome outcome) {
    pasting_ = false;
    paste_seen_pending_ = false;
    cancelling_ = false;
    Q_EMIT pasting_changed(false);
    Q_EMIT paste_finished(outcome);
}

}  // namespace beebium::linux_client
