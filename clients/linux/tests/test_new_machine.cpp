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

// Which presets the presets page offers (client specification 5.1).

#include "app/PresetsPage.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

PresetListing preset(const char* id, const char* source, const char* model) {
    PresetListing p;
    p.id = QString::fromLatin1(id);
    p.source = QString::fromLatin1(source);
    p.model = QString::fromLatin1(model);
    return p;
}

}  // namespace

TEST_CASE("Presets whose server is absent are omitted, built-in first", "[presets]") {
    const std::vector<PresetListing> listed{
        preset("mine", "user", "model-b"),
        preset("model-b", "system", "model-b"),
        preset("integra", "system", "model-b-integra-b"),
        preset("theirs", "user", "model-b-plus"),
    };
    const auto launchable = launchable_presets(listed, [](const QString& model) {
        return model != QStringLiteral("model-b-integra-b");
    });
    REQUIRE(launchable.size() == 3);
    CHECK(launchable[0].id == "model-b");
    CHECK(launchable[1].id == "mine");
    CHECK(launchable[2].id == "theirs");
}
