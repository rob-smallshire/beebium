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

#ifndef BEEBIUM_LINUX_APP_SESSION_CASSETTE_HPP
#define BEEBIUM_LINUX_APP_SESSION_CASSETTE_HPP

#include "cassette.grpc.pb.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace grpc {
class Channel;
class ClientContext;
}

namespace beebium::linux_client {

class CassetteStatusStream;
class RpcWorker;

// The tape in the deck, as the server describes it (client specification 9.0).
struct TapeInfo {
    QString name;    // the file's name
    QString url;     // as it was inserted: a file:// URL or a path
    QString format;  // "UEF" or "CSW"
    QString title;   // the image's own title, else empty
    QString origin;  // the tool that made the image, else empty
    QString target_machine;
    double duration_seconds = 0.0;  // 0 when unknown
    bool recordable = false;        // a UEF can be recorded on, a CSW cannot

    friend bool operator==(const TapeInfo&, const TapeInfo&) = default;
};

// The deck as the status stream last reported it. Transport and relay are
// independent: the tape moves only while the deck plays, records or winds,
// and plays or records only while the relay is closed.
struct DeckState {
    enum class Transport { stopped, playing, recording, rewinding, fast_forwarding };
    bool known = false;  // a first status has come
    bool has_interface = false;
    bool tape_inserted = false;
    TapeInfo tape;
    Transport transport = Transport::stopped;
    double position_seconds = 0.0;
    bool motor_on = false;

    friend bool operator==(const DeckState&, const DeckState&) = default;
};

DeckState deck_state_from(const beebium::DeckStatus& status);

// A position as the tape counter shows it: minutes and seconds, "0:00",
// "12:05", "75:30", the seconds whole and never rounded up.
QString tape_counter_text(double seconds);
// The transport and the relay together, as the deck reads: "Stopped,
// motor off", "Playing, motor off" (a tape waiting for the machine, not a
// fault), "Recording, motor on", "Rewinding, motor off".
QString deck_state_text(const DeckState& deck);

// The keys of the deck, and Eject.
enum class CassetteKey { play, stop, rewind, fast_forward, record, seek, eject };

// How a cassette call went. A refused InsertTape or key carries the
// server's sentence, shown verbatim; a refused CreateTape a lower-case
// fragment, its kind choosing the sentence. reached is false when the call
// itself failed, the message then gRPC's.
struct CassetteOutcome {
    bool ok = false;
    bool reached = true;
    int kind = 0;  // CassetteErrorKind or CreateTapeErrorKind
    QString message;
    QString created_path;
};

// A blank tape template the server ships (9.0.1).
struct BlankTapeTemplate {
    QString label;     // "Blank C30 (30 minutes)"
    QString filename;  // "Blank C30 (30 minutes).uef"
    double duration_seconds = 0.0;
    QString extension() const;  // ".uef"
};

// A machine window's cassette deck (client specification 9.0): its state
// from SubscribeDeckStatus, set up and torn down with the connection; the
// keys, inserts and blank tapes as calls whose outcomes come back
// classified, never to be parsed.
class SessionCassette : public QObject {
    Q_OBJECT

public:
    explicit SessionCassette(QObject* parent = nullptr);
    ~SessionCassette() override;

    void start(std::shared_ptr<grpc::Channel> channel);
    void stop();

    const DeckState& deck() const { return deck_; }
    const std::vector<BlankTapeTemplate>& blank_tapes() const { return blank_tapes_; }

    void insert(const QString& path);
    void press(CassetteKey key);
    void seek(double position_seconds);
    // Copy a template to `path` on the server's host.
    void create_tape(const QString& label, const QString& path);

Q_SIGNALS:
    void deck_changed();
    void insert_finished(const QString& path, const beebium::linux_client::CassetteOutcome& outcome);
    void key_finished(beebium::linux_client::CassetteKey key,
                      const beebium::linux_client::CassetteOutcome& outcome);
    void create_finished(const QString& label, const beebium::linux_client::CassetteOutcome& outcome);
    void blank_tapes_changed();

private:
    void on_status(unsigned generation, const beebium::DeckStatus& status);
    void on_unavailable(unsigned generation);
    void call_key(CassetteKey key, double position_seconds);
    template <typename Result>
    void post(std::function<Result(grpc::ClientContext&)> call,
              std::function<void(const Result&)> deliver);

    std::shared_ptr<beebium::CassetteService::Stub> stub_;
    std::unique_ptr<CassetteStatusStream> stream_;
    std::unique_ptr<RpcWorker> rpc_;
    DeckState deck_;
    std::vector<BlankTapeTemplate> blank_tapes_;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_CASSETTE_HPP
