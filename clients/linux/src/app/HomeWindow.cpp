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

#include "app/HomeWindow.hpp"

#include "app/ApplicationInfo.hpp"
#include "app/PresetsPage.hpp"
#include "app/SavedMachinesPage.hpp"
#include "app/Shortcuts.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QMessageBox>
#include <QStackedWidget>
#include <QFontMetrics>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

namespace {

QString text(std::string_view view) {
    return QString::fromLatin1(view.data(), static_cast<qsizetype>(view.size()));
}

QIcon beebium_icon() {
    const QIcon application = QApplication::windowIcon();
    return application.isNull() ? QIcon::fromTheme(QStringLiteral("org.beebium.Beebium"))
                                : application;
}

}  // namespace

HomePage initial_home_page(bool has_recent_machines) {
    return has_recent_machines ? HomePage::saved_machines : HomePage::presets;
}

void show_about_beebium(QWidget* parent) {
    QMessageBox::about(parent, QObject::tr("About Beebium"),
                       QObject::tr("<p><b>Beebium</b> %1</p><p>A BBC Micro emulator.</p>"
                                   "<p>Protocol %2</p>")
                           .arg(text(client_version()), text(client_protocol_fingerprint())));
}

HomeWindow::HomeWindow(QString thumbnail_cache_dirpath, SavedMachines& saved_machines,
                       DiscoveredMachines& discovered, RunningMachinesPage::IsOpen is_open,
                       QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("home"));
    setWindowTitle(tr("Beebium"));
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(build_header());

    auto* body = new QHBoxLayout;
    pages_list_ = new QListWidget(central);
    pages_list_->setObjectName(QStringLiteral("homePages"));
    // A navigation list of the page names alone, as on macOS: a little
    // larger than body text, in rows tall enough to read as a list of places.
    pages_list_->setFrameShape(QFrame::NoFrame);
    pages_list_->setSpacing(4);
    QFont pages_font = pages_list_->font();
    pages_font.setPointSizeF(pages_font.pointSizeF() * 1.15);
    pages_list_->setFont(pages_font);
    const QFontMetrics metrics(pages_font);
    const int row_height = metrics.height() * 2;
    int widest = 0;
    for (const QString& name : {tr("Presets"), tr("Recent Machines"), tr("Discovered Machines")}) {
        auto* entry = new QListWidgetItem(name, pages_list_);
        entry->setSizeHint(QSize(metrics.horizontalAdvance(name) + 24, row_height));
        widest = std::max(widest, metrics.horizontalAdvance(name));
    }
    pages_list_->setFixedWidth(widest + 48);
    body->addWidget(pages_list_);

    pages_ = new QStackedWidget(central);
    presets_ = new PresetsPage(std::move(thumbnail_cache_dirpath), pages_);
    saved_ = new SavedMachinesPage(saved_machines, pages_);
    running_ = new RunningMachinesPage(discovered, std::move(is_open), pages_);
    pages_->addWidget(presets_);
    pages_->addWidget(saved_);
    pages_->addWidget(running_);
    body->addWidget(pages_, 1);
    layout->addLayout(body, 1);
    setCentralWidget(central);

    connect(pages_list_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
    pages_list_->setCurrentRow(0);
    build_menus();
    resize(900, 640);
}

QWidget* HomeWindow::build_header() {
    auto* header = new QWidget(this);
    auto* layout = new QHBoxLayout(header);
    const QIcon icon = beebium_icon();
    if (!icon.isNull()) {
        auto* picture = new QLabel(header);
        picture->setPixmap(icon.pixmap(64, 64));
        layout->addWidget(picture);
    }
    auto* words = new QVBoxLayout;
    auto* name = new QLabel(tr("Beebium"), header);
    QFont font = name->font();
    font.setPointSizeF(font.pointSizeF() * 1.6);
    font.setBold(true);
    name->setFont(font);
    auto* version = new QLabel(tr("Version %1").arg(text(client_version())), header);
    version->setEnabled(false);  // the secondary text colour
    words->addWidget(name);
    words->addWidget(version);
    layout->addLayout(words);
    layout->addStretch(1);
    return header;
}

void HomeWindow::build_menus() {
    // No machine here, so the standard Ctrl shortcuts stay; each has the
    // machine window's Meta form beside it, so one habit works in both
    // (Shortcuts.hpp).
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* new_action = file->addAction(tr("&New Machine..."));
    set_session_shortcuts(new_action, QKeySequence(QStringLiteral("Meta+N")),
                          QKeySequence(QStringLiteral("Ctrl+N")));
    new_action->setObjectName(QStringLiteral("newMachine"));
    connect(new_action, &QAction::triggered, this, &HomeWindow::new_machine_requested);
    connect(presets_, &PresetsPage::new_machine_requested, this, &HomeWindow::new_machine_requested);
    // As the presets page's button: only with this host's servers.
    const auto follow = [this, new_action] {
        new_action->setEnabled(presets_->new_machine_button()->isEnabled());
        new_action->setToolTip(presets_->new_machine_button()->toolTip());
    };
    follow();
    connect(presets_, &PresetsPage::servers_changed, this, follow);
    QAction* open_action = file->addAction(tr("&Open Machine..."));
    set_session_shortcuts(open_action, QKeySequence(QStringLiteral("Meta+O")),
                          QKeySequence(QStringLiteral("Ctrl+O")));
    connect(open_action, &QAction::triggered, saved_, &SavedMachinesPage::browse_requested);
    QAction* recent_action = file->addAction(tr("Open &Recent..."));
    connect(recent_action, &QAction::triggered, this,
            [this] { show_page(HomePage::saved_machines); });
    QAction* connect_action = file->addAction(tr("&Connect to Machine..."));
    set_session_shortcuts(connect_action, QKeySequence(QStringLiteral("Meta+Shift+O")),
                          QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(connect_action, &QAction::triggered, this,
            [this] { show_page(HomePage::running_machines); });
    file->addSeparator();
    QAction* close_action = file->addAction(tr("C&lose Window"));
    set_session_shortcuts(close_action, QKeySequence(QStringLiteral("Meta+W")),
                          QKeySequence(QStringLiteral("Ctrl+W")));
    connect(close_action, &QAction::triggered, this, &QWidget::close);
    QAction* quit_action = file->addAction(tr("&Quit"));
    set_session_shortcuts(quit_action, QKeySequence(QStringLiteral("Meta+Q")),
                          QKeySequence(QStringLiteral("Ctrl+Q")));
    quit_action->setMenuRole(QAction::QuitRole);
    connect(quit_action, &QAction::triggered, this, &HomeWindow::quit_requested);

    QMenu* settings = menuBar()->addMenu(tr("&Settings"));
    QAction* configure_action = settings->addAction(tr("&Configure Beebium..."));
    // Meta+, as the macOS client's Command-, (Settings).
    set_session_shortcuts(configure_action, QKeySequence(QStringLiteral("Meta+,")),
                          QKeySequence(QStringLiteral("Ctrl+Shift+,")));
    configure_action->setMenuRole(QAction::PreferencesRole);
    connect(configure_action, &QAction::triggered, this, &HomeWindow::configure_requested);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* about_action = help->addAction(tr("&About Beebium"));
    about_action->setMenuRole(QAction::AboutRole);
    connect(about_action, &QAction::triggered, this, [this] { show_about_beebium(this); });
}

void HomeWindow::show_page(HomePage page) {
    pages_list_->setCurrentRow(static_cast<int>(page));
    if (isMinimized()) {
        showNormal();
    }
    show();
    raise();
    activateWindow();
}

HomePage HomeWindow::page() const {
    return static_cast<HomePage>(pages_->currentIndex());
}

void HomeWindow::closeEvent(QCloseEvent* event) {
    // Hidden, never destroyed: there is only ever the one (13.4).
    event->accept();
    Q_EMIT closed();
}

}  // namespace beebium::linux_client
