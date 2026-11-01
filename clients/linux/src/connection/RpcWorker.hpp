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

#ifndef BEEBIUM_LINUX_CONNECTION_RPC_WORKER_HPP
#define BEEBIUM_LINUX_CONNECTION_RPC_WORKER_HPP

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace beebium::linux_client {

// A connection's unary calls, run one after another on a thread of its own
// so the GUI thread never waits on the network. Each job reports its result
// however it likes (the session posts it back to the GUI thread). Stopping
// lets the job in hand finish, its calls being bounded by deadlines, and
// drops the rest.
class RpcWorker {
public:
    RpcWorker();
    ~RpcWorker();

    RpcWorker(const RpcWorker&) = delete;
    RpcWorker& operator=(const RpcWorker&) = delete;

    void post(std::function<void()> job);
    void stop();

private:
    void run();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_RPC_WORKER_HPP
