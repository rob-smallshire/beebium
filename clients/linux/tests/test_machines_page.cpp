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

// Settings > Machines (client specification 13.3): the built-in presets
// and the user's, in the library's order; Duplicate under a free name,
// Export, Delete of the user's own after the macOS client's question,
// Open Containing Folder; the library read again after a change, and a
// failure shown as a sentence with the server's words.

#include "app/MachinesPage.hpp"
#include "app/ClientSettings.hpp"
#include "app/SettingsDialog.hpp"

#include "WaitUntil.hpp"

#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

PresetListing listing(const char* id, const char* name, bool built_in, const char* date = "") {
    PresetListing preset;
    preset.id = QString::fromLatin1(id);
    preset.name = QString::fromLatin1(name);
    preset.source = built_in ? QStringLiteral("system") : QStringLiteral("user");
    preset.model = QStringLiteral("model-b");
    preset.description = QStringLiteral("32K Model B");
    preset.release_date = QString::fromLatin1(date);
    preset.filepath = QStringLiteral("/presets/%1.beebium-preset").arg(QString::fromLatin1(id));
    return preset;
}

// The servers, scripted.
struct Library {
    std::vector<PresetListing> presets{listing("model-b-plus", "Model B+", true, "1985-07"),
                                       listing("model-b", "Model B", true, "1981-12"),
                                       listing("mine", "My Model B", false)};
    int lists = 0;
    std::vector<std::pair<QString, QString>> duplicated;  // source id, name
    std::vector<std::pair<QString, QString>> exported;    // id, filepath
    std::vector<QString> deleted;
    bool ok = true;

    PresetLibraryHost host() {
        PresetLibraryHost host;
        host.list_presets = [this](PresetLibraryHost::Presets done) {
            ++lists;
            done(presets, {});
        };
        host.duplicate = [this](const PresetListing& source, const QString& name,
                                PresetLibraryHost::Done done) {
            duplicated.emplace_back(source.id, name);
            if (ok) {
                presets.push_back(listing("copy", "copied", false));
                presets.back().name = name;
            }
            done(ok, ok ? QString() : QStringLiteral("Error: a preset named that exists"));
        };
        host.export_preset = [this](const PresetListing& preset, const QString& filepath,
                                    PresetLibraryHost::Done done) {
            exported.emplace_back(preset.id, filepath);
            done(true, {});
        };
        host.remove = [this](const PresetListing& preset, PresetLibraryHost::Done done) {
            deleted.push_back(preset.id);
            std::erase_if(presets, [&](const PresetListing& p) { return p.id == preset.id; });
            done(true, {});
        };
        return host;
    }
};

struct Fixture {
    Library library;
    std::unique_ptr<MachinesPage> page;
    std::optional<QString> name;  // Duplicate's answer
    QString suggested;
    QStringList taken;
    bool confirm = true;
    QString confirmed;
    QStringList revealed;

    Fixture() {
        page = std::make_unique<MachinesPage>(library.host());
        page->set_ask_name([this](const QString& s, const QStringList& t) {
            suggested = s;
            taken = t;
            return name;
        });
        page->set_ask_filepath([](const QString& suggested_filepath) {
            return std::optional<QString>(suggested_filepath);
        });
        page->set_confirm_delete([this](const QString& n) {
            confirmed = n;
            return confirm;
        });
        page->set_reveal([this](const QString& filepath) { revealed << filepath; });
    }
    void select(const char* id) {
        QListWidgetItem* item = page->row(QString::fromLatin1(id));
        REQUIRE(item);
        item->listWidget()->setCurrentItem(item);
    }
};

QStringList ids(QListWidget* list) {
    QStringList out;
    for (int i = 0; i < list->count(); ++i) {
        out << list->item(i)->data(Qt::UserRole).toString();
    }
    return out;
}

}  // namespace

TEST_CASE("The library lists built-in and the user's presets apart, in order", "[settings][machines]") {
    Fixture f;
    CHECK(ids(f.page->built_in_list()) == QStringList({QStringLiteral("model-b"), QStringLiteral("model-b-plus")}));
    CHECK(ids(f.page->user_list()) == QStringList({QStringLiteral("mine")}));
    CHECK_FALSE(f.page->no_user_presets()->isVisibleTo(f.page.get()));
    // Nothing selected, nothing to do.
    CHECK_FALSE(f.page->duplicate_button()->isEnabled());
    f.select("model-b");
    CHECK(f.page->duplicate_button()->isEnabled());
    CHECK(f.page->export_button()->isEnabled());
    CHECK_FALSE(f.page->delete_button()->isEnabled());  // built-in
    f.select("mine");
    CHECK(f.page->delete_button()->isEnabled());
    CHECK(f.page->built_in_list()->selectedItems().isEmpty());  // one selection
}

TEST_CASE("No presets of the user's own says so", "[settings][machines]") {
    Fixture f;
    f.library.presets.pop_back();
    f.page->reload();
    CHECK(f.page->no_user_presets()->isVisibleTo(f.page.get()));
    CHECK(f.page->no_user_presets()->text() == QStringLiteral("No custom presets yet"));
}

TEST_CASE("Duplicate suggests a free copy name and reads the library again", "[settings][machines]") {
    Fixture f;
    f.select("model-b");
    f.name = QStringLiteral("Model B Copy");
    bool changed = false;
    QObject::connect(f.page.get(), &MachinesPage::changed, [&] { changed = true; });
    const int lists = f.library.lists;
    f.page->duplicate_button()->click();
    CHECK(f.suggested == QStringLiteral("Model B Copy"));
    CHECK(f.taken == QStringList({QStringLiteral("My Model B")}));
    REQUIRE(f.library.duplicated.size() == 1);
    CHECK(f.library.duplicated[0] == std::pair(QStringLiteral("model-b"), QStringLiteral("Model B Copy")));
    CHECK(f.library.lists > lists);
    CHECK(changed);
    CHECK(f.page->row(QStringLiteral("copy")));
}

TEST_CASE("A failed duplicate is a sentence with the server's words", "[settings][machines]") {
    Fixture f;
    f.library.ok = false;
    f.select("mine");
    f.name = QStringLiteral("Anything");
    f.page->duplicate_button()->click();
    CHECK(f.page->error()->isVisibleTo(f.page.get()));
    CHECK(f.page->error()->text() ==
          QStringLiteral("The preset could not be duplicated.\nError: a preset named that exists"));
}

TEST_CASE("Export saves the preset's archive under its own name", "[settings][machines]") {
    Fixture f;
    f.select("model-b");
    f.page->export_button()->click();
    REQUIRE(f.library.exported.size() == 1);
    CHECK(f.library.exported[0].first == QStringLiteral("model-b"));
    CHECK(f.library.exported[0].second.endsWith(QStringLiteral("/model-b.beebium-preset")));
}

TEST_CASE("Delete asks first, and a user's preset only", "[settings][machines]") {
    Fixture f;
    f.select("mine");
    f.confirm = false;
    f.page->delete_button()->click();
    CHECK(f.confirmed == QStringLiteral("My Model B"));
    CHECK(f.library.deleted.empty());
    f.confirm = true;
    f.page->delete_button()->click();
    CHECK(f.library.deleted == std::vector<QString>{QStringLiteral("mine")});
    CHECK_FALSE(f.page->row(QStringLiteral("mine")));
    CHECK(f.page->no_user_presets()->isVisibleTo(f.page.get()));
    // A built-in's menu has no Delete.
    std::unique_ptr<QMenu> menu(f.page->row_menu(f.library.presets[0]));
    QStringList items;
    for (QAction* a : menu->actions()) {
        if (!a->isSeparator()) {
            items << a->text();
        }
    }
    CHECK(items == QStringList({QStringLiteral("Duplicate..."), QStringLiteral("Export..."),
                                QStringLiteral("Open Containing Folder")}));
}

TEST_CASE("Once the library has changed, the dialog's Cancel says Close", "[settings][machines]") {
    QTemporaryDir directory;
    ClientSettings settings(directory.filePath(QStringLiteral("Beebium.conf")));
    Library library;
    SettingsDialog dialog(settings, library.host());
    REQUIRE(dialog.machines());
    CHECK(dialog.cancel_button()->text() != QStringLiteral("Close"));
    dialog.machines()->set_confirm_delete([](const QString&) { return true; });
    dialog.machines()->remove(library.presets[2]);
    CHECK(dialog.cancel_button()->text() == QStringLiteral("Close"));
}
