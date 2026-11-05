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

#include "app/SessionScreenText.hpp"

#include "connection/RpcWorker.hpp"

#include "video.grpc.pb.h"

#include <QMetaObject>

#include <chrono>

namespace beebium::linux_client {

namespace {

constexpr auto CALL_DEADLINE = std::chrono::seconds(5);

}  // namespace

SessionScreenText::SessionScreenText(QObject* parent) : QObject(parent) {}

SessionScreenText::~SessionScreenText() {
    stop();
}

void SessionScreenText::start(std::shared_ptr<grpc::Channel> channel) {
    stop();
    channel_ = std::move(channel);
    rpc_ = std::make_unique<RpcWorker>();
}

void SessionScreenText::stop() {
    ++generation_;
    if (rpc_) {
        rpc_->stop();
        rpc_.reset();
    }
    channel_.reset();
}

template <typename Result>
void SessionScreenText::answer(unsigned generation, std::function<void(Result)> done,
                               Result result) {
    QMetaObject::invokeMethod(
        this,
        [this, generation, done = std::move(done), result = std::move(result)]() mutable {
            if (generation == generation_) {
                done(std::move(result));
            }
        },
        Qt::QueuedConnection);
}

void SessionScreenText::hold(HoldDone done) {
    if (!rpc_) {
        return;
    }
    rpc_->post([this, channel = channel_, generation = generation_,
                done = std::move(done)](grpc::ClientContext& context) mutable {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::VideoService::NewStub(channel);
        beebium::HoldScreenRequest request;
        request.set_include_frame(true);
        beebium::ScreenHold wire;
        std::optional<HeldScreen> held;
        if (stub->HoldScreen(&context, request, &wire).ok()) {
            auto result = held_screen(wire, std::chrono::steady_clock::now());
            if (auto* screen = std::get_if<HeldScreen>(&result)) {
                held = std::move(*screen);
            }
        }
        // HeldScreen owns its still, so it travels in a shared_ptr through
        // the queued call, which copies its functor.
        auto shared = std::make_shared<std::optional<HeldScreen>>(std::move(held));
        QMetaObject::invokeMethod(
            this,
            [this, generation, done = std::move(done), shared] {
                if (generation == generation_) {
                    done(std::move(*shared));
                }
            },
            Qt::QueuedConnection);
    });
}

void SessionScreenText::read(const beebium::GetScreenTextRequest& request, ReadDone done) {
    if (!rpc_) {
        return;
    }
    rpc_->post([this, channel = channel_, generation = generation_, request,
                done = std::move(done)](grpc::ClientContext& context) mutable {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::VideoService::NewStub(channel);
        beebium::ScreenText wire;
        const grpc::Status status = stub->GetScreenText(&context, request, &wire);
        ScreenReadResult result =
            status.ok() ? ScreenReadResult{screen_reading(wire)}
                        : ScreenReadResult{ScreenReadFailure{
                              status.error_code() == grpc::StatusCode::NOT_FOUND,
                              QString::fromStdString(status.error_message())}};
        answer<ScreenReadResult>(generation, std::move(done), std::move(result));
    });
}

void SessionScreenText::release(std::uint64_t hold_id) {
    if (!rpc_) {
        return;
    }
    rpc_->post([channel = channel_, hold_id](grpc::ClientContext& context) {
        context.set_deadline(std::chrono::system_clock::now() + CALL_DEADLINE);
        auto stub = beebium::VideoService::NewStub(channel);
        beebium::ReleaseScreenRequest request;
        request.set_hold_id(hold_id);
        beebium::ReleaseScreenResponse response;
        stub->ReleaseScreen(&context, request, &response);
    });
}

}  // namespace beebium::linux_client
