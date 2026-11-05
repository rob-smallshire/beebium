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

#ifndef BEEBIUM_LINUX_APP_SESSION_SCREEN_TEXT_HPP
#define BEEBIUM_LINUX_APP_SESSION_SCREEN_TEXT_HPP

#include "connection/ScreenReading.hpp"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>

namespace grpc {
class Channel;
}

namespace beebium {
class GetScreenTextRequest;
}

namespace beebium::linux_client {

class RpcWorker;

// A machine window's screen text calls (client specification 6.5): holding
// the screen with its still, reading text from it or from the live screen,
// and releasing it. On a worker of their own, so a slow read never delays
// anything else the window asks. Answers arrive on the GUI thread; none
// arrive for calls made before a stop. Set up and torn down with the
// connection.
class SessionScreenText : public QObject {
    Q_OBJECT

public:
    // Nothing when the server would not hold the screen.
    using HoldDone = std::function<void(std::optional<HeldScreen> held)>;
    using ReadDone = std::function<void(ScreenReadResult result)>;

    explicit SessionScreenText(QObject* parent = nullptr);
    ~SessionScreenText() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();
    bool started() const { return rpc_ != nullptr; }

    void hold(HoldDone done);
    void read(const beebium::GetScreenTextRequest& request, ReadDone done);
    // Unanswered: the server lets an unreleased hold expire anyway.
    void release(std::uint64_t hold_id);

private:
    template <typename Result>
    void answer(unsigned generation, std::function<void(Result)> done, Result result);

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<RpcWorker> rpc_;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_SCREEN_TEXT_HPP
