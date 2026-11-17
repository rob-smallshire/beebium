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

#include "app/LiveInput.hpp"

#include "cassette.grpc.pb.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace grpc {
class Channel;
class ClientContext;
}

namespace beebium::linux_client {

class CassetteStatusStream;
class LiveStream;
class RpcWorker;

// The tape in the deck, as the server describes it (client specification 9.0).
struct TapeInfo {
    QString name;    // the file's name
    QString url;     // as it was inserted: a file:// URL or a path
    QString format;  // "UEF", "CSW", "WAV" or "LIVE"
    QString title;   // the image's own title, else empty
    QString origin;  // the tool that made the image, else empty
    QString target_machine;
    double duration_seconds = 0.0;  // 0 when unknown
    bool recordable = false;        // the server can keep a recording on it: a writable UEF
    unsigned sample_rate = 0;       // a WAV's, in Hz; 0 for UEF and CSW
    QString channel;                // the WAV channel played, "left" or "right"
    QString record_refusal;         // why not, as the server says, when not recordable

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
    // A live tape's ring: how full (0 to 1), and the samples it dropped
    // (overruns) and made up as silence (underruns).
    double live_ring_fill = 0.0;
    std::uint64_t live_overruns = 0;
    std::uint64_t live_underruns = 0;
    // Whether the tape's signal map is ready, and how many the server has
    // published: a new one after a recording.
    bool signal_map_ready = false;
    std::uint32_t signal_map_generation = 0;

    // A live input is the tape: it cannot be wound, sought or recorded on.
    bool live() const { return tape_inserted && tape.format == QStringLiteral("LIVE"); }

    friend bool operator==(const DeckState&, const DeckState&) = default;
};

DeckState deck_state_from(const beebium::DeckStatus& status);

// What the tape is, as the deck's second line reads it, in order: the
// format, a WAV's sample rate and channel beside it, then the image's own
// title, origin and target machine, each where it is known.
QStringList tape_description(const TapeInfo& tape);

// A position as the tape counter shows it: minutes and seconds, "0:00",
// "12:05", "75:30", the seconds whole and never rounded up.
QString tape_counter_text(double seconds);
// The transport as the deck reads it, as the macOS client's does: "Stopped",
// "Playing", "Rewinding", "Fast forwarding"; and for play and record, where
// a closed relay is what moves the tape, "Playing, motor off" and
// "Recording, motor off" while the machine has not asked for the tape (a
// tape waiting, not a fault). The relay is also shown on its own, as a dot.
QString deck_state_text(const DeckState& deck);

// Where the tape carries signal, as the server maps it: one level a second,
// 0 silent to 255 signal throughout. Empty until the map is ready, and for
// a live tape.
struct SignalMap {
    unsigned seconds = 0;
    QByteArray levels;
    friend bool operator==(const SignalMap&, const SignalMap&) = default;
};
// The map at `width` columns (client specification 9.0, The signal map):
// a column is lit where the loudest of the seconds it spans is at least
// half signal (128). Nothing is lit without levels.
std::vector<bool> signal_columns(unsigned seconds, const QByteArray& levels, int width);

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

// How a live stream ended: the call's outcome (the server's sentence when
// it refused the stream) and its counts of samples.
struct LiveSummary {
    CassetteOutcome outcome;
    std::uint64_t samples_received = 0;
    std::uint64_t samples_dropped = 0;
    std::uint64_t silence_inserted = 0;
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
    // The tape's signal map once the server has it ready, fetched once for
    // each generation it publishes; empty otherwise.
    const SignalMap& signal_map() const { return signal_map_; }
    const std::vector<BlankTapeTemplate>& blank_tapes() const { return blank_tapes_; }

    void insert(const QString& path);
    void press(CassetteKey key);
    void seek(double position_seconds);
    // Copy a template to `path` on the server's host.
    void create_tape(const QString& label, const QString& path);

    // A live input as the tape (9.0, Live input): the stream opens with the
    // format, and the server puts the live tape in, stopped. Samples go as
    // they come; a refusal is heard at the next write. Finishing closes the
    // stream's sending side, and the deck lets the tape go once it has
    // played out what the server holds.
    void start_live(const LiveInputFormat& format);
    void push_live(const QByteArray& samples);
    void finish_live();
    // A stream of this session's is open, finishing or not.
    bool streaming() const { return live_ != nullptr; }
    bool live_finishing() const { return live_finishing_; }

Q_SIGNALS:
    void deck_changed();
    void insert_finished(const QString& path, const beebium::linux_client::CassetteOutcome& outcome);
    void key_finished(beebium::linux_client::CassetteKey key,
                      const beebium::linux_client::CassetteOutcome& outcome);
    void create_finished(const QString& label, const beebium::linux_client::CassetteOutcome& outcome);
    void blank_tapes_changed();
    void signal_map_changed();
    void live_ended(const beebium::linux_client::LiveSummary& summary);

private:
    void on_status(unsigned generation, const beebium::DeckStatus& status);
    void on_unavailable(unsigned generation);
    void follow_signal_map();
    void drop_signal_map();
    void call_key(CassetteKey key, double position_seconds);
    template <typename Result>
    void post(std::function<Result(grpc::ClientContext&)> call,
              std::function<void(const Result&)> deliver);

    std::shared_ptr<beebium::CassetteService::Stub> stub_;
    std::unique_ptr<CassetteStatusStream> stream_;
    std::unique_ptr<LiveStream> live_;
    bool live_finishing_ = false;
    std::unique_ptr<RpcWorker> rpc_;
    DeckState deck_;
    std::vector<BlankTapeTemplate> blank_tapes_;
    SignalMap signal_map_;
    // The tape and generation of the map last asked for.
    std::optional<std::pair<QString, std::uint32_t>> signal_map_asked_;
    unsigned generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SESSION_CASSETTE_HPP
