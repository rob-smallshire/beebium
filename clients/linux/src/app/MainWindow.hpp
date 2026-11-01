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

#include <QMainWindow>
#include <QRhiWidget>

#include <functional>

class QLabel;

namespace beebium::linux_client {

class ClientSettings;
class ConnectionOverlay;
class ConnectionRecovery;
class DisplayWidget;
class MachineSession;
class SystemPowerMonitor;

// A machine window: one per connection to a server. It holds the menu bar
// (empty until the client specification's menus are built) and the display.
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

    // Connect this window to the server at host:port.
    void connect_to(const QString& endpoint);

    // The whole client is quitting: a shared server this window launched is
    // left running without asking (4.5).
    void set_quitting(bool quitting) { quitting_ = quitting; }

    // File > Save, Save As, Rename Machine (5.4, 5.7). `then` is told whether
    // the machine was saved.
    void save(std::function<void(bool saved)> then = {});
    void save_as(std::function<void(bool saved)> then = {});
    void rename();

Q_SIGNALS:
    // File > Connect to Machine: the application shows its Connect dialog.
    void connect_requested();
    // File > Quit: the application closes every window.
    void quit_requested();
    // File > New Machine, File > Open Machine, Tools > Configure Beebium.
    void new_machine_requested();
    void open_machine_requested();
    void configure_requested();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void show_verdict();
    void build_menus();

    // Closing, as client specification 4.5 decides it; closeEvent starts it
    // and it ends by closing the window for real.
    enum class ClosePhase { open, closing, closed };
    void begin_close();
    void decide_close(int client_count);
    void shut_down_server();
    void finish_close();

    MachineSession* session_;
    ConnectionRecovery* recovery_;
    DisplayWidget* display_;
    ConnectionOverlay* overlay_;
    QLabel* description_label_ = nullptr;
    ClientSettings* settings_ = nullptr;
    QLabel* unsaved_label_ = nullptr;
    ClosePhase close_phase_ = ClosePhase::open;
    bool quitting_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MAIN_WINDOW_HPP
