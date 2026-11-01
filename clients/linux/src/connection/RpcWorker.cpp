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

#include "connection/RpcWorker.hpp"

#if defined(__linux__)
#include <pthread.h>
#endif

namespace beebium::linux_client {

RpcWorker::RpcWorker() : worker_([this] { run(); }) {}

RpcWorker::~RpcWorker() {
    stop();
}

void RpcWorker::post(std::function<void()> job) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void RpcWorker::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void RpcWorker::run() {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "bbm-rpc");
#endif
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
}

}  // namespace beebium::linux_client
