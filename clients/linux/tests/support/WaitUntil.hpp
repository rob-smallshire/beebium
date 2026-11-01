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

#ifndef BEEBIUM_LINUX_TESTS_WAIT_UNTIL_HPP
#define BEEBIUM_LINUX_TESTS_WAIT_UNTIL_HPP

#include <QCoreApplication>
#include <QElapsedTimer>

#include <chrono>
#include <thread>

namespace beebium::linux_client::testing {

using namespace std::chrono_literals;

// Runs the event loop until the predicate holds or the timeout passes.
template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 5000ms) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeout.count()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_WAIT_UNTIL_HPP
