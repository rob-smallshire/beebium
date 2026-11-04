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

#ifndef BEEBIUM_LINUX_APP_SESSION_SIDEWAYS_HPP
#define BEEBIUM_LINUX_APP_SESSION_SIDEWAYS_HPP

#include "connection/SidewaysTopology.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <string>

namespace grpc {
class Channel;
}

namespace beebium::linux_client {

class RpcWorker;
class SidewaysEventStream;

enum class SidewaysProtection { write_protect, hide };

// A machine window's sideways memory (client specification 11): the
// server's topology, read each time something starts watching it (the
// Memory mode as it shows) and kept live from the event stream while it
// watches. Protection is changed by group and kind and then read back, as
// there is no event for it: the topology shows the server's answer, never
// the request. Set up and torn down with the connection.
class SessionSideways : public QObject {
    Q_OBJECT

public:
    explicit SessionSideways(QObject* parent = nullptr);
    ~SessionSideways() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    // Kept across reconnects.
    void set_watching(bool watching);
    bool watching() const { return watching_; }

    // Whether the topology has been read since this connection began.
    bool loaded() const { return loaded_; }
    const SidewaysTopology& topology() const { return topology_; }

    void set_protection(const std::string& group_id, SidewaysProtection kind, bool engaged);

Q_SIGNALS:
    void changed();
    // The server would not change a protection switch, in its words.
    void protection_refused(const QString& message);

private:
    void read_status();
    void open_stream();
    void close_stream();
    void on_event(unsigned generation, const beebium::SidewaysEvent& event);

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<SidewaysEventStream> stream_;
    std::unique_ptr<RpcWorker> rpc_;
    SidewaysTopology topology_;
    bool loaded_ = false;
    bool watching_ = false;
    unsigned stream_generation_ = 0;
    unsigned connection_generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_SIDEWAYS_HPP
