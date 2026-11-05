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

// The preset library's order and the name Duplicate suggests, as the
// macOS client's Settings > Machines has them.

#include "servers/PresetLibrary.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

PresetListing preset(const char* name, const char* date) {
    PresetListing p;
    p.id = QString::fromUtf8(name);
    p.name = QString::fromUtf8(name);
    p.release_date = QString::fromUtf8(date);
    return p;
}

QStringList names(const std::vector<PresetListing>& presets) {
    QStringList out;
    for (const auto& p : presets) {
        out << p.name;
    }
    return out;
}

}  // namespace

TEST_CASE("The library lists presets by release date, undated last, then by name", "[presets][library]") {
    const auto ordered = library_order({preset("Undated", ""), preset("B+ 2", "1985-07"),
                                        preset("Model B", "1981-12-01"), preset("B+ 10", "1985-07"),
                                        preset("Early", "1981")});
    CHECK(names(ordered) == QStringList({QStringLiteral("Early"), QStringLiteral("Model B"),
                                         QStringLiteral("B+ 2"), QStringLiteral("B+ 10"),
                                         QStringLiteral("Undated")}));
}

TEST_CASE("Presets of one date list by name regardless of case", "[presets][library]") {
    const auto ordered = library_order({preset("beta", "1985"), preset("Alpha", "1985"),
                                        preset("B+ 10", "1985"), preset("B+ 2", "1985")});
    CHECK(names(ordered) == QStringList({QStringLiteral("Alpha"), QStringLiteral("B+ 2"),
                                         QStringLiteral("B+ 10"), QStringLiteral("beta")}));
}

TEST_CASE("Duplicate suggests the first free copy name", "[presets][library]") {
    CHECK(duplicate_name(QStringLiteral("Elite"), {}) == QStringLiteral("Elite Copy"));
    CHECK(duplicate_name(QStringLiteral("Elite"), {QStringLiteral("Elite Copy")}) ==
          QStringLiteral("Elite Copy 2"));
    // A copy's copy is the base's next.
    CHECK(duplicate_name(QStringLiteral("Elite Copy"), {QStringLiteral("Elite Copy")}) ==
          QStringLiteral("Elite Copy 2"));
    CHECK(duplicate_name(QStringLiteral("Elite Copy 2"),
                         {QStringLiteral("Elite Copy"), QStringLiteral("Elite Copy 2")}) ==
          QStringLiteral("Elite Copy 3"));
}

TEST_CASE("The library's commands name the preset by its id", "[presets][library]") {
    const PresetListing source = preset("Model B (8271 Disc)", "1982");
    CHECK(duplicate_preset_arguments(source, QStringLiteral("Mine")) ==
          QStringList({QStringLiteral("create-preset"), QStringLiteral("--name"), QStringLiteral("Mine"),
                       QStringLiteral("--from"), source.id}));
    CHECK(export_preset_arguments(source, QStringLiteral("/home/me/x.beebium-preset")) ==
          QStringList({QStringLiteral("export-preset"), source.id, QStringLiteral("--output"),
                       QStringLiteral("/home/me/x.beebium-preset")}));
    CHECK(delete_preset_arguments(source) == QStringList({QStringLiteral("delete-preset"), source.id}));
}
