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

#ifndef BEEBIUM_LINUX_APP_RUN_HOLD_HPP
#define BEEBIUM_LINUX_APP_RUN_HOLD_HPP

#include <QObject>
#include <QTimer>

#include <functional>

namespace beebium::linux_client {

// Holds a launched machine's Run (client specification 5.5) until its sound
// output is consuming, so a cold launch's boot beep is heard: a sound
// output can take a second or two to start (PipeWire on a Raspberry Pi 400
// took 1.5 to 2.3 s), and what the machine says before then piles up and
// is trimmed away when playback begins. Never longer than the bound: a
// machine whose sound never comes still runs.
class RunHold : public QObject {
    Q_OBJECT

public:
    static constexpr int BOUND_MS = 3000;

    explicit RunHold(QObject* parent = nullptr);

    // Run `run` once: now if the sound is `ready`, else at release(), or
    // when the bound passes, whichever is first.
    void arm(bool ready, std::function<void()> run, int bound_ms = BOUND_MS);
    // The sound output is consuming.
    void release();
    // The connection went: never run.
    void cancel();

    bool held() const { return static_cast<bool>(run_); }

private:
    void fire();

    std::function<void()> run_;
    QTimer bound_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_RUN_HOLD_HPP
