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

#ifndef BEEBIUM_LINUX_CONNECTION_STREAM_READER_HPP
#define BEEBIUM_LINUX_CONNECTION_STREAM_READER_HPP

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace beebium::linux_client {

// How a stream ended. cancelled_by_client separates our own stop() from a
// loss: the status code alone cannot, since a server may also end a stream
// as CANCELLED (lifecycle-management.md section 2).
struct StreamEnd {
    grpc::StatusCode code = grpc::StatusCode::OK;
    std::string message;
    bool cancelled_by_client = false;
};

// One server-streaming call read on a thread of its own: the pattern every
// stream of a connection shares. `open` starts the call on the reader's
// context; each message goes to `message` on the reader thread; `finished`
// runs once, on the reader thread, however the stream ends. stop() cancels
// and joins, and the end it reports is then marked as the client's own.
template <typename Message>
class StreamReader {
public:
    using Open = std::function<std::unique_ptr<grpc::ClientReader<Message>>(grpc::ClientContext&)>;
    struct Callbacks {
        std::function<void(Message&)> message;
        std::function<void(const StreamEnd&)> finished;
    };

    // `thread_name` names the reader for per-thread measurements: 15
    // characters at most.
    StreamReader(std::string thread_name, Open open, Callbacks callbacks)
        : thread_name_(std::move(thread_name)), open_(std::move(open)),
          callbacks_(std::move(callbacks)) {}

    ~StreamReader() { stop(); }

    StreamReader(const StreamReader&) = delete;
    StreamReader& operator=(const StreamReader&) = delete;

    void start() {
        if (reader_.joinable() || stop_requested_.load()) {
            return;
        }
        reader_ = std::thread([this] { run(); });
    }

    // Cancel the call and join the reader. Idempotent; safe before start().
    void stop() {
        stop_requested_.store(true);
        context_.TryCancel();
        if (reader_.joinable()) {
            reader_.join();
        }
    }

private:
    void run() {
#if defined(__linux__)
        pthread_setname_np(pthread_self(), thread_name_.c_str());
#endif
        auto reader = open_(context_);
        Message message;
        while (reader->Read(&message)) {
            if (callbacks_.message) {
                callbacks_.message(message);
            }
        }
        const grpc::Status status = reader->Finish();
        StreamEnd end;
        end.code = status.error_code();
        end.message = status.error_message();
        end.cancelled_by_client = stop_requested_.load();
        if (callbacks_.finished) {
            callbacks_.finished(end);
        }
    }

    std::string thread_name_;
    Open open_;
    Callbacks callbacks_;
    grpc::ClientContext context_;
    std::atomic<bool> stop_requested_{false};
    std::thread reader_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_STREAM_READER_HPP
