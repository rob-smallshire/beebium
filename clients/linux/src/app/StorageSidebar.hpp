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

#ifndef BEEBIUM_LINUX_APP_STORAGE_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_STORAGE_SIDEBAR_HPP

#include "app/SessionDiscs.hpp"

#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <vector>

class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace beebium::linux_client {

class ClientSettings;
class CoreCli;

// The short marker a failure shows inline, chosen by the server's
// classification, never from its text (client specification 9.6); empty
// for a kind not known here, where the caller's own generic reads honestly.
QString disc_error_marker(int kind);
QString create_disc_error_marker(int kind);
// The sentence that leads a blank disc's failure; the server's lower-case
// fragment completes it in the details.
QString create_disc_error_sentence(int kind);
// "Elite.ssd", or "Elite.ssd (Games)" when another recent disc has the same
// name: what the Recent menu shows for each path, in order.
QStringList recent_disc_labels(const QStringList& paths);

// What a drive row says besides its state, at most one thing at a time
// (9.6): nothing, a short notice that times out (a precondition, no server
// detail), or a failure that stays until cleared, replaced or ejected, with
// the server's words a click away.
struct DriveMessage {
    enum class Kind { none, notice, failure };
    Kind kind = Kind::none;
    QString marker;
    QString sentence;
    QString detail;
};

// One floppy drive, after the macOS client's drive row.
class DriveRow : public QWidget {
    Q_OBJECT

public:
    static constexpr int NOTICE_MS = 6000;
    // An ordinary eject waits about half a second for the motor; one still
    // waiting after this is offered Force Eject (9.5).
    static constexpr int EJECT_STALL_MS = 2000;

    using Shares = std::function<bool()>;
    using Extensions = std::function<QStringList()>;

    DriveRow(SessionDiscs& discs, int number, ClientSettings* settings, Shares shares,
             Extensions extensions, QWidget* parent = nullptr);

    int number() const { return number_; }
    void refresh();

    // From the window: an outcome for this drive.
    void show_failure(const QString& marker, const QString& sentence, const QString& detail);
    void show_notice(const QString& text);
    void clear_message();
    const DriveMessage& message() const { return message_; }

    // For tests and the window: what the row shows.
    QString state_text() const;
    bool eject_stalled() const { return eject_stalled_; }
    QToolButton* browse_button() const { return browse_; }
    QPushButton* eject_button() const { return eject_; }
    QPushButton* cancel_button() const { return cancel_; }
    QPushButton* force_button() const { return force_; }
    QMenu* browse_menu() const { return menu_; }
    bool highlighted() const { return highlighted_; }
    // Why a drag would be refused now, or empty if it would be accepted:
    // by the drive's state and the server's locality only, never by what
    // the file is (9.4).
    QString drop_refusal() const;
    // As a drop of these files would go.
    void drop_files(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    const DriveInfo* drive() const { return discs_.drive(number_); }
    void browse();
    void rebuild_menu();
    void new_blank_disc(const BlankDiscTemplate& blank);
    void show_details();
    void set_highlighted(bool highlighted);
    void present();

    SessionDiscs& discs_;
    int number_;
    ClientSettings* settings_;
    Shares shares_;
    Extensions extensions_;
    QLabel* title_;
    QLabel* motor_;
    QLabel* state_;
    QLabel* detail_line_;
    QWidget* message_line_;
    QLabel* message_marker_;
    QPushButton* details_;
    QToolButton* dismiss_;
    QToolButton* browse_;
    QMenu* menu_;
    QPushButton* eject_;
    QPushButton* cancel_;
    QPushButton* force_;
    QTimer notice_timer_;
    QTimer stall_timer_;
    DriveMessage message_;
    DriveInfo::State shown_state_ = DriveInfo::State::empty;
    bool eject_stalled_ = false;
    bool highlighted_ = false;
};

// The Storage mode (client specification 9): the floppy drives, a row each,
// from the server's disc controller; a machine without one says so. The
// machine's other storage, the peripherals', belongs to the Peripherals
// mode, which this client does not have yet.
class StorageSidebar : public QWidget {
    Q_OBJECT

public:
    StorageSidebar(SessionDiscs& discs, ClientSettings* settings, DriveRow::Shares shares,
                   QWidget* parent = nullptr);
    ~StorageSidebar() override;

    std::vector<DriveRow*> rows() const { return rows_; }
    DriveRow* row(int number) const;
    QString status_text() const;
    // The disc formats' extensions, once read from a local server.
    QStringList floppy_extensions() const { return extensions_; }

private:
    void rebuild();
    void load_formats();

    SessionDiscs& discs_;
    ClientSettings* settings_;
    DriveRow::Shares shares_;
    QLabel* status_;
    QLabel* status_detail_;
    QVBoxLayout* rows_layout_;
    std::vector<DriveRow*> rows_;
    std::unique_ptr<CoreCli> cli_;
    QStringList extensions_;
    bool formats_asked_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_STORAGE_SIDEBAR_HPP
