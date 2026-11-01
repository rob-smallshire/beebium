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

#ifndef BEEBIUM_LINUX_CONNECTION_CALL_CANCELLER_HPP
#define BEEBIUM_LINUX_CONNECTION_CALL_CANCELLER_HPP

#include <grpcpp/grpcpp.h>

#include <mutex>

namespace beebium::linux_client {

// Lets another thread cancel a sequence of unary calls: the call in hand at
// once, and every one after it before it starts. So a thread making calls
// can be joined promptly however stalled the server, instead of when the
// calls' deadlines run out.
class CallCanceller {
public:
    // About to call with `context`: false if already cancelled (skip it).
    bool begin(grpc::ClientContext& context) {
        std::lock_guard lock(mutex_);
        if (cancelled_) {
            return false;
        }
        in_hand_ = &context;
        return true;
    }

    // The call returned.
    void end() {
        std::lock_guard lock(mutex_);
        in_hand_ = nullptr;
    }

    void cancel() {
        std::lock_guard lock(mutex_);
        cancelled_ = true;
        if (in_hand_) {
            in_hand_->TryCancel();
        }
    }

    bool cancelled() const {
        std::lock_guard lock(mutex_);
        return cancelled_;
    }

private:
    mutable std::mutex mutex_;
    grpc::ClientContext* in_hand_ = nullptr;
    bool cancelled_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_CALL_CANCELLER_HPP
