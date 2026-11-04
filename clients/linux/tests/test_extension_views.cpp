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

// Extension-supplied views (client specification 12.1): the eleven control
// primitives rendered by type and patched in place by id, every action
// fire-and-observe with the view's revision, an edit in progress committed
// before another control acts; and the subscription, held while shown,
// dropped when the extension goes, kept stale when the server is lost.

#include "app/ExtensionPanel.hpp"
#include "app/ExtensionViewWidget.hpp"
#include "app/SessionExtensions.hpp"

#include "FakeBeebiumServer.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <grpcpp/grpcpp.h>

#include <memory>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

beebium::Control* add(beebium::Control* group, const char* id) {
    auto* control = group->mutable_group()->add_controls();
    control->set_id(id);
    return control;
}

// One of each primitive, at a revision.
beebium::View every_primitive(std::uint64_t revision, const QString& file_path = {}) {
    beebium::View view;
    view.set_extension_id("ext-1");
    view.set_view_revision(revision);
    beebium::Control* root = view.mutable_root();
    root->set_id("root");
    root->mutable_group()->set_label("Transport");
    add(root, "label")->mutable_label()->set_text("Station 1.254");
    root->mutable_group()->mutable_controls(0)->mutable_label()->set_secondary_text("in force");
    auto* indicator = add(root, "status")->mutable_indicator();
    indicator->set_state(beebium::Indicator::OK);
    indicator->set_text("Connected");
    auto* toggle = add(root, "enabled")->mutable_toggle();
    toggle->set_label("Enabled");
    toggle->set_value(false);
    auto* button = add(root, "connect")->mutable_button();
    button->set_label("Connect");
    button->set_enabled(true);
    auto* choice = add(root, "mode")->mutable_choice();
    choice->set_label("Mode");
    choice->add_options("auto");
    choice->add_options("manual");
    choice->set_selected_index(0);
    auto* text = add(root, "name")->mutable_text_input();
    text->set_label("Name");
    text->set_value("old");
    text->set_help("What the peer is called");
    text->set_note("Saving pins the port");
    auto* combo = add(root, "host")->mutable_editable_choice();
    combo->set_label("Host");
    combo->set_value("pi.local");
    combo->add_options("pi.local");
    combo->add_options("beeb.local");
    auto* modal_control = add(root, "station");
    auto* modal = modal_control->mutable_modal_editor();
    modal->mutable_anchor()->set_id("station-anchor");
    modal->mutable_anchor()->mutable_label()->set_text("254");
    modal->mutable_editor()->set_id("station-editor");
    auto* station_field = modal->mutable_editor()->mutable_group()->add_controls();
    station_field->set_id("number");
    station_field->mutable_text_input()->set_value("254");
    modal->set_editable(true);
    auto* list = add(root, "peers")->mutable_editable_list();
    list->set_title("Peers (2)");
    list->set_can_add(true);
    list->mutable_add_editor()->set_id("add-editor");
    auto* add_host = list->mutable_add_editor()->mutable_group()->add_controls();
    add_host->set_id("address");
    add_host->mutable_text_input()->set_value("");
    for (const char* id : {"peer-1", "peer-2"}) {
        auto* item = list->add_items();
        item->set_id(id);
        item->set_primary(std::string("0.") + id);
        item->set_secondary("mDNS");
        item->set_removable(std::string(id) == "peer-2");
        item->set_editable(true);
        item->mutable_editor()->set_id(std::string(id) + "-editor");
        auto* field = item->mutable_editor()->mutable_group()->add_controls();
        field->set_id("address");
        field->mutable_text_input()->set_value(id);
        auto* forget = item->add_actions();
        forget->set_id("forget");
        forget->set_title("Forget");
        forget->set_warning("Forget this peer?");
    }
    auto* file = add(root, "map")->mutable_file_reference();
    file->set_path(file_path.toStdString());
    file->set_display_name("Shared AUN map");
    file->set_state(beebium::Indicator::OK);
    file->set_state_text("loaded");
    auto* reload = file->add_actions();
    reload->set_id("reload");
    reload->set_title("Reload");
    return view;
}

struct Rendered {
    std::vector<beebium::DispatchRequest> sent;
    bool local = true;
    ExtensionViewWidget view{[this](const beebium::DispatchRequest& r) { sent.push_back(r); },
                             [this] { return local; }};
    std::optional<EditorValues> editor_answer;
    QString editor_title;
    bool confirm_answer = true;

    Rendered() {
        view.set_editor_runner([this](const QString& title, const beebium::Control&,
                                      const QString&) {
            editor_title = title;
            return editor_answer;
        });
        view.set_confirmer([this](const QString&, const QString&) { return confirm_answer; });
    }
    template <typename T>
    T* inside(const char* id) {
        QWidget* node = view.control_widget(QString::fromLatin1(id));
        REQUIRE(node);
        T* found = node->findChild<T*>();
        REQUIRE(found);
        return found;
    }
    QAction* menu_action(QMenu* menu, const QString& text) {
        for (QAction* action : menu->actions()) {
            if (action->text().remove(QLatin1Char('&')) == text) {
                return action;
            }
        }
        return nullptr;
    }
};

}  // namespace

TEST_CASE("Every primitive renders by its type, named by its control id", "[extension][render]") {
    Rendered r;
    r.view.set_view(every_primitive(5));
    for (const char* id : {"root", "label", "status", "enabled", "connect", "mode", "name", "host",
                           "station", "station-anchor", "peers", "map"}) {
        INFO(id);
        CHECK(r.view.control_widget(QString::fromLatin1(id)) != nullptr);
    }
    QStringList texts;
    for (QLabel* label : r.view.findChildren<QLabel*>()) {
        texts << label->text();
    }
    for (const char* shown : {"Transport", "Station 1.254", "in force", "Connected", "Mode",
                              "Name", "Saving pins the port", "Host", "254", "PEERS (2)",
                              "Shared AUN map", "loaded"}) {
        INFO(shown);
        CHECK(texts.contains(QString::fromUtf8(shown)));
    }
    CHECK(r.inside<QCheckBox>("enabled")->text() == QStringLiteral("Enabled"));
    CHECK(r.inside<QPushButton>("connect")->text() == QStringLiteral("Connect"));
    CHECK(r.inside<QLineEdit>("name")->text() == QStringLiteral("old"));
    CHECK(r.inside<QComboBox>("host")->currentText() == QStringLiteral("pi.local"));
    QListWidget* peers = r.inside<QListWidget>("peers");
    CHECK(peers->count() == 2);
    // As tall as its rows, not the panel's free space.
    CHECK(peers->height() ==
          2 * peers->frameWidth() + peers->sizeHintForRow(0) + peers->sizeHintForRow(1));
}

TEST_CASE("Toggles, buttons and choices dispatch with the view's revision and change nothing",
          "[extension][render][dispatch]") {
    Rendered r;
    r.view.set_view(every_primitive(9));
    QCheckBox* toggle = r.inside<QCheckBox>("enabled");
    toggle->click();
    REQUIRE(r.sent.size() == 1);
    CHECK(r.sent[0].control_id() == "enabled");
    CHECK(r.sent[0].bool_value());
    CHECK(r.sent[0].view_revision() == 9);
    CHECK_FALSE(toggle->isChecked());  // the server's value until it says otherwise

    r.inside<QPushButton>("connect")->click();
    CHECK(r.sent.back().control_id() == "connect");
    CHECK(r.sent.back().payload_case() == beebium::DispatchRequest::PAYLOAD_NOT_SET);

    QComboBox* mode = r.inside<QComboBox>("mode");
    Q_EMIT mode->activated(1);
    CHECK(r.sent.back().index_value() == 1u);
    CHECK(mode->currentIndex() == 0);
    CHECK(r.sent.back().view_revision() == 9);
}

TEST_CASE("A new view patches the panel in place", "[extension][render]") {
    Rendered r;
    r.view.set_view(every_primitive(1));
    QWidget* before = r.view.control_widget(QStringLiteral("enabled"));
    QCheckBox* box = r.inside<QCheckBox>("enabled");
    beebium::View next = every_primitive(2);
    next.mutable_root()->mutable_group()->mutable_controls(2)->mutable_toggle()->set_value(true);
    r.view.set_view(next);
    CHECK(r.view.control_widget(QStringLiteral("enabled")) == before);
    CHECK(r.inside<QCheckBox>("enabled") == box);
    CHECK(box->isChecked());
    // Gone from the view, gone from the panel.
    beebium::View smaller = next;
    smaller.mutable_root()->mutable_group()->mutable_controls()->DeleteSubrange(3, 1);
    r.view.set_view(smaller);
    CHECK(r.view.control_widget(QStringLiteral("connect")) == nullptr);
    CHECK(r.view.control_widget(QStringLiteral("enabled")) == before);
}

TEST_CASE("Text commits on the end of editing, and before any other control acts",
          "[extension][render][text]") {
    Rendered r;
    r.view.set_view(every_primitive(4));
    QLineEdit* name = r.inside<QLineEdit>("name");
    name->setText(QStringLiteral("new"));
    CHECK(r.sent.empty());  // nothing per keystroke
    Q_EMIT name->editingFinished();
    REQUIRE(r.sent.size() == 1);
    CHECK(r.sent[0].control_id() == "name");
    CHECK(r.sent[0].string_value() == "new");
    Q_EMIT name->editingFinished();
    CHECK(r.sent.size() == 1);  // sent once

    // Typed but not finished, then another control: the text lands first.
    QComboBox* host = r.inside<QComboBox>("host");
    host->setEditText(QStringLiteral("beeb.local"));
    r.inside<QCheckBox>("enabled")->click();
    REQUIRE(r.sent.size() == 3);
    CHECK(r.sent[1].control_id() == "host");
    CHECK(r.sent[1].string_value() == "beeb.local");
    CHECK(r.sent[2].control_id() == "enabled");
    CHECK(r.sent[1].view_revision() == 4);
}

TEST_CASE("A modal editor commits all its fields at once, or nothing",
          "[extension][render][editor]") {
    Rendered r;
    r.view.set_view(every_primitive(6));
    auto* edit = r.view.control_widget(QStringLiteral("station"))->findChild<QToolButton*>(
        QStringLiteral("edit"));
    REQUIRE(edit);
    CHECK(edit->isVisibleTo(r.view.control_widget(QStringLiteral("station"))));
    r.editor_answer = std::nullopt;  // cancelled
    edit->click();
    CHECK(r.sent.empty());
    r.editor_answer = EditorValues{{"number", std::string("200")}};
    edit->click();
    REQUIRE(r.sent.size() == 1);
    CHECK(r.sent[0].control_id() == "station");
    REQUIRE(r.sent[0].editor_commit().fields_size() == 1);
    CHECK(r.sent[0].editor_commit().fields(0).string_value() == "200");
    CHECK(r.sent[0].view_revision() == 6);
}

TEST_CASE("An editable list adds, edits, removes and acts, confirming a warning first",
          "[extension][render][list]") {
    Rendered r;
    r.view.set_view(every_primitive(3));
    QWidget* node = r.view.control_widget(QStringLiteral("peers"));
    auto* add_button = node->findChild<QToolButton*>(QStringLiteral("add"));
    auto* remove_button = node->findChild<QToolButton*>(QStringLiteral("remove"));
    auto* list = node->findChild<QListWidget*>(QStringLiteral("items"));
    REQUIRE(add_button);
    REQUIRE(remove_button);
    CHECK(add_button->isEnabled());
    CHECK_FALSE(remove_button->isEnabled());  // nothing chosen

    r.editor_answer = EditorValues{{"address", std::string("192.168.1.10")}};
    add_button->click();
    REQUIRE(r.sent.size() == 1);
    CHECK(r.sent[0].editable_list_event().kind() == beebium::EditableListEvent::ADD);
    CHECK(r.sent[0].editable_list_event().commit().fields(0).string_value() == "192.168.1.10");

    list->setCurrentRow(0);  // peer-1 is not removable
    CHECK_FALSE(remove_button->isEnabled());
    list->setCurrentRow(1);
    CHECK(remove_button->isEnabled());
    remove_button->click();
    CHECK(r.sent.back().editable_list_event().kind() == beebium::EditableListEvent::REMOVE);
    CHECK(r.sent.back().editable_list_event().item_id() == "peer-2");

    Q_EMIT list->itemDoubleClicked(list->item(0));
    CHECK(r.sent.back().editable_list_event().kind() == beebium::EditableListEvent::EDIT);
    CHECK(r.sent.back().editable_list_event().item_id() == "peer-1");

    std::unique_ptr<QMenu> menu(static_cast<ExtensionNode*>(node)->make_menu("peer-1"));
    QAction* forget = r.menu_action(menu.get(), QStringLiteral("Forget"));
    REQUIRE(forget);
    const auto before = r.sent.size();
    r.confirm_answer = false;
    forget->trigger();
    CHECK(r.sent.size() == before);  // declined
    r.confirm_answer = true;
    forget->trigger();
    CHECK(r.sent.back().editable_list_event().kind() == beebium::EditableListEvent::ACTION);
    CHECK(r.sent.back().editable_list_event().action_id() == "forget");
    // The selection holds through a new view, by item id.
    r.view.set_view(every_primitive(4));
    CHECK(list->currentRow() == 1);
}

TEST_CASE("A file reference offers the server's actions, Copy Path and, where the file is here, "
          "Open Containing Folder",
          "[extension][render][file]") {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("aun-map.json"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
    }
    Rendered r;
    r.view.set_view(every_primitive(8, path));
    auto* node = static_cast<ExtensionNode*>(r.view.control_widget(QStringLiteral("map")));
    CHECK(node->toolTip() == path);
    {
        std::unique_ptr<QMenu> menu(node->make_menu());
        QStringList texts;
        for (QAction* action : menu->actions()) {
            texts << (action->isSeparator() ? QStringLiteral("|")
                                            : action->text().remove(QLatin1Char('&')));
        }
        CHECK(texts == QStringList{QStringLiteral("Reload"), QStringLiteral("|"),
                                   QStringLiteral("Copy Path"),
                                   QStringLiteral("Open Containing Folder")});
        r.menu_action(menu.get(), QStringLiteral("Reload"))->trigger();
        CHECK(r.sent.back().file_action_id() == "reload");
        CHECK(r.sent.back().view_revision() == 8);
        r.menu_action(menu.get(), QStringLiteral("Copy Path"))->trigger();
        CHECK(QApplication::clipboard()->text() == path);
        CHECK(r.menu_action(menu.get(), QStringLiteral("Open Containing Folder"))->isEnabled());
    }
    r.local = false;
    {
        std::unique_ptr<QMenu> menu(node->make_menu());
        QAction* reveal = r.menu_action(menu.get(), QStringLiteral("Open Containing Folder"));
        CHECK_FALSE(reveal->isEnabled());
        CHECK(reveal->toolTip() == QStringLiteral("The server is on another host"));
        // The server's own actions are never gated.
        CHECK(r.menu_action(menu.get(), QStringLiteral("Reload"))->isEnabled());
    }
}

TEST_CASE("A stale view is kept but inert", "[extension][render]") {
    Rendered r;
    r.view.set_view(every_primitive(2));
    r.view.set_stale(true);
    CHECK_FALSE(r.view.isEnabled());
    r.view.act(toggle_action(r.view.view(), "enabled", true));
    CHECK(r.sent.empty());
}

// --- the subscription -----------------------------------------------------------------

namespace {

struct Session {
    FakeServer server;
    SessionExtensions extensions;
    Session() {
        extensions.start(grpc::CreateChannel(server.endpoint().toStdString(),
                                             grpc::InsecureChannelCredentials()));
    }
};

}  // namespace

TEST_CASE("A panel subscribes while it shows and releases its view when it hides",
          "[extension][subscription]") {
    Session s;
    s.server.extension_ui.push(every_primitive(1));
    ExtensionPanel panel(s.extensions, "ext-1", [] { return true; });
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(s.server.extension_ui.subscribers("ext-1") == 0);
    panel.show();
    REQUIRE(wait_until([&] { return panel.feed()->view().has_value(); }));
    CHECK(s.server.extension_ui.subscribers("ext-1") == 1);
    CHECK(panel.renderer()->control_widget(QStringLiteral("enabled")) != nullptr);
    // A push patches the panel.
    beebium::View next = every_primitive(2);
    next.mutable_root()->mutable_group()->mutable_controls(2)->mutable_toggle()->set_value(true);
    s.server.extension_ui.push(next);
    REQUIRE(wait_until([&] { return panel.feed()->view()->view_revision() == 2; }));
    panel.hide();
    REQUIRE(wait_until([&] { return s.server.extension_ui.subscribers("ext-1") == 0; }));
}

TEST_CASE("A gone extension's view is dropped quietly; a lost server's is kept, stale",
          "[extension][subscription]") {
    Session s;
    s.server.extension_ui.push(every_primitive(1));
    ExtensionViewFeed feed(s.extensions, "ext-1");
    feed.set_watching(true);
    REQUIRE(wait_until([&] { return feed.view().has_value(); }));
    s.server.extension_ui.end("ext-1", grpc::StatusCode::NOT_FOUND);
    REQUIRE(wait_until([&] { return feed.gone(); }));
    CHECK_FALSE(feed.view().has_value());
    CHECK_FALSE(feed.stale());

    s.server.extension_ui.push(every_primitive(3));
    ExtensionViewFeed other(s.extensions, "ext-1");
    other.set_watching(true);
    REQUIRE(wait_until([&] { return other.view().has_value(); }));
    s.server.extension_ui.end("ext-1", grpc::StatusCode::UNAVAILABLE);
    REQUIRE(wait_until([&] { return other.stale(); }));
    CHECK(other.view().has_value());
    CHECK(other.view()->view_revision() == 3);

    // The connection itself going keeps the view, stale, too.
    s.server.extension_ui.push(every_primitive(4));
    ExtensionViewFeed third(s.extensions, "ext-1");
    third.set_watching(true);
    REQUIRE(wait_until([&] { return third.view().has_value(); }));
    s.extensions.stop();
    CHECK(third.stale());
    CHECK(third.view().has_value());
}

TEST_CASE("An action at a stale revision is refused and changes nothing",
          "[extension][subscription][dispatch]") {
    Session s;
    s.server.extension_ui.push(every_primitive(5));
    std::optional<QString> refused;
    QObject::connect(&s.extensions, &SessionExtensions::refused,
                     [&](const QString&, const QString& error) { refused = error; });
    s.extensions.dispatch(toggle_action(every_primitive(4), "enabled", true));
    REQUIRE(wait_until([&] { return refused.has_value(); }));
    CHECK(*refused == QStringLiteral("stale view revision"));
    const auto sent = s.server.extension_ui.dispatched();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].view_revision() == 4);
    // At the current revision it is accepted.
    refused.reset();
    s.extensions.dispatch(toggle_action(every_primitive(5), "enabled", true));
    REQUIRE(wait_until([&] { return s.server.extension_ui.dispatched().size() == 2; }));
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK_FALSE(refused.has_value());
}
