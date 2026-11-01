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

#ifndef BEEBIUM_LINUX_APP_SESSION_TYPING_HPP
#define BEEBIUM_LINUX_APP_SESSION_TYPING_HPP

#include "connection/StreamReader.hpp"

#include "keyboard.grpc.pb.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace grpc {
class Channel;
}

namespace beebium::linux_client {

class RpcWorker;

// How a paste ended (client specification 7.5).
struct PasteOutcome {
    enum class Result { typed, cancelled, refused, cut_short_by_reset, failed };
    Result result = Result::typed;
    QString message;  // the server's words, for refused and failed
    unsigned count = 0;  // characters cleared or discarded
};

// A machine window's typing (client specification 7.3, 7.5): the server's
// WatchTypingStatus, whose guest_ready rising edge is the moment to read the
// machine's lock latches after a boot or reset (never a wall-clock settle);
// and pasting as typing, which the server queues and the machine paces, so
// the client waits for the server to say the queue has drained, never a
// timeout of its own. Set up and torn down with the connection.
class SessionTyping : public QObject {
    Q_OBJECT

public:
    static constexpr unsigned MIN_INTERVAL_MS = 50;

    explicit SessionTyping(QObject* parent = nullptr);
    ~SessionTyping() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    bool guest_ready() const { return guest_ready_.value_or(false); }
    bool pasting() const { return pasting_; }
    unsigned pending_characters() const { return pending_; }

    // Type the text, with the server translating host conventions (line
    // endings, typographic punctuation): it moves characters, not bytes.
    void paste(const QString& text);
    // Drop what is still to type; what was typed stays typed.
    void cancel_paste();

Q_SIGNALS:
    // The guest has asked for input since its last reset: the sync point.
    void guest_ready_rose();
    void pasting_changed(bool pasting);
    void paste_finished(const beebium::linux_client::PasteOutcome& outcome);

private:
    void on_status(unsigned generation, const beebium::TypingStatus& status);
    void finish(PasteOutcome outcome);

    std::shared_ptr<beebium::KeyboardService::Stub> stub_;
    std::unique_ptr<StreamReader<beebium::TypingStatus>> stream_;
    std::unique_ptr<RpcWorker> rpc_;
    std::optional<bool> guest_ready_;
    bool pasting_ = false;
    // The queue has been seen holding text since the paste was accepted, so
    // an idle status after it is the drain, not an old one.
    bool paste_seen_pending_ = false;
    bool cancelling_ = false;
    unsigned pending_ = 0;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_TYPING_HPP
