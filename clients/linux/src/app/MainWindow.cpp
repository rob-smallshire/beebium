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

#include "app/DisplayWidget.hpp"
#include "app/ErrorPresentation.hpp"
#include "app/MachineSession.hpp"

#include <QCloseEvent>
#include <QMenuBar>
#include <QLabel>
#include <QStatusBar>

#include <cstdio>

namespace beebium::linux_client {

MainWindow::MainWindow(QRhiWidget::Api graphics_api, QWidget* parent)
    : QMainWindow(parent),
      session_(new MachineSession(this)),
      display_(new DisplayWidget(session_->mailbox(), this)) {
    setWindowTitle(QStringLiteral("Beebium"));

    // The menus come from the client specification (13.2); the bar exists
    // now so the window has its final shape. menuBar() creates it.
    menuBar();

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
                ServerMessageDialog dialog(
                    tr("Could Not Connect"),
                    connection_failure_sentence(failure, session_->endpoint()),
                    server_message, this);
                dialog.exec();
            });
    connect(session_, &MachineSession::connected_to_server, this,
            [] { std::fprintf(stderr, "beebium: connected\n"); });
    connect(session_, &MachineSession::liveness_changed, this, [](Liveness liveness) {
        std::fprintf(stderr, "beebium: liveness %s\n", liveness_name(liveness));
    });
    // The title is the machine's name, exactly (5.7). Qt has no window
    // subtitle, so the description goes at the start of the status bar.
    description_label_ = new QLabel(this);
    statusBar()->addWidget(description_label_, 1);
    connect(session_, &MachineSession::identity_changed, this,
            [this](const QString& name, const QString& description) {
                setWindowTitle(name.isEmpty() ? QStringLiteral("Beebium") : name);
                description_label_->setText(description);
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

void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing the window is dropping its connection (lifecycle-management.md
    // section 4). Held keys are released first, and the key queue drains
    // before the streams are cancelled, so nothing is left down on the machine.
    display_->keyboard().detach();
    session_->disconnect_from_server();
    event->accept();
}

}  // namespace beebium::linux_client
