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

#include "app/SessionPeripherals.hpp"

#include "connection/RpcWorker.hpp"
#include "connection/SerialStatusStream.hpp"

#include "peripheral_extension.grpc.pb.h"

#include <QMetaObject>
#include <QPointer>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(5);

}  // namespace

SessionPeripherals::SessionPeripherals(QObject* parent) : QObject(parent) {}

SessionPeripherals::~SessionPeripherals() {
    stop();
}

void SessionPeripherals::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = std::move(channel);
    rpc_ = std::make_unique<RpcWorker>();
    list();
    if (watching_serial_) {
        open_serial();
    }
}

void SessionPeripherals::stop() {
    ++generation_;
    close_serial();
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    channel_.reset();
}

void SessionPeripherals::list() {
    const unsigned generation = generation_;
    QPointer<SessionPeripherals> guard(this);
    rpc_->post([channel = channel_, guard, generation](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::PeripheralExtensionService::NewStub(channel);
        beebium::ListExtensionsResponse response;
        const grpc::Status status = stub->ListExtensions(&context, {}, &response);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, ok = status.ok(),
             message = QString::fromStdString(status.error_message()), response] {
                if (!guard || generation != guard->generation_) {
                    return;
                }
                if (ok) {
                    guard->tree_ = peripheral_tree(response);
                    guard->error_.clear();
                } else {
                    guard->error_ = message;
                }
                guard->loaded_ = true;
                Q_EMIT guard->changed();
            },
            Qt::QueuedConnection);
    });
}

void SessionPeripherals::set_watching_serial(bool watching) {
    if (watching == watching_serial_) {
        return;
    }
    watching_serial_ = watching;
    if (watching_serial_) {
        open_serial();
    } else {
        close_serial();
    }
}

void SessionPeripherals::open_serial() {
    if (!channel_ || serial_stream_) {
        return;
    }
    const unsigned generation = ++serial_generation_;
    serial_stream_ = std::make_unique<SerialStatusStream>(
        channel_, SerialStatusStream::Callbacks{
                      [this, generation](const beebium::SerialStatus& status) {
                          QMetaObject::invokeMethod(
                              this,
                              [this, generation, status] {
                                  if (generation != serial_generation_) {
                                      return;
                                  }
                                  const std::string before = serial_.text();
                                  serial_.observe(status);
                                  if (serial_.text() != before) {
                                      Q_EMIT serial_changed();
                                  }
                              },
                              Qt::QueuedConnection);
                      },
                      {},
                  });
    serial_stream_->start();
}

void SessionPeripherals::close_serial() {
    ++serial_generation_;
    if (serial_stream_) {
        serial_stream_->stop();
        serial_stream_.reset();
    }
}

}  // namespace beebium::linux_client
