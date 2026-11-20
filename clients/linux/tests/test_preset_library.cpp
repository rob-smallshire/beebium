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
// macOS client's Settings > Machines has them, and the catalogue's search
// (client specification 5.1).

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

PresetListing indexed(const char* name, const char* description,
                      std::vector<std::pair<QString, QStringList>> facets, QStringList keywords = {}) {
    PresetListing p = preset(name, "");
    p.description = QString::fromUtf8(description);
    p.facets = std::move(facets);
    p.keywords = std::move(keywords);
    return p;
}

std::vector<std::pair<QString, QStringList>> facets(const char* model, const char* coprocessor,
                                                    const char* disc_controller, const char* network,
                                                    QStringList storage, QStringList peripherals = {}) {
    return {{QStringLiteral("model"), {QString::fromUtf8(model)}},
            {QStringLiteral("coprocessor"), {QString::fromUtf8(coprocessor)}},
            {QStringLiteral("disc_controller"), {QString::fromUtf8(disc_controller)}},
            {QStringLiteral("network"), {QString::fromUtf8(network)}},
            {QStringLiteral("storage"), std::move(storage)},
            {QStringLiteral("peripherals"), std::move(peripherals)}};
}

// A catalogue as the server lists it, each row with its facets.
std::vector<PresetListing> catalogue() {
    const QString floppy = QStringLiteral("Floppy");
    const QString cassette = QStringLiteral("Cassette");
    return {
        indexed("Master 128", "BBC Master 128, MOS 3.20",
                facets("Master 128", "None", "1770", "None", {floppy, cassette}), {QStringLiteral("BBC")}),
        indexed("Master 128, internal 65C102 Copro (4 MHz)", "Master 128, 65C102 copro on the internal Tube",
                facets("Master 128", "65C102", "1770", "None", {floppy, cassette}),
                {QStringLiteral("BBC"), QStringLiteral("6502")}),
        indexed("Model B+ 128K, 65C02 Copro (3 MHz)", "128K B+, 65C02 copro",
                facets("Model B+ 128K", "65C02", "1770", "None", {floppy, cassette}),
                {QStringLiteral("BBC"), QStringLiteral("6502")}),
        indexed("Model B+ with Music 5000", "B+ and the Hybrid Music 5000",
                facets("Model B+", "None", "1770", "None", {floppy, cassette}, {QStringLiteral("Music 5000")})),
        indexed("Station 80 (AUN, Model B)", "Model B on AUN as station 80",
                facets("Model B", "None", "1770", "AUN", {floppy, cassette})),
        indexed("L3 File Server", "Level 3 file server on AUN",
                facets("Model B", "None", "8271", "AUN", {floppy, QStringLiteral("Hard disc")},
                       {QStringLiteral("SCSI adapter")})),
    };
}

QStringList found(const char* query) {
    QStringList out;
    for (const auto& p : catalogue()) {
        if (preset_matches(p, QString::fromUtf8(query))) {
            out << p.name;
        }
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

TEST_CASE("An empty search matches every preset", "[presets][search]") {
    CHECK(found("").size() == 6);
    CHECK(found("   ").size() == 6);
}

TEST_CASE("Every word of a search must match somewhere", "[presets][search]") {
    // A facet value and a name word.
    CHECK(found("master copro") == QStringList{QStringLiteral("Master 128, internal 65C102 Copro (4 MHz)")});
    // Two facet values.
    CHECK(found("aun 1770") == QStringList{QStringLiteral("Station 80 (AUN, Model B)")});
    CHECK(found("aun") .size() == 2);
}

TEST_CASE("A search ignores case", "[presets][search]") {
    CHECK(found("MASTER 65c102") == found("master 65C102"));
    CHECK(found("master 65c102").size() == 1);
}

TEST_CASE("A search splits on whitespace only, so b+ is literal", "[presets][search]") {
    CHECK(found("b+ music") == QStringList{QStringLiteral("Model B+ with Music 5000")});
    CHECK(found("b+").size() == 2);
}

TEST_CASE("A search matches facet values a name and description do not mention", "[presets][search]") {
    CHECK(found("scsi") == QStringList{QStringLiteral("L3 File Server")});
    CHECK(found("hard disc") == QStringList{QStringLiteral("L3 File Server")});
    CHECK(found("8271") == QStringList{QStringLiteral("L3 File Server")});
    CHECK(found("l3") == QStringList{QStringLiteral("L3 File Server")});
}

TEST_CASE("A search matches the server's keywords too", "[presets][search]") {
    CHECK(found("6502 copro") == QStringList{QStringLiteral("Master 128, internal 65C102 Copro (4 MHz)"),
                                             QStringLiteral("Model B+ 128K, 65C02 Copro (3 MHz)")});
    CHECK(found("bbc").size() == 3);
}

TEST_CASE("A search no preset matches finds none", "[presets][search]") {
    CHECK(found("electron").isEmpty());
    CHECK(found("master aun").isEmpty());
}
