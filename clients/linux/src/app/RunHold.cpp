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

#include "app/RunHold.hpp"

#include <utility>

namespace beebium::linux_client {

RunHold::RunHold(QObject* parent) : QObject(parent) {
    bound_.setSingleShot(true);
    bound_.setTimerType(Qt::PreciseTimer);  // never early
    connect(&bound_, &QTimer::timeout, this, &RunHold::fire);
}

void RunHold::arm(bool ready, std::function<void()> run, int bound_ms) {
    cancel();
    run_ = std::move(run);
    if (ready) {
        fire();
        return;
    }
    bound_.start(bound_ms);
}

void RunHold::release() {
    if (held()) {
        fire();
    }
}

void RunHold::cancel() {
    bound_.stop();
    run_ = nullptr;
}

void RunHold::fire() {
    bound_.stop();
    auto run = std::exchange(run_, nullptr);
    if (run) {
        run();
    }
}

}  // namespace beebium::linux_client
