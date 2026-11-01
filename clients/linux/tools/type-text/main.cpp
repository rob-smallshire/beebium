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

// beebium-type HOST:PORT TEXT [DISC]
//
// Inserts DISC, if given, into drive 0 with DiscService.InsertDisc, then types
// TEXT into a server's machine with KeyboardService.TypeQuickly (with
// translation, so \n is Return) and waits on WatchTypingStatus until the
// queue drains. For acceptance runs on a desktop, where keys synthesised
// with their own keymap do not reach the machine as physical keys; the
// machine's response is then watched in the client's own log.

#include "disc.grpc.pb.h"
#include "keyboard.grpc.pb.h"
#include "video.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: beebium-type HOST:PORT TEXT [DISC]\n");
        return 2;
    }
    const auto channel = grpc::CreateChannel(argv[1], grpc::InsecureChannelCredentials());
    if (argc == 4) {
        auto discs = beebium::DiscService::NewStub(channel);
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        beebium::InsertDiscRequest request;
        request.set_drive(0);
        request.set_url(argv[3]);  // a bare path: the server takes either
        beebium::InsertDiscResponse response;
        const grpc::Status status = discs->InsertDisc(&context, request, &response);
        if (!status.ok() || !response.success()) {
            std::fprintf(stderr, "beebium-type: disc not inserted: %s\n",
                         status.ok() ? response.error().c_str() : status.error_message().c_str());
            return 1;
        }
        std::printf("inserted %s\n", response.disc().name().c_str());
    }
    auto keyboard = beebium::KeyboardService::NewStub(channel);
    grpc::ClientContext watch_context;
    watch_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));
    beebium::WatchTypingStatusRequest watch_request;
    auto watch = keyboard->WatchTypingStatus(&watch_context, watch_request);
    {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        beebium::TypeQuicklyRequest request;
        request.set_text(argv[2]);
        request.set_translate(true);
        beebium::TypeQuicklyResponse response;
        const grpc::Status status = keyboard->TypeQuickly(&context, request, &response);
        if (!status.ok() || !response.accepted()) {
            std::fprintf(stderr, "beebium-type: not typed: %s\n",
                         status.ok() ? response.error().c_str() : status.error_message().c_str());
            return 1;
        }
        std::printf("typing %u characters\n", response.pending_characters());
    }
    bool seen_pending = false;
    beebium::TypingStatus status;
    while (watch->Read(&status)) {
        if (!status.idle()) {
            seen_pending = true;
        } else if (seen_pending) {
            std::printf("typed\n");
            watch_context.TryCancel();
            // What the machine made of it, a few seconds on.
            std::this_thread::sleep_for(std::chrono::seconds(4));
            auto video = beebium::VideoService::NewStub(channel);
            grpc::ClientContext screen_context;
            screen_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
            beebium::ScreenText screen;
            if (video->GetScreenText(&screen_context, {}, &screen).ok()) {
                std::printf("--- the screen:\n%s\n---\n", screen.text().c_str());
            }
            return 0;
        }
    }
    std::printf("the typing status stream ended\n");
    return 0;
}
