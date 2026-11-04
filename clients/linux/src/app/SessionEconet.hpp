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

#ifndef BEEBIUM_LINUX_APP_SESSION_ECONET_HPP
#define BEEBIUM_LINUX_APP_SESSION_ECONET_HPP

#include "connection/EconetPresentation.hpp"

#include "econet_transport.pb.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace grpc {
class Channel;
}  // namespace grpc

namespace beebium::linux_client {

class EconetStatusStream;
class RpcWorker;

// A machine window's Econet (client specification 12.4): the status, read
// on connect and streamed while something watches it (the Network mode as
// it shows), and the transports the server lists, each addressed by its
// opaque id. A lost stream keeps the last status, no longer current; a
// lost server likewise. Set up and torn down with the connection.
class SessionEconet : public QObject {
    Q_OBJECT

public:
    explicit SessionEconet(QObject* parent = nullptr);
    ~SessionEconet() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    void set_watching(bool watching);
    bool watching() const { return watching_; }

    const EconetInputs& inputs() const { return inputs_; }
    const std::vector<beebium::EconetTransport>& transports() const { return transports_; }
    bool transports_loaded() const { return transports_loaded_; }
    bool transports_failed() const { return transports_failed_; }

    // Change the station links: adopted by the guest at its next Break.
    void set_station(std::uint32_t station);

Q_SIGNALS:
    void changed();
    // The server would not take a station number, in its words.
    void station_refused(const QString& message);

private:
    void read_status();
    void list_transports();
    void open_stream();
    void close_stream();

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<RpcWorker> rpc_;
    std::unique_ptr<EconetStatusStream> stream_;
    EconetInputs inputs_;
    std::vector<beebium::EconetTransport> transports_;
    bool transports_loaded_ = false;
    bool transports_failed_ = false;
    bool watching_ = false;
    unsigned generation_ = 0;
    unsigned stream_generation_ = 0;
    // The connection the stream's last snapshot came from: the read on
    // connect gives way to it.
    unsigned streamed_in_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_ECONET_HPP
