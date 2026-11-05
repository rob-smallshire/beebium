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

#ifndef BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP
#define BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP

#include "app/MappingCatalogue.hpp"
#include "app/Presentation.hpp"

#include <QMainWindow>
#include <QSplitter>
#include <QRhiWidget>

#include <functional>
#include <memory>

class QLabel;
class QToolButton;

namespace beebium::linux_client {

class ClientSettings;
class ConnectionOverlay;
class ConnectionRecovery;
class DisplayWidget;
class MachineSession;
class SelectionCoordinator;
class SelectionOverlayWidget;
class SystemPowerMonitor;
class DisplayFrame;
class Sidebar;
class IndicatorStrip;
class KeyboardSidebar;
class ImmersiveExit;

// A machine window: one per connection to a server. It holds the menu bar
// (empty until the client specification's menus are built) and the display.
// The questions closing a machine window may ask (client specification 4.5),
// as a seam: the window asks through this, the default answers with message
// boxes, and tests answer without them.
struct ClosePrompts {
    enum class Shared { shut_down, leave_running, cancel };
    enum class Unsaved { save, discard, cancel };

    // Ours, and others use it: what to do with the server.
    std::function<Shared(QWidget* window, const QString& machine)> shared;
    // Ours, transient, written to, about to shut down: save it first?
    std::function<Unsaved(QWidget* window, const QString& machine)> unsaved;
    // The same, for a server on another host, which cannot be saved from
    // here (3.2, #263): close and lose the changes, or cancel; never save.
    std::function<Unsaved(QWidget* window, const QString& machine)> unsaved_remote;

    static ClosePrompts message_boxes();
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // `power` signals full system sleep and wake for recovery, and `settings`
    // holds the client's own settings; either may be null.
    explicit MainWindow(QRhiWidget::Api graphics_api = QRhiWidget::Api::Vulkan,
                        SystemPowerMonitor* power = nullptr, ClientSettings* settings = nullptr,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    MachineSession& session() { return *session_; }
    ConnectionRecovery& recovery() { return *recovery_; }
    DisplayWidget& display() { return *display_; }
    Sidebar& sidebar() { return *sidebar_; }
    IndicatorStrip& indicator_strip() { return *indicator_strip_; }
    // Whether the status bar's contents are showing, in the bar or, while
    // immersive, over the picture.
    bool status_shown() const;

    // Connect this window to the server at host:port.
    void connect_to(const QString& endpoint);

    // The whole client is quitting: a shared server this window launched is
    // left running without asking (4.5).
    void set_quitting(bool quitting) { quitting_ = quitting; }

    // Windowed, full screen or immersive (6.4), as the View menu, F11 and
    // the exit bar choose it.
    void set_presentation(Presentation presentation);
    // Go immersive the first time the machine is live (--immersive): a
    // machine shown edge to edge from the start, as on a kiosk.
    void set_immersive_when_live() { immersive_when_live_ = true; }
    Presentation presentation() const { return presentation_; }

    // Replace the close questions (tests).
    void set_close_prompts(ClosePrompts prompts) { prompts_ = std::move(prompts); }

    // File > Save, Save As, Rename Machine (5.4, 5.7). `then` is told whether
    // the machine was saved.
    void save(std::function<void(bool saved)> then = {});
    void save_as(std::function<void(bool saved)> then = {});
    // File > Move To: a saved machine to another folder, under its name.
    void move_to();
    void rename();

Q_SIGNALS:
    // File > Quit: the application closes every window.
    void quit_requested();
    // File > Show Beebium: the application brings its home window forward.
    void home_requested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void show_verdict();
    void build_menus();

    // Closing, as client specification 4.5 decides it; closeEvent starts it
    // and it ends by closing the window for real.
    enum class ClosePhase { open, closing, closed };
    void begin_close();
    void leave_running();
    void update_link_control();
    void decide_close(int client_count);
    void shut_down_server();
    // Signal a process this client launched (SIGTERM, a graceful shutdown)
    // unless it has already ended, wait for it without blocking the GUI, at
    // most 6 s, then close.
    void stop_process_then_close(qint64 pid, bool already_asked);
    void finish_close();
    // The close was cancelled: the window stays, and a later close is an
    // ordinary close, not part of a quit.
    void cancel_close();

    MachineSession* session_;
    ConnectionRecovery* recovery_;
    DisplayWidget* display_;
    ConnectionOverlay* overlay_;
    // Screen text selection and copy (6.5).
    void build_selection();
    std::unique_ptr<SelectionCoordinator> selection_;
    SelectionOverlayWidget* selection_overlay_ = nullptr;
    QWidget* status_strip_ = nullptr;
    IndicatorStrip* indicator_strip_ = nullptr;
    // An LED's related key, tapped (10).
    void tap_named_key(const QString& name);
    // Edit > Paste (7.5).
    void paste();
    // The machine's name as the title, with room for the unsaved marker.
    void show_title(const QString& name);
    // Make a mapping the active one, from the sidebar or the Edit menu's
    // switch (7.4): the keys, the sidebar's selection, the status bar and
    // the stored choice follow, and the one it replaces is recorded.
    void activate_mapping(const MappingChoice& choice);
    // Edit > Switch to ... Keyboard Mapping: its title names where it goes;
    // with nowhere to go it is disabled. The user's files are read again,
    // so a deleted previous mapping is forgotten.
    void update_mapping_switch();
    void switch_mapping();
    KeyboardSidebar* keyboard_mode_ = nullptr;
    MappingHistory mapping_history_;
    QAction* switch_mapping_action_ = nullptr;
    static constexpr int TAP_HOLD_MS = 50;
    QLabel* mapping_label_ = nullptr;
    QLabel* model_label_ = nullptr;
    ClientSettings* settings_ = nullptr;
    DisplayFrame* frame_ = nullptr;
    ImmersiveExit* immersive_exit_ = nullptr;
    QAction* status_bar_action_ = nullptr;
    QAction* full_screen_action_ = nullptr;
    QAction* immersive_action_ = nullptr;
    QAction* sidebar_action_ = nullptr;
    QSplitter* splitter_ = nullptr;
    Sidebar* sidebar_ = nullptr;
    // While immersive: whether the sidebar and status bar are over the picture.
    bool immersive_sidebar_ = false;
    bool immersive_status_bar_ = false;
    bool overlaid_ = false;
    void place_sidebar_and_status(bool immersive);
    void place_overlays();
    Presentation presentation_ = Presentation::windowed;
    bool immersive_when_live_ = false;
    void apply_presentation();
    // Immersive is offered only while the machine is live (6.4).
    void update_immersive_enabled();
    ClosePhase close_phase_ = ClosePhase::open;
    bool quitting_ = false;
    // The link to a machine this client launched (13.3), and whether the
    // user has asked for it to be left running when this window closes.
    QToolButton* link_button_ = nullptr;
    bool unlink_requested_ = false;
    ClosePrompts prompts_ = ClosePrompts::message_boxes();
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP
