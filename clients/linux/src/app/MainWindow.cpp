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

#include "app/ClientSettings.hpp"
#include "app/CloseDecision.hpp"
#include "app/ConnectionOverlay.hpp"
#include "app/ConnectionRecovery.hpp"
#include "app/DisplayFrame.hpp"
#include "app/DisplayWidget.hpp"
#include "app/ErrorPresentation.hpp"
#include "app/HomeWindow.hpp"
#include "app/ImmersiveExit.hpp"
#include "app/MachineSession.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
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
    // The display inside a frame that shows when it has keyboard focus (7.1).
    frame_ = new DisplayFrame(display_, this);
    setCentralWidget(frame_);
    immersive_exit_ = new ImmersiveExit(display_);
    connect(immersive_exit_, &ImmersiveExit::exit_requested, this,
            [this] { set_presentation(Presentation::windowed); });
    resize(1280, 1024 + menuBar()->sizeHint().height());

    connect(session_, &MachineSession::frame_ready, display_, &DisplayWidget::frame_ready);
    connect(session_, &MachineSession::keyboard_ready, this, [this] {
        KeySender* sender = session_->key_sender();
        display_->keyboard().attach(session_->key_table(),
                                    [sender](const std::vector<WireEvent>& events) {
                                        sender->send(events);
                                    });
        display_->keyboard_changed();
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
                // Immersive is only meaningful while live: the window
                // returns to its layout, where the overlay explains (6.4).
                update_immersive_enabled();
                if (!is_live(verdict)) {
                    set_presentation(after_connection_lost(presentation_));
                    // Nothing typed reaches a machine that is not there, and
                    // nothing stays held for when it returns.
                    display_->keyboard().detach();
                    display_->keyboard_changed();
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
    // The unsaved-changes mark (5.3), beside rather than in the title, which
    // is exactly the machine's name (5.7).
    unsaved_label_ = new QLabel(tr("Unsaved changes"), this);
    unsaved_label_->setToolTip(tr("This machine has written to its discs since it was launched; "
                                  "save it to keep them."));
    unsaved_label_->hide();
    statusBar()->addPermanentWidget(unsaved_label_);
    connect(session_, &MachineSession::unsaved_changes_changed, unsaved_label_,
            [this](bool unsaved) {
                unsaved_label_->setVisible(unsaved && !session_->machine_saved());
            });

    // BREAK as a control, not only a menu item (5.5): held while pressed,
    // like the key, with its shortcut said in the tooltip.
    auto* break_button = new QToolButton(this);
    break_button->setObjectName(QStringLiteral("breakButton"));
    break_button->setText(tr("Break"));
    break_button->setToolTip(tr("Break (Alt+F12): reset the machine; hold to keep it in reset"));
    break_button->setFocusPolicy(Qt::TabFocus);
    break_button->setEnabled(false);
    connect(break_button, &QToolButton::pressed, this, [this] { display_->keyboard().press_break(); });
    connect(break_button, &QToolButton::released, this, [this] { display_->keyboard().release_break(); });
    statusBar()->addPermanentWidget(break_button);

    // Where keys go (7.1), and how to change it.
    auto* keys_label = new QLabel(this);
    keys_label->setObjectName(QStringLiteral("keysLabel"));
    keys_label->setToolTip(tr("Click the display to type into the machine; Ctrl+Tab leaves it. "
                              "While the display has the keys, every key the keyboard mapping "
                              "uses goes to the machine, Ctrl shortcuts included; F11, Alt with "
                              "a menu letter and the mouse still reach Beebium."));
    statusBar()->addPermanentWidget(keys_label);
    const auto show_keys = [keys_label, break_button, this](KeysGoTo destination) {
        keys_label->setText(destination == KeysGoTo::machine ? tr("Keys: machine")
                                                             : tr("Keys: Beebium"));
        break_button->setEnabled(display_->keyboard().attached());
    };
    show_keys(display_->keys_go_to());
    connect(display_, &DisplayWidget::keys_go_to_changed, this, show_keys);
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
    // The application's own commands are the home window's (13.4).
    QAction* home_action = file->addAction(tr("Show &Beebium"));
    home_action->setToolTip(tr("Bring the Beebium window forward, to start, open or connect to "
                               "another machine"));
    home_action->setShortcut(QKeySequence(tr("Ctrl+Shift+H")));
    connect(home_action, &QAction::triggered, this, &MainWindow::home_requested);
    file->addSeparator();
    QAction* save_action = file->addAction(tr("&Save"));
    save_action->setShortcut(QKeySequence::Save);
    connect(save_action, &QAction::triggered, this, [this] { save(); });
    QAction* save_as_action = file->addAction(tr("Save &As..."));
    save_as_action->setShortcut(QKeySequence::SaveAs);
    connect(save_as_action, &QAction::triggered, this, [this] { save_as(); });
    QAction* rename_action = file->addAction(tr("&Rename Machine..."));
    connect(rename_action, &QAction::triggered, this, &MainWindow::rename);
    for (QAction* action : {save_action, save_as_action, rename_action}) {
        action->setEnabled(false);
    }
    // A transient machine is not a document on disk (5.3): its first save is
    // Save, so Save As waits until it has been saved.
    const auto enable_saving = [this, save_action, save_as_action, rename_action] {
        save_action->setEnabled(true);
        rename_action->setEnabled(true);
        save_as_action->setEnabled(session_->machine_saved());
    };
    connect(session_, &MachineSession::connected_to_server, this, enable_saving);
    connect(session_, &MachineSession::save_finished, this, enable_saving);
    connect(session_, &MachineSession::system_info_refreshed, this, enable_saving);
    file->addSeparator();
    QAction* close_action = file->addAction(tr("C&lose Window"));
    close_action->setShortcut(QKeySequence::Close);
    connect(close_action, &QAction::triggered, this, &QWidget::close);
    QAction* quit_action = file->addAction(tr("&Quit"));
    quit_action->setShortcut(QKeySequence::Quit);
    quit_action->setMenuRole(QAction::QuitRole);
    connect(quit_action, &QAction::triggered, this, &MainWindow::quit_requested);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    status_bar_action_ = view->addAction(tr("Show &Status Bar"));
    status_bar_action_->setCheckable(true);
    // Ctrl+/ as the macOS client's Command-/.
    status_bar_action_->setShortcut(QKeySequence(tr("Ctrl+/")));
    const bool show_status_bar =
        settings_ ? settings_->store().value(QStringLiteral("view/statusBar"), true).toBool() : true;
    status_bar_action_->setChecked(show_status_bar);
    connect(status_bar_action_, &QAction::toggled, this, [this](bool shown) {
        if (settings_) {
            settings_->store().setValue(QStringLiteral("view/statusBar"), shown);
        }
        apply_presentation();
    });
    view->addSeparator();
    // Immersive as the macOS client has it, Ctrl+Shift+F for its
    // Command-Shift-F; Shift+F11 too, since F11 with any modifier always
    // reaches Beebium, the display's keys or not (7.1).
    immersive_action_ = view->addAction(tr("Enter &Immersive Mode"));
    immersive_action_->setShortcuts({QKeySequence(tr("Ctrl+Shift+F")),
                                     QKeySequence(Qt::SHIFT | Qt::Key_F11)});
    update_immersive_enabled();
    connect(session_, &MachineSession::connected_to_server, this,
            &MainWindow::update_immersive_enabled);
    connect(immersive_action_, &QAction::triggered, this,
            [this] { set_presentation(after_immersive_command(presentation_)); });
    full_screen_action_ = view->addAction(tr("&Full Screen"));
    full_screen_action_->setCheckable(true);
    full_screen_action_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(full_screen_action_, &QAction::triggered, this,
            [this] { set_presentation(after_full_screen_key(presentation_)); });
    // Window-wide, and on the window as well as in the menus: the menu bar
    // is hidden while immersive, and F11 must still lead out (6.4).
    for (QAction* action : {status_bar_action_, immersive_action_, full_screen_action_}) {
        action->setShortcutContext(Qt::WindowShortcut);
        addAction(action);
    }
    apply_presentation();

    QMenu* machine = menuBar()->addMenu(tr("&Machine"));
    QAction* run_action = machine->addAction(tr("&Run"));
    run_action->setToolTip(tr("Start a machine that is waiting to run"));
    run_action->setEnabled(false);
    connect(run_action, &QAction::triggered, session_, &MachineSession::run);
    connect(session_, &MachineSession::running_changed, run_action,
            [run_action](bool running) { run_action->setEnabled(!running); });
    QAction* break_action = machine->addAction(tr("&Break"));
    // Alt+F12 is BREAK in the built-in mappings: while the display has the
    // keys it reaches the machine as a key; elsewhere this shortcut taps it.
    break_action->setShortcut(QKeySequence(tr("Alt+F12")));
    connect(break_action, &QAction::triggered, this, [this] {
        display_->keyboard().press_break();
        QTimer::singleShot(100, this, [this] { display_->keyboard().release_break(); });
    });
    machine->addSeparator();
    QAction* battery_action = machine->addAction(tr("&Forget Battery-Backed State..."));
    connect(battery_action, &QAction::triggered, this, [this] {
        const auto answer = QMessageBox::question(
            this, tr("Forget Battery-Backed State"),
            tr("Forget what %1 keeps while it is off, such as its clock and settings? "
               "It takes effect the next time the machine starts.")
                .arg(session_->machine_name()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(session_, &MachineSession::battery_reset, this,
                              [this, connection](const CallOutcome& outcome, const QStringList& units) {
                                  disconnect(*connection);
                                  if (!outcome.ok) {
                                      ServerMessageDialog dialog(
                                          tr("Could Not Forget"),
                                          tr("The battery-backed state could not be forgotten."),
                                          outcome.message, this);
                                      dialog.exec();
                                      return;
                                  }
                                  QMessageBox::information(
                                      this, tr("Forget Battery-Backed State"),
                                      units.isEmpty()
                                          ? tr("This machine keeps no battery-backed state.")
                                          : tr("At the next start this machine will forget: %1.")
                                                .arg(units.join(QStringLiteral(", "))));
                              });
        session_->reset_battery();
    });
    for (QAction* action : {break_action, battery_action}) {
        action->setEnabled(false);
        connect(session_, &MachineSession::connected_to_server, action,
                [action] { action->setEnabled(true); });
    }

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* about_action = help->addAction(tr("&About Beebium"));
    about_action->setMenuRole(QAction::AboutRole);
    connect(about_action, &QAction::triggered, this, [this] { show_about_beebium(this); });
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

void MainWindow::save(std::function<void(bool saved)> then) {
    if (!session_->machine_saved()) {
        // A transient machine has nowhere to save in place (5.4): its first
        // save chooses where.
        save_as(std::move(then));
        return;
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::save_finished, this,
                          [this, then, connection](const CallOutcome& outcome) {
                              disconnect(*connection);
                              if (!outcome.ok) {
                                  ServerMessageDialog dialog(
                                      tr("Could Not Save"),
                                      tr("Beebium could not save %1.").arg(session_->machine_name()),
                                      outcome.message, this);
                                  dialog.exec();
                              }
                              if (then) {
                                  then(outcome.ok);
                              }
                          });
    session_->save(QString(), false);
}

void MainWindow::save_as(std::function<void(bool saved)> then) {
    const QString suggested =
        QDir(QDir::homePath()).filePath(machine_dirpath_for(session_->machine_name()));
    const QString chosen = QFileDialog::getSaveFileName(
        this, tr("Save Machine As"), suggested, tr("Beebium machines (*.beebium-machine)"),
        nullptr, QFileDialog::DontConfirmOverwrite);
    if (chosen.isEmpty()) {
        if (then) {
            then(false);
        }
        return;
    }
    const QString destination = machine_dirpath_for(chosen);
    bool replace = false;
    if (QFileInfo::exists(destination)) {
        // Replaced only on the user's say-so; the server removes the old one
        // once the new one is whole (5.4).
        const auto answer = QMessageBox::question(
            this, tr("Replace Machine"),
            tr("%1 already exists. Replace it?").arg(QFileInfo(destination).fileName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            if (then) {
                then(false);
            }
            return;
        }
        replace = true;
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::save_finished, this,
                          [this, then, connection, destination](const CallOutcome& outcome) {
                              disconnect(*connection);
                              if (outcome.ok) {
                                  unsaved_label_->hide();
                              } else {
                                  ServerMessageDialog dialog(
                                      tr("Could Not Save"),
                                      tr("Beebium could not save the machine to %1.").arg(destination),
                                      outcome.message, this);
                                  dialog.exec();
                              }
                              if (then) {
                                  then(outcome.ok);
                              }
                          });
    session_->save(destination, replace);
}

void MainWindow::rename() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Machine"), tr("Name:"),
                                               QLineEdit::Normal, session_->machine_name(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == session_->machine_name()) {
        return;
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::name_set, this,
                          [this, connection, name](const CallOutcome& outcome) {
                              disconnect(*connection);
                              if (!outcome.ok) {
                                  ServerMessageDialog dialog(
                                      tr("Could Not Rename"),
                                      tr("Beebium could not rename the machine to %1.").arg(name),
                                      outcome.message, this);
                                  dialog.exec();
                              }
                          });
    session_->set_name(name);
}

void MainWindow::set_presentation(Presentation presentation) {
    if (presentation == presentation_) {
        return;
    }
    presentation_ = presentation;
    apply_presentation();
}

void MainWindow::update_immersive_enabled() {
    if (immersive_action_) {
        immersive_action_->setEnabled(session_->connected() && is_live(recovery_->verdict()));
    }
}

void MainWindow::apply_presentation() {
    const Chrome chrome = chrome_for(presentation_);
    const bool immersive = presentation_ == Presentation::immersive;
    menuBar()->setVisible(chrome.menu_bar);
    statusBar()->setVisible(chrome.status_bar && status_bar_action_->isChecked());
    frame_->set_bare(immersive);
    immersive_exit_->set_active(immersive);
    immersive_action_->setText(immersive ? tr("Exit &Immersive Mode") : tr("Enter &Immersive Mode"));
    full_screen_action_->setChecked(presentation_ == Presentation::full_screen);
    if (chrome.os_full_screen && !isFullScreen()) {
        showFullScreen();
    } else if (!chrome.os_full_screen && isFullScreen()) {
        showNormal();
    }
    if (immersive) {
        // The keys stay the machine's: entering never takes them away.
        display_->setFocus();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    // The desktop left full screen by its own means (a compositor binding):
    // follow it, as the macOS client does its window's (6.4).
    if (event->type() == QEvent::WindowStateChange && !isFullScreen() &&
        presentation_ != Presentation::windowed && isVisible()) {
        presentation_ = Presentation::windowed;
        apply_presentation();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (close_phase_ == ClosePhase::closed) {
        event->accept();
        return;
    }
    // Every close, the window manager's included, goes through the decision
    // of 4.5, which may need the server's answer first.
    event->ignore();
    if (close_phase_ == ClosePhase::open) {
        close_phase_ = ClosePhase::closing;
        // From the event loop, never inside this event: the close that ends
        // the sequence must not nest in the one being refused here, or Qt
        // takes the outer refusal as final and the window never goes.
        QMetaObject::invokeMethod(this, &MainWindow::begin_close, Qt::QueuedConnection);
    }
}

void MainWindow::begin_close() {
    const auto server = session_->launched_server();
    if (!server) {
        // Not ours: the server is left as it is.
        finish_close();
        return;
    }
    if (!session_->connected() || !is_live(recovery_->verdict())) {
        // Ours, but not there to ask. If its process is still alive (hung
        // rather than gone), nobody else would ever stop it and it would keep
        // its machine's lock and port, so the client that launched it
        // signals it (4.5).
        recovery_->window_closing();
        display_->keyboard().detach();
        session_->disconnect_from_server();
        stop_process_then_close(server->pid, false);
        return;
    }
    // How many clients watch it: ours alone, or others too. Without an
    // answer the server is left running rather than risk another client's.
    auto answered = std::make_shared<bool>(false);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::system_info_refreshed, this,
                          [this, answered, connection](int client_count) {
                              disconnect(*connection);
                              *answered = true;
                              decide_close(client_count);
                          });
    QTimer::singleShot(3000, this, [this, answered, connection] {
        if (!*answered) {
            disconnect(*connection);
            finish_close();
        }
    });
    session_->refresh_system_info();
}

void MainWindow::decide_close(int client_count) {
    const CloseAction action = quitting_ ? quit_action(true, client_count)
                                         : close_action(true, client_count);
    if (action == CloseAction::disconnect) {
        finish_close();
        return;
    }
    if (action == CloseAction::ask) {
        switch (prompts_.shared(this, session_->machine_name())) {
        case ClosePrompts::Shared::leave_running:
            finish_close();
            break;
        case ClosePrompts::Shared::shut_down:
            shut_down_server();
            break;
        case ClosePrompts::Shared::cancel:
            cancel_close();
            break;
        }
        return;
    }
    // Shutting it down. A transient machine's written media go with it
    // unless saved first (4.5).
    if (!session_->machine_saved() && session_->template_media_written()) {
        switch (prompts_.unsaved(this, session_->machine_name())) {
        case ClosePrompts::Unsaved::cancel:
            cancel_close();
            return;
        case ClosePrompts::Unsaved::save:
            save_as([this](bool saved) {
                if (saved) {
                    shut_down_server();
                } else {
                    // Cancelling the save cancels the close.
                    cancel_close();
                }
            });
            return;
        case ClosePrompts::Unsaved::discard:
            break;
        }
    }
    shut_down_server();
}

void MainWindow::shut_down_server() {
    recovery_->window_closing();
    display_->keyboard().detach();
    const auto server = session_->launched_server();
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::shutdown_answered, this,
                          [this, connection, server](const CallOutcome& outcome) {
                              disconnect(*connection);
                              session_->disconnect_from_server();
                              if (!server) {
                                  finish_close();
                                  return;
                              }
                              // Asked and accepted: the server is on its way
                              // down. Refused or unanswered: ours to stop.
                              stop_process_then_close(server->pid, outcome.ok);
                          });
    session_->request_shutdown();
}

void MainWindow::stop_process_then_close(qint64 pid, bool already_asked) {
    if (!process_alive(pid)) {
        finish_close();
        return;
    }
    if (!already_asked) {
        // The signal shuts the server down gracefully too.
        terminate_process(pid);
    }
    // Let it finish flushing its drives, without holding up the GUI.
    auto* poll = new QTimer(this);
    auto waited = std::make_shared<QElapsedTimer>();
    waited->start();
    connect(poll, &QTimer::timeout, this, [this, poll, waited, pid, already_asked] {
        if (process_alive(pid) && waited->elapsed() < 6000) {
            return;
        }
        if (process_alive(pid) && already_asked) {
            // It accepted the shutdown but has not gone: ask the other way.
            terminate_process(pid);
        }
        poll->deleteLater();
        finish_close();
    });
    poll->start(100);
}

void MainWindow::cancel_close() {
    close_phase_ = ClosePhase::open;
    quitting_ = false;
}

ClosePrompts ClosePrompts::message_boxes() {
    ClosePrompts prompts;
    prompts.shared = [](QWidget* window, const QString& machine) {
        QMessageBox box(QMessageBox::Question, QObject::tr("Close Machine"),
                        QObject::tr("%1 is also in use by another client.").arg(machine),
                        QMessageBox::NoButton, window);
        QPushButton* shut_down = box.addButton(QObject::tr("Shut Down"), QMessageBox::DestructiveRole);
        QPushButton* leave = box.addButton(QObject::tr("Leave Running"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(leave);
        box.exec();
        if (box.clickedButton() == leave) {
            return Shared::leave_running;
        }
        return box.clickedButton() == shut_down ? Shared::shut_down : Shared::cancel;
    };
    prompts.unsaved = [](QWidget* window, const QString& machine) {
        const auto answer = QMessageBox::question(
            window, QObject::tr("Save Machine"),
            QObject::tr("%1 has written to its discs. Save it before closing?").arg(machine),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Save) {
            return Unsaved::save;
        }
        return answer == QMessageBox::Discard ? Unsaved::discard : Unsaved::cancel;
    };
    return prompts;
}

void MainWindow::finish_close() {
    // Recovery first, so nothing re-dials during teardown (4.4). Held keys
    // are released and the key queue drains before the streams go, so
    // nothing is left down on the machine.
    recovery_->window_closing();
    display_->keyboard().detach();
    session_->disconnect_from_server();
    close_phase_ = ClosePhase::closed;
    close();
}

}  // namespace beebium::linux_client
