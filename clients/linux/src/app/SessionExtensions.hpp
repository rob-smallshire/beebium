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

#ifndef BEEBIUM_LINUX_APP_SESSION_EXTENSIONS_HPP
#define BEEBIUM_LINUX_APP_SESSION_EXTENSIONS_HPP

#include "extension_ui.pb.h"

#include <grpcpp/support/status.h>

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <string>

namespace grpc {
class Channel;
}  // namespace grpc

namespace beebium::linux_client {

class ExtensionViewStream;
class RpcWorker;

// A connection's way to extensions' views (client specification 12.1): the
// channel the views subscribe over, and one queue for their actions. Set up
// and torn down with the connection.
class SessionExtensions : public QObject {
    Q_OBJECT

public:
    explicit SessionExtensions(QObject* parent = nullptr);
    ~SessionExtensions() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();
    const std::shared_ptr<grpc::Channel>& channel() const { return channel_; }

    // Fire and observe: the result is the next view, not this call's
    // answer. A refusal (a stale revision, say) changes nothing.
    void dispatch(const beebium::DispatchRequest& request);

Q_SIGNALS:
    void started();
    void stopped();
    void refused(const QString& control_id, const QString& error);

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<RpcWorker> rpc_;
    unsigned generation_ = 0;
};

// One extension's view, subscribed while something watches it (its panel
// while it shows). An extension that has gone (the stream ends cleanly, or
// with NOT_FOUND) drops its view, which is not an error; a lost server
// keeps the last view, marked stale.
class ExtensionViewFeed : public QObject {
    Q_OBJECT

public:
    ExtensionViewFeed(SessionExtensions& extensions, std::string extension_id,
                      QObject* parent = nullptr);
    ~ExtensionViewFeed() override;

    const std::string& extension_id() const { return extension_id_; }
    void set_watching(bool watching);
    bool watching() const { return watching_; }

    const std::optional<beebium::View>& view() const { return view_; }
    bool stale() const { return stale_; }
    bool gone() const { return gone_; }

Q_SIGNALS:
    void changed();

private:
    void open();
    void close();
    void on_view(unsigned generation, const beebium::View& view);
    void on_end(unsigned generation, grpc::StatusCode code, bool ours);

    SessionExtensions& extensions_;
    std::string extension_id_;
    std::unique_ptr<ExtensionViewStream> stream_;
    std::optional<beebium::View> view_;
    bool watching_ = false;
    bool stale_ = false;
    bool gone_ = false;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_EXTENSIONS_HPP
