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
#include "app/KeyboardSidebar.hpp"
#include "app/PictureDrop.hpp"
#include "app/ProcessorSidebar.hpp"
#include "app/IndicatorStrip.hpp"
#include "app/Shortcuts.hpp"
#include "app/Sidebar.hpp"
#include "app/SoundSidebar.hpp"
#include "app/StorageSidebar.hpp"
#include "app/VideoSidebar.hpp"
#include "app/WindowGeometry.hpp"
#include "app/MachineMove.hpp"
#include "app/MachineSession.hpp"

#include <QAction>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QEvent>
#include <QScreen>
#include <QStatusBar>

#include <cstdio>

namespace beebium::linux_client {

namespace {

// Two links of a chain at a slant, joined or, unlinked, pulled apart; drawn
// here since icon themes have no broken link (the macOS client draws its
// own too).
QPixmap link_glyph(bool linked, const QColor& colour, int size, qreal device_ratio) {
    QPixmap pixmap(QSize(size, size) * device_ratio);
    pixmap.setDevicePixelRatio(device_ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(colour, size / 9.0, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.translate(size / 2.0, size / 2.0);
    painter.rotate(-45);
    const qreal length = size * 0.42;
    const qreal width = size * 0.26;
    const qreal apart = linked ? length * 0.32 : length * 0.62;
    for (const qreal centre : {-apart, apart}) {
        painter.drawRoundedRect(QRectF(centre - length / 2, -width / 2, length, width), width / 2,
                                width / 2);
    }
    return pixmap;
}

}  // namespace

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
    // A new window starts from the video defaults (6.6); later changes to
    // them leave it be.
    if (settings_) {
        display_->set_video_settings(settings_->video_defaults());
    }
    show_title(QString());
    setAttribute(Qt::WA_DeleteOnClose);

    display_->setObjectName(QStringLiteral("display"));
    display_->setApi(graphics_api);
    // The display inside a frame that shows when it has keyboard focus (7.1).
    frame_ = new DisplayFrame(display_, this);
    frame_->installEventFilter(this);
    // A disc image dropped onto the picture goes into drive 0, or the only
    // empty drive (9.2).
    new PictureDropTarget(
        display_, [this] { return session_->discs().drives(); },
        [this] { return session_->server_shares_filesystem(); },
        [this](int drive, const QString& path) { session_->discs().insert(drive, path); });
    // The sidebar beside the picture (13.1), the macOS client's 280 pixels.
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setChildrenCollapsible(false);
    sidebar_ = new Sidebar(splitter_);
    sidebar_->add_mode(SidebarMode::storage,
                       new StorageSidebar(session_->discs(), settings_,
                                          [this] { return session_->server_shares_filesystem(); },
                                          sidebar_));
    auto* video_mode = new VideoSidebar(sidebar_);
    video_mode->set_settings(display_->video_settings());
    connect(video_mode, &VideoSidebar::changed, display_, &DisplayWidget::set_video_settings);
    sidebar_->add_mode(SidebarMode::video, video_mode);
    sidebar_->add_mode(SidebarMode::sound, new SoundSidebar(session_->audio(), sidebar_));
    // The keyboard mapping, as chosen last time (7.4).
    auto* keyboard_mode = keyboard_mode_ = new KeyboardSidebar(user_mappings_dirpath(), sidebar_);
    if (settings_) {
        const QString chosen = settings_->store().value(QStringLiteral("keyboard/mapping")).toString();
        for (const MappingChoice& choice : keyboard_mode->choices()) {
            if (!chosen.isEmpty() && choice.id == chosen && choice.mapping) {
                display_->keyboard().set_mapping(*choice.mapping);
            }
        }
    }
    keyboard_mode->set_active(QString::fromStdString(display_->keyboard().mapping().id));
    keyboard_mode->set_caps_lock_sync(display_->keyboard().caps_lock_sync_enabled());
    // Nothing before the mapping a window opens with.
    mapping_history_ = MappingHistory(QString::fromStdString(display_->keyboard().mapping().id));
    connect(keyboard_mode, &KeyboardSidebar::mapping_chosen, this, &MainWindow::activate_mapping);
    connect(keyboard_mode, &KeyboardSidebar::caps_lock_sync_toggled, this,
            [this](bool on) { display_->keyboard().set_caps_lock_sync_override(on); });
    sidebar_->add_mode(SidebarMode::keyboard, keyboard_mode);
    sidebar_->add_mode(SidebarMode::processor,
                       new ProcessorSidebar(session_->speed(), sidebar_));
    // Storage first, as the macOS client opens.
    sidebar_->set_mode(SidebarMode::storage);
    if (settings_) {
        const auto mode = static_cast<SidebarMode>(
            settings_->store()
                .value(QStringLiteral("view/sidebarMode"), static_cast<int>(SidebarMode::storage))
                .toInt());
        sidebar_->set_mode(mode);
    }
    connect(sidebar_, &Sidebar::mode_changed, this, [this](SidebarMode mode) {
        if (settings_) {
            settings_->store().setValue(QStringLiteral("view/sidebarMode"), static_cast<int>(mode));
        }
    });
    splitter_->addWidget(sidebar_);
    splitter_->addWidget(frame_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({Sidebar::WIDTH, 1280});
    setCentralWidget(splitter_);
    immersive_exit_ = new ImmersiveExit(display_);
    connect(immersive_exit_, &ImmersiveExit::exit_requested, this,
            [this] { set_presentation(Presentation::windowed); });
    connect(immersive_exit_, &ImmersiveExit::sidebar_toggled, this, [this] {
        immersive_sidebar_ = !immersive_sidebar_;
        apply_presentation();
    });
    connect(immersive_exit_, &ImmersiveExit::status_bar_toggled, this, [this] {
        immersive_status_bar_ = !immersive_status_bar_;
        apply_presentation();
    });
    // The picture at twice the BBC's size beside the sidebar where the
    // screen has room, else shrunk at 5:4 to fit with the title bar and the
    // panel (a 1080p screen does not hold 1024 lines and the chrome).
    if (const QScreen* on = screen()) {
        const int chrome = menuBar()->sizeHint().height() + statusBar()->sizeHint().height() +
                           2 * DisplayFrame::MARGIN;
        const int side = Sidebar::WIDTH + splitter_->handleWidth() + 2 * DisplayFrame::MARGIN;
        const QSize size = machine_window_size(on->geometry(), on->availableGeometry(), chrome, side);
        resize(size);
        std::fprintf(stderr, "beebium: window %dx%d on a %dx%d screen, %dx%d of it available\n",
                     size.width(), size.height(), on->geometry().width(), on->geometry().height(),
                     on->availableGeometry().width(), on->availableGeometry().height());
    }

    connect(session_, &MachineSession::frame_ready, display_, &DisplayWidget::frame_ready);
    // A key or Break from the user, for a machine whose Run is held for its
    // sound, runs it now; the client's own lock syncs and releases do not.
    display_->keyboard().on_user_press([this] { session_->release_run_hold(); });
    connect(session_, &MachineSession::keyboard_ready, this, [this] {
        KeySender* sender = session_->key_sender();
        display_->keyboard().attach(session_->key_table(),
                                    [sender](const std::vector<WireEvent>& events) {
                                        sender->send(events);
                                    });
        display_->keyboard_changed();
        display_->setFocus();
    });
    // The machine owns its lock latches and re-inits them on every reset;
    // the client reads them exactly at each moment they may have changed
    // (7.3): the guest's readiness after a boot or reset, from the typing
    // stream rather than any settle time, and a lock LED changing.
    connect(&session_->typing(), &SessionTyping::guest_ready_rose, this,
            [this] { display_->keyboard().sync_caps_lock(); });
    connect(&session_->indicators(), &SessionIndicators::lock_led_changed, this,
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
                sidebar_->set_verdict(verdict);
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
    // The status bar as the macOS client's (13.3): the machine's LEDs on
    // the left; on the right a paste under way, the keyboard mapping and the
    // machine's model. Everything it shows is in one strip, which immersive
    // mode can lift over the picture whole (6.4).
    status_strip_ = new QWidget(this);
    status_strip_->setObjectName(QStringLiteral("statusStrip"));
    auto* strip_layout = new QHBoxLayout(status_strip_);
    strip_layout->setContentsMargins(4, 0, 4, 0);
    strip_layout->setSpacing(8);
    statusBar()->addWidget(status_strip_, 1);
    // The LEDs, in the strip so they ride the immersive overlay too (10).
    indicator_strip_ = new IndicatorStrip(session_->indicators(), status_strip_);
    strip_layout->addWidget(indicator_strip_);
    connect(indicator_strip_, &IndicatorStrip::key_tapped, this, &MainWindow::tap_named_key);
    strip_layout->addStretch(1);

    // A paste under way, and its Cancel (7.5): Escape is the machine's while
    // the display has the keys, so the control is the way to stop it.
    auto* pasting_label = new QLabel(tr("Pasting..."), status_strip_);
    pasting_label->setObjectName(QStringLiteral("pastingLabel"));
    auto* cancel_paste = new QToolButton(status_strip_);
    cancel_paste->setObjectName(QStringLiteral("cancelPaste"));
    cancel_paste->setText(tr("Cancel"));
    cancel_paste->setToolTip(tr("Stop typing the rest; what has been typed stays"));
    cancel_paste->setFocusPolicy(Qt::NoFocus);
    cancel_paste->setAutoRaise(true);
    pasting_label->hide();
    cancel_paste->hide();
    strip_layout->addWidget(pasting_label);
    strip_layout->addWidget(cancel_paste);
    connect(cancel_paste, &QToolButton::clicked, &session_->typing(), &SessionTyping::cancel_paste);
    connect(&session_->typing(), &SessionTyping::pasting_changed, this,
            [pasting_label, cancel_paste](bool pasting) {
                pasting_label->setVisible(pasting);
                cancel_paste->setVisible(pasting);
            });
    connect(&session_->typing(), &SessionTyping::paste_finished, this,
            [this](const PasteOutcome& outcome) {
                switch (outcome.result) {
                case PasteOutcome::Result::typed:
                case PasteOutcome::Result::cancelled:
                    break;
                case PasteOutcome::Result::cut_short_by_reset:
                    QMessageBox::information(
                        this, tr("Paste"),
                        tr("The machine was reset while pasting, so the last %n characters were "
                           "not typed.", nullptr, static_cast<int>(outcome.count)));
                    break;
                case PasteOutcome::Result::refused:
                case PasteOutcome::Result::failed: {
                    ServerMessageDialog dialog(tr("Could Not Paste"),
                                               tr("The text could not be typed into the machine."),
                                               outcome.message, this);
                    dialog.exec();
                    break;
                }
                }
            });

    // The keyboard mapping in use, after a small keyboard glyph, then the
    // machine's model as the server names it, both in the secondary colour.
    const int glyph_size = status_strip_->fontMetrics().height();
    // Linked to a machine this client launched (as the macOS client): the
    // window's close shuts it down. Unlinking asks for it to be left running
    // when the window closes instead; nothing happens until then.
    link_button_ = new QToolButton(status_strip_);
    link_button_->setObjectName(QStringLiteral("linkControl"));
    link_button_->setAutoRaise(true);
    link_button_->setFocusPolicy(Qt::NoFocus);
    link_button_->hide();
    strip_layout->addWidget(link_button_);
    connect(link_button_, &QToolButton::clicked, this, [this] {
        unlink_requested_ = !unlink_requested_;
        update_link_control();
    });
    connect(session_, &MachineSession::connected_to_server, this, [this] {
        link_button_->setVisible(session_->launched_server().has_value());
        update_link_control();
    });

    auto* keyboard_glyph = new QLabel(status_strip_);
    keyboard_glyph->setPixmap(QIcon::fromTheme(QStringLiteral("input-keyboard"))
                                  .pixmap(glyph_size, glyph_size));
    mapping_label_ = new QLabel(status_strip_);
    mapping_label_->setObjectName(QStringLiteral("mappingName"));
    mapping_label_->setToolTip(tr("The keyboard mapping in use"));
    model_label_ = new QLabel(status_strip_);
    model_label_->setObjectName(QStringLiteral("modelName"));
    for (QLabel* label : {mapping_label_, model_label_}) {
        label->setForegroundRole(QPalette::PlaceholderText);
    }
    auto* mapping_group = new QHBoxLayout;
    mapping_group->setSpacing(3);
    mapping_group->addWidget(keyboard_glyph);
    mapping_group->addWidget(mapping_label_);
    strip_layout->addLayout(mapping_group);
    strip_layout->addWidget(model_label_);
    mapping_label_->setText(QString::fromStdString(display_->keyboard().mapping().name));
    connect(session_, &MachineSession::connected_to_server, this,
            [this] { model_label_->setText(session_->model_name()); });
    // The title is the machine's name, exactly (5.7).
    // The description is the sidebar's subtitle, not the title's (5.7).
    connect(session_, &MachineSession::identity_changed, this,
            [this](const QString& name, const QString& description) {
                show_title(name);
                sidebar_->set_description(description);
            });
    // Unsaved changes (5.3) are the platform's own marker in the title.
    connect(session_, &MachineSession::unsaved_changes_changed, this,
            &QWidget::setWindowModified);

    build_menus();
}

void MainWindow::show_title(const QString& name) {
    // A template: the platform puts its unsaved-changes marker at "[*]",
    // so one in the name itself is doubled to stay the name's.
    QString title = name.isEmpty() ? QStringLiteral("Beebium") : name;
    title.replace(QStringLiteral("[*]"), QStringLiteral("[*][*]"));
    setWindowTitle(title + QStringLiteral("[*]"));
}

void MainWindow::activate_mapping(const MappingChoice& choice) {
    if (!choice.mapping) {
        return;
    }
    display_->keyboard().set_mapping(*choice.mapping);
    mapping_history_.activate(choice.id);
    if (mapping_label_) {
        mapping_label_->setText(choice.name);
    }
    keyboard_mode_->set_active(choice.id);
    keyboard_mode_->set_caps_lock_sync(display_->keyboard().caps_lock_sync_enabled());
    if (settings_) {
        settings_->store().setValue(QStringLiteral("keyboard/mapping"), choice.id);
    }
    update_mapping_switch();
    std::fprintf(stderr, "beebium: keyboard mapping %s\n", qPrintable(choice.name));
}

void MainWindow::update_mapping_switch() {
    if (!switch_mapping_action_) {
        return;
    }
    keyboard_mode_->rescan();
    mapping_history_.keep_only(keyboard_mode_->choices());
    const QString target = mapping_history_.target();
    QString name;
    for (const MappingChoice& choice : keyboard_mode_->choices()) {
        if (choice.usable() && choice.id == target) {
            name = choice.name;
        }
    }
    switch_mapping_action_->setEnabled(!name.isEmpty());
    switch_mapping_action_->setText(name.isEmpty()
                                        ? tr("Switch to Previous Keyboard Mapping")
                                        : tr("Switch to %1 Keyboard Mapping").arg(name));
}

void MainWindow::switch_mapping() {
    // Read again first: the file may have gone since the title was set.
    update_mapping_switch();
    const QString target = mapping_history_.target();
    for (const MappingChoice& choice : keyboard_mode_->choices()) {
        if (choice.usable() && choice.id == target) {
            activate_mapping(choice);
            return;
        }
    }
}

void MainWindow::build_menus() {
    // Client specification 13.2, as a KDE-style menu bar. The BBC Micro has
    // Ctrl and Shift but no Alt or Meta, so every shortcut here carries one
    // of those and none is the machine's typing: each answers to a Meta form,
    // Meta playing the part of the macOS client's Command, and a Ctrl+Alt
    // form, ordered for the session (Shortcuts.hpp). F11 is always the
    // window's.
    // File, Edit, View, Window and Help, as the macOS client's (13.2); the
    // application's own commands are the home window's (13.4).
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* save_action = file->addAction(tr("&Save"));
    set_session_shortcuts(save_action, QKeySequence(QStringLiteral("Meta+S")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+S")));
    connect(save_action, &QAction::triggered, this, [this] { save(); });
    QAction* save_as_action = file->addAction(tr("Save &As..."));
    set_session_shortcuts(save_as_action, QKeySequence(QStringLiteral("Meta+Shift+S")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+Shift+S")));
    connect(save_as_action, &QAction::triggered, this, [this] { save_as(); });
    QAction* rename_action = file->addAction(tr("&Rename Machine..."));
    connect(rename_action, &QAction::triggered, this, &MainWindow::rename);
    // As the macOS client's document Move To: the machine keeps its name and
    // goes to another folder (5.4).
    QAction* move_action = file->addAction(tr("&Move To..."));
    connect(move_action, &QAction::triggered, this, &MainWindow::move_to);
    for (QAction* action : {save_action, save_as_action, rename_action, move_action}) {
        action->setEnabled(false);
    }
    // A transient machine is not a document on disk (5.3): its first save is
    // Save, so Save As and Move wait until it has been saved. A server on
    // another host is not saved from here at all for now (3.2, #263): its
    // folders cannot be chosen here. Rename needs no folder.
    const auto enable_saving = [this, save_action, save_as_action, rename_action, move_action] {
        const bool saved = session_->machine_saved();
        const bool local = session_->server_shares_filesystem();
        const auto explain = [local](QAction* action, const QString& reason) {
            action->setToolTip(local ? QString() : reason);
        };
        save_action->setEnabled(local);
        explain(save_action, tr("The server is on another host, so it cannot save the machine "
                                "from here"));
        rename_action->setEnabled(true);
        save_as_action->setEnabled(saved && local);
        explain(save_as_action, tr("The server is on another host, so it cannot save the "
                                   "machine from here"));
        move_action->setEnabled(saved && local);
        explain(move_action, tr("The server is on another host, so it cannot move the "
                                "machine to a folder here"));
    };
    connect(session_, &MachineSession::connected_to_server, this, enable_saving);
    connect(session_, &MachineSession::save_finished, this, enable_saving);
    connect(session_, &MachineSession::move_finished, this, enable_saving);
    connect(session_, &MachineSession::system_info_refreshed, this, enable_saving);
    file->addSeparator();
    QAction* close_action = file->addAction(tr("C&lose Window"));
    set_session_shortcuts(close_action, QKeySequence(QStringLiteral("Meta+W")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+W")));
    connect(close_action, &QAction::triggered, this, &QWidget::close);
    QAction* quit_action = file->addAction(tr("&Quit"));
    set_session_shortcuts(quit_action, QKeySequence(QStringLiteral("Meta+Q")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+Q")));
    quit_action->setMenuRole(QAction::QuitRole);
    connect(quit_action, &QAction::triggered, this, &MainWindow::quit_requested);

    // Edit > Paste types the clipboard into the machine (7.5). Ctrl+V is the
    // machine's (VDU 22), so Paste is Meta+V or Ctrl+Alt+V.
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QAction* paste_action = edit->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")),
                                            tr("&Paste"));
    set_session_shortcuts(paste_action, QKeySequence(QStringLiteral("Meta+V")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+V")));
    paste_action->setToolTip(tr("Type the clipboard's text into the machine"));
    paste_action->setEnabled(false);
    connect(paste_action, &QAction::triggered, this, &MainWindow::paste);
    connect(session_, &MachineSession::connected_to_server, paste_action,
            [paste_action] { paste_action->setEnabled(true); });
    // Not live, nothing to type into: the action goes with the keyboard.
    connect(recovery_, &ConnectionRecovery::verdict_changed, paste_action,
            [paste_action](ConnectionVerdict verdict) {
                if (!is_live(verdict)) {
                    paste_action->setEnabled(false);
                }
            });
    connect(&session_->typing(), &SessionTyping::pasting_changed, paste_action,
            [paste_action](bool pasting) { paste_action->setEnabled(!pasting); });
    // Last, under a separator, as the macOS client's Command-K: Logical and
    // back (7.4).
    edit->addSeparator();
    switch_mapping_action_ = edit->addAction(QString());
    set_session_shortcuts(switch_mapping_action_, QKeySequence(QStringLiteral("Meta+K")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+K")));
    connect(switch_mapping_action_, &QAction::triggered, this, &MainWindow::switch_mapping);
    // On the window too, for a game played immersive, its menu bar hidden.
    switch_mapping_action_->setShortcutContext(Qt::WindowShortcut);
    addAction(switch_mapping_action_);
    connect(edit, &QMenu::aboutToShow, this, &MainWindow::update_mapping_switch);
    update_mapping_switch();
    QMenu* view = menuBar()->addMenu(tr("&View"));
    status_bar_action_ = view->addAction(tr("Show &Status Bar"));
    status_bar_action_->setCheckable(true);
    // As the macOS client's Command-/.
    set_session_shortcuts(status_bar_action_, QKeySequence(QStringLiteral("Meta+/")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+/")));
    const bool show_status_bar =
        settings_ ? settings_->store().value(QStringLiteral("view/statusBar"), true).toBool() : true;
    status_bar_action_->setChecked(show_status_bar);
    connect(status_bar_action_, &QAction::toggled, this, [this](bool shown) {
        if (presentation_ == Presentation::immersive) {
            // Over the picture while immersive; the window's setting stays.
            const QSignalBlocker keep(status_bar_action_);
            status_bar_action_->setChecked(!shown);
            immersive_status_bar_ = !immersive_status_bar_;
        } else if (settings_) {
            settings_->store().setValue(QStringLiteral("view/statusBar"), shown);
        }
        apply_presentation();
    });
    // The sidebar and its modes, as the macOS client's View menu has them:
    // Command-digit is Meta+digit or Ctrl+Alt+digit, the mode's fixed
    // number. Control-Command-S is Meta+Alt+S; its Ctrl+Alt form cannot be
    // Ctrl+Alt+S, which is Save's, so it is Ctrl+Alt+0, the sidebar itself
    // beside its numbered modes.
    sidebar_action_ = view->addAction(tr("Show S&idebar"));
    sidebar_action_->setCheckable(true);
    set_session_shortcuts(sidebar_action_, QKeySequence(QStringLiteral("Meta+Alt+S")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+0")));
    sidebar_action_->setChecked(
        settings_ ? settings_->store().value(QStringLiteral("view/sidebar"), true).toBool() : true);
    connect(sidebar_action_, &QAction::toggled, this, [this](bool shown) {
        if (presentation_ == Presentation::immersive) {
            const QSignalBlocker keep(sidebar_action_);
            sidebar_action_->setChecked(!shown);
            immersive_sidebar_ = !immersive_sidebar_;
        } else if (settings_) {
            settings_->store().setValue(QStringLiteral("view/sidebar"), shown);
        }
        apply_presentation();
    });
    std::vector<QAction*> window_actions{status_bar_action_, sidebar_action_};
    for (SidebarMode mode : offered_sidebar_modes()) {
        QAction* mode_action = view->addAction(sidebar_mode_icon(mode), sidebar_mode_label(mode));
        const int number = static_cast<int>(mode);
        set_session_shortcuts(mode_action, QKeySequence(QStringLiteral("Meta+%1").arg(number)),
                              QKeySequence(QStringLiteral("Ctrl+Alt+%1").arg(number)));
        // Shows that mode; pressed again with it showing, hides the sidebar.
        connect(mode_action, &QAction::triggered, this, [this, mode] {
            const bool immersive = presentation_ == Presentation::immersive;
            bool& shown_flag = immersive_sidebar_;
            const bool shown = immersive ? shown_flag : sidebar_action_->isChecked();
            const bool hide = shown && sidebar_->mode() == mode;
            sidebar_->set_mode(mode);
            if (immersive) {
                shown_flag = !hide;
            } else {
                const QSignalBlocker quiet(sidebar_action_);
                sidebar_action_->setChecked(!hide);
                if (settings_) {
                    settings_->store().setValue(QStringLiteral("view/sidebar"), !hide);
                }
            }
            apply_presentation();
        });
        window_actions.push_back(mode_action);
    }
    view->addSeparator();
    // Immersive as the macOS client has it, Meta+Shift+F for its
    // Command-Shift-F.
    immersive_action_ = view->addAction(tr("Enter &Immersive Mode"));
    set_session_shortcuts(immersive_action_, QKeySequence(QStringLiteral("Meta+Shift+F")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+F")));
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
    window_actions.push_back(immersive_action_);
    window_actions.push_back(full_screen_action_);
    for (QAction* action : window_actions) {
        action->setShortcutContext(Qt::WindowShortcut);
        addAction(action);
    }
    apply_presentation();

    // Window: Minimize, and Show Beebium, which brings the home window
    // forward to start, open or connect to another machine.
    QMenu* window = menuBar()->addMenu(tr("&Window"));
    QAction* minimize_action = window->addAction(tr("&Minimize"));
    set_session_shortcuts(minimize_action, QKeySequence(QStringLiteral("Meta+M")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+M")));
    connect(minimize_action, &QAction::triggered, this, &QWidget::showMinimized);
    window->addSeparator();
    QAction* home_action = window->addAction(tr("Show &Beebium"));
    home_action->setToolTip(tr("Bring the Beebium window forward, to start, open or connect to "
                               "another machine"));
    set_session_shortcuts(home_action, QKeySequence(QStringLiteral("Meta+Shift+H")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+H")));
    connect(home_action, &QAction::triggered, this, &MainWindow::home_requested);
    // On the window too: the menu bar is hidden while immersive (6.4).
    home_action->setShortcutContext(Qt::WindowShortcut);
    addAction(home_action);

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
    if (watched == frame_ && event->type() == QEvent::Resize) {
        place_overlays();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::save(std::function<void(bool saved)> then) {
    if (!session_->server_shares_filesystem()) {
        // Never saved from here (3.2, #263); the commands are disabled.
        if (then) {
            then(false);
        }
        return;
    }
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
    if (!session_->server_shares_filesystem()) {
        if (then) {
            then(false);
        }
        return;
    }
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
    // Asked of this host's filesystem: Save As is offered only when the
    // server shares it (3.2), so the server sees the same folder.
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
                              if (!outcome.ok) {
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

void MainWindow::move_to() {
    const QString name = session_->machine_name();
    const QString current_dirpath = session_->machine_dirpath();
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Move %1 To").arg(name), QFileInfo(current_dirpath).path());
    if (folder.isEmpty()) {
        return;
    }
    const QString destination = move_destination(folder, name);
    if (QDir(destination) == QDir(current_dirpath)) {
        return;  // already there
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::move_finished, this,
                          [this, connection, name](const CallOutcome& outcome) {
                              disconnect(*connection);
                              if (!outcome.ok) {
                                  ServerMessageDialog dialog(
                                      tr("Could Not Move"),
                                      move_failure_sentence(outcome.code, name),
                                      outcome.message, this);
                                  dialog.exec();
                              }
                          });
    // A machine of that name already in the folder is the server's to
    // refuse: a move never replaces one.
    session_->move(destination);
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

void MainWindow::paste() {
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasText() || data->text().isEmpty()) {
        QMessageBox::information(this, tr("Paste"),
                                 tr("The clipboard holds no text to type into the machine."));
        return;
    }
    session_->typing().paste(data->text());
}

void MainWindow::tap_named_key(const QString& name) {
    KeyboardInput& keyboard = display_->keyboard();
    // Caps Lock follows the host's while synchronised (7.3): a click cannot
    // fight it, so it only sounds, as on macOS.
    if (name == QLatin1String("Caps Lock") && keyboard.caps_lock_sync_enabled()) {
        QApplication::beep();
        return;
    }
    const std::string key = name.toStdString();
    if (!keyboard.press_named_key("led", key)) {
        return;
    }
    // Held long enough for the MOS's keyboard scan, as the macOS client does;
    // a focus loss meanwhile has released it already (R10).
    QTimer::singleShot(TAP_HOLD_MS, this, [this, key] {
        display_->keyboard().release_named_key("led", key);
    });
}

void MainWindow::set_presentation(Presentation presentation) {
    if (presentation == presentation_) {
        return;
    }
    presentation_ = presentation;
    apply_presentation();
}

void MainWindow::update_immersive_enabled() {
    if (!immersive_action_) {
        return;
    }
    const bool live = session_->connected() && is_live(recovery_->verdict());
    immersive_action_->setEnabled(live);
    if (live && immersive_when_live_) {
        immersive_when_live_ = false;
        set_presentation(Presentation::immersive);
    }
}

void MainWindow::apply_presentation() {
    static const char* const names[] = {"windowed", "full screen", "immersive"};
    std::fprintf(stderr, "beebium: presentation %s\n", names[static_cast<int>(presentation_)]);
    const Chrome chrome = chrome_for(presentation_);
    const bool immersive = presentation_ == Presentation::immersive;
    if (immersive != overlaid_) {
        // Entering, the sidebar and status bar go; any shown again while
        // immersive appear over the picture without reflowing it (6.4).
        immersive_sidebar_ = false;
        immersive_status_bar_ = false;
        overlaid_ = immersive;
    }
    menuBar()->setVisible(chrome.menu_bar);
    place_sidebar_and_status(immersive);
    frame_->set_bare(immersive);
    immersive_exit_->set_active(immersive);
    immersive_exit_->show_overlay_state(immersive_sidebar_, immersive_status_bar_);
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

void MainWindow::place_sidebar_and_status(bool immersive) {
    if (immersive) {
        // Lifted out of the layout and over the picture.
        statusBar()->hide();
        if (sidebar_->parentWidget() != frame_) {
            sidebar_->setParent(frame_);
        }
        if (status_strip_->parentWidget() != frame_) {
            statusBar()->removeWidget(status_strip_);
            status_strip_->setParent(frame_);
            status_strip_->setAutoFillBackground(true);
        }
        sidebar_->setVisible(immersive_sidebar_);
        status_strip_->setVisible(immersive_status_bar_);
        place_overlays();
        return;
    }
    if (sidebar_->parentWidget() != splitter_) {
        splitter_->insertWidget(0, sidebar_);
        splitter_->setSizes({Sidebar::WIDTH, std::max(width() - Sidebar::WIDTH, 1)});
    }
    if (status_strip_->parentWidget() == frame_) {
        status_strip_->setAutoFillBackground(false);
        statusBar()->addWidget(status_strip_, 1);
    }
    status_strip_->show();
    sidebar_->setVisible(sidebar_action_->isChecked());
    statusBar()->setVisible(chrome_for(presentation_).status_bar && status_bar_action_->isChecked());
}

bool MainWindow::status_shown() const {
    return status_strip_->isVisibleTo(this) &&
           (status_strip_->parentWidget() == frame_ || statusBar()->isVisibleTo(this));
}

void MainWindow::place_overlays() {
    if (sidebar_->parentWidget() == frame_) {
        sidebar_->setGeometry(0, 0, Sidebar::WIDTH, frame_->height());
        sidebar_->raise();
    }
    if (status_strip_->parentWidget() == frame_) {
        const int height = status_strip_->sizeHint().height() + 6;
        const int left = sidebar_->isVisible() && sidebar_->parentWidget() == frame_ ? Sidebar::WIDTH : 0;
        status_strip_->setGeometry(left, frame_->height() - height, frame_->width() - left, height);
        status_strip_->raise();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) {
        return;
    }
    const auto old_state = static_cast<QWindowStateChangeEvent*>(event)->oldState();
    std::fprintf(stderr, "beebium: window state %#x -> %#x\n", static_cast<unsigned>(old_state),
                 static_cast<unsigned>(windowState()));
    // The desktop left full screen by its own means (a compositor binding):
    // follow it, as the macOS client does its window's (6.4).
    if (left_full_screen(old_state, windowState()) && presentation_ != Presentation::windowed &&
        isVisible()) {
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

void MainWindow::update_link_control() {
    const int size = status_strip_->fontMetrics().height();
    link_button_->setIcon(QIcon(link_glyph(!unlink_requested_,
                                           palette().color(QPalette::PlaceholderText), size,
                                           devicePixelRatioF())));
    link_button_->setIconSize(QSize(size, size));
    link_button_->setToolTip(
        unlink_requested_
            ? tr("Unlinked: the machine will keep running when this window closes. Click to "
                 "re-link.")
            : tr("Linked: closing this window will shut down the machine. Click to unlink."));
}

void MainWindow::begin_close() {
    const auto server = session_->launched_server();
    if (!server) {
        // Not ours: the server is left as it is.
        finish_close();
        return;
    }
    if (unlink_requested_) {
        leave_running();
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

void MainWindow::leave_running() {
    // Unlinked: released, so it outlives this client, and left running.
    recovery_->window_closing();
    display_->keyboard().detach();
    if (!session_->connected() || !is_live(recovery_->verdict())) {
        session_->disconnect_from_server();
        finish_close();
        return;
    }
    auto done = std::make_shared<bool>(false);
    const auto close_once = [this, done] {
        if (*done) {
            return;
        }
        *done = true;
        session_->disconnect_from_server();
        finish_close();
    };
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(session_, &MachineSession::released_from_launcher, this,
                          [connection, close_once](const CallOutcome& outcome) {
                              disconnect(*connection);
                              if (!outcome.ok) {
                                  std::fprintf(stderr,
                                               "beebium: the server could not be released, "
                                               "and is left running: %s\n",
                                               qPrintable(outcome.message));
                              }
                              close_once();
                          });
    QTimer::singleShot(3000, this, close_once);
    session_->release_from_launcher();
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
        // A server on another host cannot be saved from here (3.2, #263).
        const auto& ask = session_->server_shares_filesystem() ? prompts_.unsaved
                                                               : prompts_.unsaved_remote;
        switch (ask(this, session_->machine_name())) {
        case ClosePrompts::Unsaved::cancel:
            cancel_close();
            return;
        case ClosePrompts::Unsaved::save:
            if (!session_->server_shares_filesystem()) {
                cancel_close();  // not offered; never saved from here
                return;
            }
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
    prompts.unsaved_remote = [](QWidget* window, const QString& machine) {
        QMessageBox box(QMessageBox::Warning, QObject::tr("Close Machine"),
                        QObject::tr("%1 has written to its discs, but its server is on another "
                                    "host, so it cannot be saved from here. Closing it loses "
                                    "those changes.")
                            .arg(machine),
                        QMessageBox::NoButton, window);
        QPushButton* close = box.addButton(QObject::tr("Close"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        box.exec();
        return box.clickedButton() == close ? Unsaved::discard : Unsaved::cancel;
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
