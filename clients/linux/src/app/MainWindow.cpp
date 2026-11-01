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

#include "app/MainWindow.hpp"

#include "app/ApplicationInfo.hpp"
#include "app/ClientSettings.hpp"
#include "app/ConnectionOverlay.hpp"
#include "app/ConnectionRecovery.hpp"
#include "app/DisplayWidget.hpp"
#include "app/ErrorPresentation.hpp"
#include "app/MachineSession.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

#include <cstdio>

namespace beebium::linux_client {

MainWindow::MainWindow(QRhiWidget::Api graphics_api, SystemPowerMonitor* power,
                       ClientSettings* settings, QWidget* parent)
    : QMainWindow(parent),
      session_(new MachineSession(this)),
      // Before anything else listens to the session: a failed reconnect is
      // counted by recovery before the window decides whether to say so.
      recovery_(new ConnectionRecovery(*session_, power, this)),
      display_(new DisplayWidget(session_->mailbox(), this)),
      overlay_(new ConnectionOverlay(display_)) {
    settings_ = settings;
    setWindowTitle(QStringLiteral("Beebium"));
    setAttribute(Qt::WA_DeleteOnClose);

    display_->setObjectName(QStringLiteral("display"));
    display_->setApi(graphics_api);
    setCentralWidget(display_);
    resize(1280, 1024 + menuBar()->sizeHint().height());

    connect(session_, &MachineSession::frame_ready, display_, &DisplayWidget::frame_ready);
    connect(session_, &MachineSession::keyboard_ready, this, [this] {
        KeySender* sender = session_->key_sender();
        display_->keyboard().attach(session_->key_table(),
                                    [sender](const std::vector<WireEvent>& events) {
                                        sender->send(events);
                                    });
        display_->setFocus();
    });
    connect(session_, &MachineSession::guest_ready, this,
            [this] { display_->keyboard().sync_caps_lock(); });
    connect(display_, &DisplayWidget::graphics_ready, this,
            [](const QString& description, bool software) {
                std::fprintf(stderr, "beebium: drawing with %s%s\n", qPrintable(description),
                             software ? " (SOFTWARE RENDERER, not the GPU)" : "");
            });
    connect(session_, &MachineSession::connection_failed, this,
            [this](ConnectionFailure failure, const QString& server_message) {
                std::fprintf(stderr, "beebium: connection failed: %s\n",
                             qPrintable(server_message));
                if (recovery_->recovering()) {
                    return;  // the overlay counts the attempts
                }
                ServerMessageDialog dialog(
                    tr("Could Not Connect"),
                    connection_failure_sentence(failure, session_->endpoint()),
                    server_message, this);
                dialog.exec();
            });
    connect(session_, &MachineSession::connected_to_server, this,
            [] { std::fprintf(stderr, "beebium: connected\n"); });
    connect(session_, &MachineSession::running_changed, this, [](bool running) {
        std::fprintf(stderr, "beebium: machine %s\n", running ? "running" : "waiting for Run");
    });
    connect(session_, &MachineSession::run_finished, this, [this](const CallOutcome& outcome) {
        if (!outcome.ok) {
            ServerMessageDialog dialog(tr("Could Not Run"),
                                       tr("The machine could not be started."), outcome.message,
                                       this);
            dialog.exec();
        }
    });
    connect(session_, &MachineSession::liveness_changed, this, [](Liveness liveness) {
        std::fprintf(stderr, "beebium: liveness %s\n", liveness_name(liveness));
    });

    // The connection's one verdict, over the frozen last frame (4.3, 4.4).
    display_->installEventFilter(this);
    connect(recovery_, &ConnectionRecovery::verdict_changed, this,
            [this](ConnectionVerdict verdict, int attempt) {
                std::fprintf(stderr, "beebium: connection %s%s\n", verdict_name(verdict),
                             verdict == ConnectionVerdict::reconnecting
                                 ? qPrintable(QStringLiteral(" (attempt %1)").arg(attempt))
                                 : "");
                if (!is_live(verdict)) {
                    // Nothing typed reaches a machine that is not there, and
                    // nothing stays held for when it returns.
                    display_->keyboard().detach();
                }
                show_verdict();
            });
    connect(overlay_, &ConnectionOverlay::retry_requested, recovery_, &ConnectionRecovery::retry);
    // Close goes through the window's own close, so it gets the same
    // teardown as the window manager's close button (4.5).
    connect(overlay_, &ConnectionOverlay::close_requested, this, &QWidget::close);
    // The title is the machine's name, exactly (5.7). Qt has no window
    // subtitle, so the description goes at the start of the status bar.
    description_label_ = new QLabel(this);
    statusBar()->addWidget(description_label_, 1);
    connect(session_, &MachineSession::identity_changed, this,
            [this](const QString& name, const QString& description) {
                setWindowTitle(name.isEmpty() ? QStringLiteral("Beebium") : name);
                description_label_->setText(description);
            });

    build_menus();
}

void MainWindow::build_menus() {
    // Client specification 13.2, as a KDE-style menu bar with Ctrl
    // shortcuts. While the display has focus a key the machine takes goes to
    // the machine first (DisplayWidget::event), so these shortcuts apply
    // when it does not; F11 is always the window's.
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* new_action = file->addAction(tr("&New Machine..."));
    new_action->setShortcut(QKeySequence::New);
    connect(new_action, &QAction::triggered, this, &MainWindow::new_machine_requested);
    QAction* open_action = file->addAction(tr("&Open Machine..."));
    open_action->setShortcut(QKeySequence::Open);
    connect(open_action, &QAction::triggered, this, &MainWindow::open_machine_requested);
    QAction* connect_action = file->addAction(tr("&Connect to Machine..."));
    connect_action->setShortcut(QKeySequence(tr("Ctrl+Shift+O")));
    connect(connect_action, &QAction::triggered, this, &MainWindow::connect_requested);
    file->addSeparator();
    QAction* close_action = file->addAction(tr("C&lose Window"));
    close_action->setShortcut(QKeySequence::Close);
    connect(close_action, &QAction::triggered, this, &QWidget::close);
    QAction* quit_action = file->addAction(tr("&Quit"));
    quit_action->setShortcut(QKeySequence::Quit);
    quit_action->setMenuRole(QAction::QuitRole);
    connect(quit_action, &QAction::triggered, this, &MainWindow::quit_requested);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QAction* status_bar_action = view->addAction(tr("Show &Status Bar"));
    status_bar_action->setCheckable(true);
    const bool show_status_bar =
        settings_ ? settings_->store().value(QStringLiteral("view/statusBar"), true).toBool() : true;
    status_bar_action->setChecked(show_status_bar);
    statusBar()->setVisible(show_status_bar);
    connect(status_bar_action, &QAction::toggled, this, [this](bool shown) {
        statusBar()->setVisible(shown);
        if (settings_) {
            settings_->store().setValue(QStringLiteral("view/statusBar"), shown);
        }
    });
    QAction* full_screen_action = view->addAction(tr("&Full Screen"));
    full_screen_action->setCheckable(true);
    full_screen_action->setShortcut(QKeySequence(Qt::Key_F11));
    // A window-wide shortcut, so it works whatever has focus, the display
    // included: the obvious way out of full screen.
    full_screen_action->setShortcutContext(Qt::WindowShortcut);
    connect(full_screen_action, &QAction::toggled, this, [this](bool full) {
        full ? showFullScreen() : showNormal();
    });

    QMenu* machine = menuBar()->addMenu(tr("&Machine"));
    QAction* run_action = machine->addAction(tr("&Run"));
    run_action->setToolTip(tr("Start a machine that is waiting to run"));
    run_action->setEnabled(false);
    connect(run_action, &QAction::triggered, session_, &MachineSession::run);
    connect(session_, &MachineSession::running_changed, run_action,
            [run_action](bool running) { run_action->setEnabled(!running); });

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    QAction* configure_action = tools->addAction(tr("&Configure Beebium..."));
    configure_action->setMenuRole(QAction::PreferencesRole);
    connect(configure_action, &QAction::triggered, this, &MainWindow::configure_requested);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* about_action = help->addAction(tr("&About Beebium"));
    about_action->setMenuRole(QAction::AboutRole);
    connect(about_action, &QAction::triggered, this, [this] {
        const auto text = [](std::string_view view) {
            return QString::fromLatin1(view.data(), static_cast<qsizetype>(view.size()));
        };
        QMessageBox::about(this, tr("About Beebium"),
                           tr("<p><b>Beebium</b> %1</p><p>A BBC Micro emulator.</p>"
                              "<p>Protocol %2</p>")
                               .arg(text(client_version()), text(client_protocol_fingerprint())));
    });
}

MainWindow::~MainWindow() {
    // The keyboard's sender belongs to the session, which as the elder child
    // is destroyed first; detach while both still exist.
    display_->keyboard().detach();
    session_->disconnect_from_server();
}

void MainWindow::connect_to(const QString& endpoint) {
    display_->keyboard().detach();
    session_->connect_to(endpoint);
}

void MainWindow::show_verdict() {
    const QString machine =
        session_->machine_name().isEmpty() ? session_->endpoint() : session_->machine_name();
    overlay_->show_content(overlay_content(recovery_->verdict(), machine, recovery_->attempt()));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == display_ && event->type() == QEvent::Resize) {
        overlay_->setGeometry(display_->rect());
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Recovery first, so nothing re-dials during teardown (4.4).
    recovery_->window_closing();
    // Closing the window is dropping its connection (lifecycle-management.md
    // section 4). Held keys are released first, and the key queue drains
    // before the streams are cancelled, so nothing is left down on the machine.
    display_->keyboard().detach();
    session_->disconnect_from_server();
    event->accept();
}

}  // namespace beebium::linux_client
