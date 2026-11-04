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

#include "app/SessionExtensions.hpp"

#include "connection/ExtensionViewStream.hpp"
#include "connection/RpcWorker.hpp"

#include <QMetaObject>
#include <QPointer>

#include <chrono>
#include <cstdio>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(5);

}  // namespace

SessionExtensions::SessionExtensions(QObject* parent) : QObject(parent) {}

SessionExtensions::~SessionExtensions() {
    stop();
}

void SessionExtensions::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = std::move(channel);
    rpc_ = std::make_unique<RpcWorker>();
    Q_EMIT started();
}

void SessionExtensions::stop() {
    ++generation_;
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    if (channel_) {
        channel_.reset();
        Q_EMIT stopped();
    }
}

void SessionExtensions::dispatch(const beebium::DispatchRequest& request) {
    if (!rpc_ || !channel_) {
        return;
    }
    QPointer<SessionExtensions> guard(this);
    const unsigned generation = generation_;
    rpc_->post([channel = channel_, request, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::ExtensionUiService::NewStub(channel);
        beebium::DispatchResponse response;
        const grpc::Status status = stub->Dispatch(&context, request, &response);
        if (status.ok() && response.accepted()) {
            return;
        }
        const QString error = QString::fromStdString(status.ok() ? response.error()
                                                                 : status.error_message());
        // The view stays the server's: nothing to undo, but worth a line.
        std::fprintf(stderr, "beebium: extension action refused (%s/%s): %s\n",
                     request.extension_id().c_str(), request.control_id().c_str(),
                     qPrintable(error));
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, control = QString::fromStdString(request.control_id()), error] {
                if (guard && generation == guard->generation_) {
                    Q_EMIT guard->refused(control, error);
                }
            },
            Qt::QueuedConnection);
    });
}

ExtensionViewFeed::ExtensionViewFeed(SessionExtensions& extensions, std::string extension_id,
                                     QObject* parent)
    : QObject(parent), extensions_(extensions), extension_id_(std::move(extension_id)) {
    // A lost server keeps the last view, stale; a new connection reads it
    // afresh if it is still watched.
    connect(&extensions_, &SessionExtensions::stopped, this, [this] {
        close();
        if (view_ && !stale_) {
            stale_ = true;
            Q_EMIT changed();
        }
    });
    connect(&extensions_, &SessionExtensions::started, this, [this] {
        if (watching_) {
            open();
        }
    });
}

ExtensionViewFeed::~ExtensionViewFeed() {
    close();
}

void ExtensionViewFeed::set_watching(bool watching) {
    if (watching == watching_) {
        return;
    }
    watching_ = watching;
    if (watching_) {
        open();
    } else {
        // Released, as macOS does when a panel disappears.
        close();
    }
}

void ExtensionViewFeed::open() {
    if (stream_ || !extensions_.channel()) {
        return;
    }
    const unsigned generation = ++generation_;
    stream_ = std::make_unique<ExtensionViewStream>(
        extensions_.channel(), extension_id_,
        ExtensionViewStream::Callbacks{
            [this, generation](const beebium::View& view) {
                QMetaObject::invokeMethod(
                    this, [this, generation, view] { on_view(generation, view); },
                    Qt::QueuedConnection);
            },
            [this, generation](const StreamEnd& end) {
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, code = end.code, ours = end.cancelled_by_client] {
                        on_end(generation, code, ours);
                    },
                    Qt::QueuedConnection);
            },
        });
    stream_->start();
}

void ExtensionViewFeed::close() {
    ++generation_;
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
}

void ExtensionViewFeed::on_view(unsigned generation, const beebium::View& view) {
    if (generation != generation_) {
        return;
    }
    view_ = view;
    stale_ = false;
    gone_ = false;
    Q_EMIT changed();
}

void ExtensionViewFeed::on_end(unsigned generation, grpc::StatusCode code, bool ours) {
    if (generation != generation_ || ours) {
        return;
    }
    if (code == grpc::StatusCode::OK || code == grpc::StatusCode::NOT_FOUND ||
        code == grpc::StatusCode::CANCELLED) {
        // The extension is gone or unloaded: drop the view, quietly.
        view_.reset();
        stale_ = false;
        gone_ = true;
    } else if (view_) {
        // The server is lost: keep the last view.
        stale_ = true;
    }
    stream_.reset();
    Q_EMIT changed();
}

}  // namespace beebium::linux_client
