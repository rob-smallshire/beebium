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

#ifndef BEEBIUM_LINUX_APP_SESSION_DISCS_HPP
#define BEEBIUM_LINUX_APP_SESSION_DISCS_HPP

#include "disc.grpc.pb.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace grpc {
class Channel;
}

namespace beebium::linux_client {

class DiscStream;
class RpcWorker;

// One floppy drive as the server reports it (client specification 9.1).
struct DriveInfo {
    enum class State { empty, loaded, ejecting };
    int number = 0;
    State state = State::empty;
    QString disc_name;
    QString disc_url;  // a file:// URL or a bare path, as it was mounted
    QString format;    // "SSD", "ADL", "HFE v3"
    int sides = 0;
    bool motor_on = false;
    bool write_protected = false;
};

// How a disc call went: the server's classification and its own words.
struct DiscOutcome {
    bool ok = false;
    int kind = 0;     // DiscErrorKind or CreateDiscErrorKind
    QString message;  // verbatim, lower case, for the details
    QString created_path;
};

// The URL a local path is sent as: a percent-encoded file:// URL, which the
// server decodes (DiscUrl), spaces and all. One convention end to end.
QString disc_url_for_path(const QString& path);
// The path of what the server reports mounted: its disc_url may be a
// file:// URL (a Browse or a drop) or a bare path (a preset, the CLI).
QString path_for_disc_url(const QString& url);

// The template blank discs the server ships (9.3).
struct BlankDiscTemplate {
    QString label;     // "Acorn DFS 80T SS 200K"
    QString filename;  // "Acorn DFS 80T SS 200K.ssd"
    QString extension() const;  // ".ssd"
};

// A machine window's floppy drives (client specification 9): one
// GetDriveStatus, then the event stream, set up and torn down with the
// connection; inserts, ejects and blank discs as calls whose outcomes come
// back classified, never to be parsed.
class SessionDiscs : public QObject {
    Q_OBJECT

public:
    explicit SessionDiscs(QObject* parent = nullptr);
    ~SessionDiscs() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    bool loaded() const { return loaded_; }
    // A machine with no disc controller has no drives: a state, not an error.
    bool has_controller() const { return has_controller_; }
    QString controller_type() const { return controller_type_; }
    const std::vector<DriveInfo>& drives() const { return drives_; }
    const DriveInfo* drive(int number) const;
    const std::vector<BlankDiscTemplate>& blank_discs() const { return blank_discs_; }

    void insert(int drive, const QString& path);
    // Safe unless immediate: the server waits as long as the drive is busy,
    // and never gives up by itself (9.5).
    void eject(int drive, bool immediate);
    void cancel_eject(int drive);
    // Copy a template to `path` on the server's host.
    void create_disc(const QString& label, const QString& path);

Q_SIGNALS:
    void loaded_changed(bool loaded);
    void drives_changed();
    void insert_finished(int drive, const QString& path,
                         const beebium::linux_client::DiscOutcome& outcome);
    void eject_finished(int drive, const beebium::linux_client::DiscOutcome& outcome);
    void cancel_finished(int drive, const beebium::linux_client::DiscOutcome& outcome);
    void create_finished(const QString& label, const beebium::linux_client::DiscOutcome& outcome);
    void blank_discs_changed();

private:
    void on_status(unsigned generation, const beebium::GetDriveStatusResponse& status);
    void on_event(unsigned generation, const beebium::DiscEvent& event);
    template <typename Result>
    void post(std::function<Result(grpc::ClientContext&)> call,
              std::function<void(const Result&)> deliver);

    std::shared_ptr<grpc::Channel> channel_;
    std::shared_ptr<beebium::DiscService::Stub> stub_;
    std::unique_ptr<DiscStream> stream_;
    std::unique_ptr<RpcWorker> rpc_;
    bool loaded_ = false;
    bool has_controller_ = false;
    QString controller_type_;
    std::vector<DriveInfo> drives_;
    std::vector<BlankDiscTemplate> blank_discs_;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_DISCS_HPP
