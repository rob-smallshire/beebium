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

// The sidebars leave the keys with the machine (#286): a click anywhere in a
// sidebar, on its background, a label or a button, keeps the focus in the
// display, and so does a dialog a sidebar opens. Only a field the user types
// into may take the focus, and only by a click.

#include "app/ConnectionRecovery.hpp"
#include "app/DisplayWidget.hpp"
#include "app/MachineSession.hpp"
#include "app/MainWindow.hpp"
#include "app/Sidebar.hpp"
#include "app/SidebarMode.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <qpa/qwindowsysteminterface.h>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

struct LiveWindow {
    FakeServer server;
    MainWindow window{QRhiWidget::Api::OpenGL};

    LiveWindow() {
        window.show();
        window.connect_to(server.endpoint());
        REQUIRE(wait_until([&] { return window.session().connected(); }));
        REQUIRE(wait_until([&] { return is_live(window.recovery().verdict()); }));
        window.activateWindow();
        REQUIRE(wait_until([&] { return window.isActiveWindow(); }));
    }
    ~LiveWindow() {
        window.set_quitting(true);
        window.close();
    }

    void focus_display() {
        window.display().setFocus();
        REQUIRE(wait_until([&] { return window.display().hasFocus(); }));
    }
};

// A field the user types into: the one kind of control allowed to take the
// focus, and only by a click.
bool editable(const QWidget* widget) {
    if (const auto* line = qobject_cast<const QLineEdit*>(widget)) {
        return !line->isReadOnly();
    }
    if (const auto* combo = qobject_cast<const QComboBox*>(widget)) {
        return combo->isEditable();
    }
    if (const auto* text = qobject_cast<const QTextEdit*>(widget)) {
        return !text->isReadOnly();
    }
    if (const auto* text = qobject_cast<const QPlainTextEdit*>(widget)) {
        return !text->isReadOnly();
    }
    return qobject_cast<const QAbstractSpinBox*>(widget) != nullptr;
}

// Inside a field the user types into: its own parts (a spin box's line
// edit, an editable combo's) follow it.
bool within_editable(const QWidget* widget, const QWidget* top) {
    for (const QWidget* w = widget; w && w != top; w = w->parentWidget()) {
        if (editable(w)) {
            return true;
        }
    }
    return false;
}

QString describe(const QWidget* widget) {
    QString path;
    for (const QWidget* w = widget; w; w = w->parentWidget()) {
        path += QString::fromLatin1(w->metaObject()->className());
        if (!w->objectName().isEmpty()) {
            path += QLatin1Char('#') + w->objectName();
        }
        path += QStringLiteral(" < ");
    }
    return path;
}

// A click as the window system delivers it, so that Qt gives the focus as
// the clicked widget's policy says.
void click(QWidget* widget, QPoint at) {
    QWindow* handle = widget->window()->windowHandle();
    REQUIRE(handle);
    const QPointF local = widget->mapTo(widget->window(), at);
    const QPointF global = widget->mapToGlobal(at);
    QWindowSystemInterface::handleMouseEvent(handle, local, global, Qt::LeftButton,
                                             Qt::LeftButton, QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent(handle, local, global, Qt::NoButton,
                                             Qt::LeftButton, QEvent::MouseButtonRelease);
    QWindowSystemInterface::flushWindowSystemEvents();
    QApplication::processEvents();
}

}  // namespace

TEST_CASE("No control in any sidebar takes the focus, except a field the user types into",
          "[focus][sidebar]") {
    LiveWindow live;
    Sidebar& sidebar = live.window.sidebar();
    // Every widget, the scroll areas and the pages behind them included: a
    // click on a control that refuses the focus gives it to the nearest
    // ancestor that takes it, so none may.
    for (QWidget* widget : sidebar.findChildren<QWidget*>()) {
        if (within_editable(widget, &sidebar)) {
            continue;
        }
        INFO(describe(widget).toStdString());
        CHECK_FALSE(widget->focusPolicy() & Qt::ClickFocus);
    }
    CHECK_FALSE(sidebar.focusPolicy() & Qt::ClickFocus);
}

TEST_CASE("A click on a sidebar's background or its labels leaves the keys in the display",
          "[focus][sidebar]") {
    LiveWindow live;
    Sidebar& sidebar = live.window.sidebar();
    for (const SidebarMode mode : offered_sidebar_modes()) {
        if (!sidebar.has_mode(mode)) {
            continue;
        }
        INFO(sidebar_mode_label(mode).toStdString());
        sidebar.set_mode(mode);
        QApplication::processEvents();
        live.focus_display();
        // The sidebar's own background, near its foot, below the controls.
        click(&sidebar, QPoint(sidebar.width() / 2, sidebar.height() - 4));
        CHECK(live.window.display().hasFocus());
        for (QLabel* label : sidebar.findChildren<QLabel*>()) {
            if (!label->isVisible() || label->rect().isEmpty()) {
                continue;
            }
            INFO(describe(label).toStdString());
            live.focus_display();
            click(label, label->rect().center());
            CHECK(live.window.display().hasFocus());
        }
    }
}

TEST_CASE("After the Browse dialog is cancelled the keys are the display's again",
          "[focus][sidebar][storage]") {
    LiveWindow live;
    Sidebar& sidebar = live.window.sidebar();
    sidebar.set_mode(SidebarMode::storage);
    QApplication::processEvents();
    // The drives' rows come with the server's first drive status.
    QToolButton* browse = nullptr;
    REQUIRE(wait_until([&] {
        browse = sidebar.findChild<QToolButton*>(QStringLiteral("browse"));
        return browse && browse->isVisible();
    }));
    live.focus_display();

    // Cancel the dialog as soon as it is up.
    bool dialog_seen = false;
    QTimer cancel;
    cancel.setInterval(20);
    QObject::connect(&cancel, &QTimer::timeout, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog_seen = true;
            dialog->reject();
        }
    });
    cancel.start();
    // On the folder icon, at the button's left: the arrow at its right is
    // the menu's.
    click(browse, QPoint(4, browse->height() / 2));
    cancel.stop();
    REQUIRE(dialog_seen);
    // The window keeps the display as the widget its focus returns to, and
    // the window system gives it back as it activates the window again
    // (which the offscreen platform leaves to the test).
    CHECK(live.window.focusWidget() == &live.window.display());
    live.window.activateWindow();
    REQUIRE(wait_until([&] { return live.window.isActiveWindow(); }));
    CHECK(live.window.display().hasFocus());
}
