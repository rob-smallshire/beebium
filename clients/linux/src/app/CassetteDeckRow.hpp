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

#ifndef BEEBIUM_LINUX_APP_CASSETTE_DECK_ROW_HPP
#define BEEBIUM_LINUX_APP_CASSETTE_DECK_ROW_HPP

#include "app/LiveInput.hpp"
#include "app/SessionCassette.hpp"

#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <map>
#include <memory>
#include <optional>

class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QToolButton;

namespace beebium::linux_client {

class ClientSettings;
class ElidedLabel;

// The icon a refused cassette request shows, chosen by the server's kind,
// never from its text (client specification 9.0): a note for what the user
// can put right at the deck (empty, occupied, read-only, a bad position, a
// live input asked to wind),
// a warning for a tape that would not go in, and for a kind not known here.
QStyle::StandardPixmap cassette_error_icon(int kind);
// The sentence that leads a blank tape's failure; the server's lower-case
// fragment completes it in the tooltip.
QString create_tape_error_sentence(int kind);

// What the deck says besides its state, one thing at a time: nothing, a
// short notice that times out (a drop refused, before anything was sent),
// or a failure that stays until dismissed, replaced or the tape is ejected.
// A failure the server refused shows its sentence verbatim and in full.
struct CassetteMessage {
    enum class Kind { none, notice, failure };
    Kind kind = Kind::none;
    QString text;
    QString detail;  // the tooltip: the server's fragment or gRPC's words
    QStyle::StandardPixmap icon = QStyle::SP_MessageBoxWarning;
    bool offer_eject = false;  // a deck-occupied refusal's recovery
};

// The cassette deck, above the floppy drives (client specification 9.0):
// the loaded tape's name, format and metadata; the transport and the motor
// relay; the tape counter and a marker on a timeline to scrub with; the
// keys; and the three ways in for an empty deck, Browse, a drop and the
// recent tapes, with New Blank Tape beside them (9.0.1). No catalogue, no
// waveform, no sound.
class CassetteDeckRow : public QWidget {
    Q_OBJECT

public:
    static constexpr int NOTICE_MS = 6000;
    // The timeline's steps per second of tape.
    static constexpr int SLIDER_SCALE = 10;

    using Shares = std::function<bool()>;
    using AudioInputs = std::function<std::vector<AudioInputDevice>()>;

    CassetteDeckRow(SessionCassette& cassette, ClientSettings* settings, Shares shares,
                    QWidget* parent = nullptr);

    void refresh();

    // Where the Live Input menu's devices come from, and what captures one:
    // the host's, unless a test says otherwise. The row owns the capture.
    void set_audio_inputs(AudioInputs inputs) { inputs_ = std::move(inputs); }
    void set_capture(LiveCapture* capture);
    // Start a live input as the tape, as the menu's entry does.
    void choose_live_input(const AudioInputDevice& device, const QString& channel);

    void show_failure(const CassetteMessage& message);
    void show_notice(const QString& text);
    void clear_message();
    const CassetteMessage& message() const { return message_; }

    // For tests: what the deck shows.
    QString name_text() const;
    QString state_text() const;
    QString metadata_text() const;
    QString counter_text() const;
    QString duration_text() const;  // empty when the length is unknown
    QString live_text() const;      // the ring, for a live tape
    QString message_text() const;
    bool motor_shown() const;
    QSlider* slider() const { return slider_; }
    QToolButton* key(CassetteKey key) const { return keys_.at(key); }
    QToolButton* browse_button() const { return browse_; }
    QMenu* browse_menu() const { return menu_; }
    QPushButton* eject_offer() const { return eject_offer_; }
    QToolButton* dismiss_button() const { return dismiss_; }
    // Why a drag would be refused now, or empty if it would be accepted: by
    // the deck's state and the server's locality only, never by the file.
    QString drop_refusal() const;
    void drop_files(const QStringList& paths);
    // The loaded tape's menu: Copy Path, and Open Containing Folder where
    // the path is this host's. The caller owns it; none for an empty deck.
    QMenu* make_path_menu();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    const DeckState& deck() const { return cassette_.deck(); }
    void present();
    void present_position();
    void insert(const QString& path);
    void press(CassetteKey key);
    void seek_to_slider();
    void browse();
    void rebuild_menu();
    void new_blank_tape(const BlankTapeTemplate& blank);
    void set_highlighted(bool highlighted);

    SessionCassette& cassette_;
    ClientSettings* settings_;
    Shares shares_;
    QLabel* motor_;
    ElidedLabel* name_;
    ElidedLabel* state_;
    ElidedLabel* metadata_;
    QWidget* position_line_;
    QLabel* counter_;
    QLabel* duration_;
    QSlider* slider_;
    QWidget* message_line_;
    QLabel* message_icon_;
    QLabel* message_text_;
    QPushButton* eject_offer_;
    QToolButton* dismiss_;
    QLabel* live_;
    QToolButton* browse_;
    QMenu* menu_;
    std::map<CassetteKey, QToolButton*> keys_;
    AudioInputs inputs_;
    LiveCapture* capture_ = nullptr;
    std::unique_ptr<LiveChunker> chunker_;
    QTimer notice_timer_;
    CassetteMessage message_;
    // The deck as last presented, its position aside.
    std::optional<DeckState> presented_;
    bool presented_local_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CASSETTE_DECK_ROW_HPP
