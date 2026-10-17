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

// test_nvram_store.cpp
// Tests for keeping battery-backed units' contents in files (NvramStore.hpp):
// saved and restored, refused into another layout or hardware with the file
// kept aside, and forgotten.

#include <beebium/Machines.hpp>
#include <beebium/server/NvramStore.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "test_temp_paths.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace beebium;
using namespace beebium::server;
namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace {

// A unit holding a few bytes, with a layout and hardware a test can change.
class FakeUnit final : public BatteryBackedUnit {
public:
    explicit FakeUnit(std::string name) : name_(std::move(name)) {}
    std::string_view name() const override { return name_; }
    std::uint32_t layout_version() const override { return layout; }
    std::string hardware_descriptor() const override { return hardware; }
    void seed() override { contents = {0, 0, 0}; }
    std::vector<std::uint8_t> serialise() const override { return contents; }
    bool restore(std::span<const std::uint8_t> in) override {
        if (in.size() != 3) return false;
        contents.assign(in.begin(), in.end());
        return true;
    }

    std::uint32_t layout = 1;
    std::string hardware = "fake";
    std::vector<std::uint8_t> contents{0, 0, 0};

private:
    std::string name_;
};

std::size_t entry_count(const fs::path& dirpath) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dirpath)) ++count;
    return count;
}

}  // namespace

TEST_CASE("nvram: contents saved are restored, one file per unit", "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    FakeUnit a("unit-a");
    FakeUnit b("unit-b");
    a.contents = {1, 2, 3};
    b.contents = {4, 5, 6};
    save_battery_units({&a, &b}, dir / "nvram");
    CHECK(fs::exists(dir / "nvram" / "unit-a.bin"));
    CHECK(fs::exists(dir / "nvram" / "unit-b.bin"));

    a.seed();
    b.seed();
    std::ostringstream warnings;
    const auto restored = restore_battery_units({&a, &b}, dir / "nvram", warnings);
    CHECK(restored == std::vector<std::string>{"unit-a", "unit-b"});
    CHECK(a.contents == std::vector<std::uint8_t>{1, 2, 3});
    CHECK(b.contents == std::vector<std::uint8_t>{4, 5, 6});
    CHECK(warnings.str().empty());
}

TEST_CASE("nvram: a unit with no saved contents keeps its seed, silently", "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    FakeUnit a("unit-a");
    a.contents = {7, 7, 7};
    std::ostringstream warnings;
    CHECK(restore_battery_units({&a}, dir / "nvram", warnings).empty());
    CHECK(a.contents == std::vector<std::uint8_t>{7, 7, 7});
    CHECK(warnings.str().empty());
}

TEST_CASE("nvram: contents for another layout, other hardware or that do not fit are kept aside",
          "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    const auto nvram = dir / "nvram";
    FakeUnit saved("unit-a");
    saved.contents = {1, 2, 3};

    auto check_refused = [&](FakeUnit& reader, const std::string& why) {
        save_battery_units({&saved}, nvram);
        reader.contents = {9, 9, 9};
        std::ostringstream warnings;
        CHECK(restore_battery_units({&reader}, nvram, warnings).empty());
        CHECK(reader.contents == std::vector<std::uint8_t>{9, 9, 9});
        CHECK_THAT(warnings.str(), ContainsSubstring("unit-a starts afresh"));
        CHECK_THAT(warnings.str(), ContainsSubstring(why));
        CHECK_FALSE(fs::exists(nvram / "unit-a.bin"));
    };

    FakeUnit later_layout("unit-a");
    later_layout.layout = 2;
    check_refused(later_layout, "saved in layout 1, not 2");
    FakeUnit other_board("unit-a");
    other_board.hardware = "another board";
    check_refused(other_board, "saved for other hardware (fake)");

    // Not a saved unit at all.
    std::ofstream(nvram / "unit-a.bin") << "rubbish";
    FakeUnit reader("unit-a");
    std::ostringstream warnings;
    CHECK(restore_battery_units({&reader}, nvram, warnings).empty());
    CHECK_THAT(warnings.str(), ContainsSubstring("it is not a saved unit"));

    // Every refused file is kept, none overwritten.
    CHECK(entry_count(nvram / "stale") == 3);
}

TEST_CASE("nvram: contents cut short are kept aside", "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    const auto nvram = dir / "nvram";
    FakeUnit unit("unit-a");
    unit.contents = {1, 2, 3};
    save_battery_units({&unit}, nvram);
    fs::resize_file(nvram / "unit-a.bin", fs::file_size(nvram / "unit-a.bin") - 1);

    unit.contents = {9, 9, 9};
    std::ostringstream warnings;
    CHECK(restore_battery_units({&unit}, nvram, warnings).empty());
    CHECK(unit.contents == std::vector<std::uint8_t>{9, 9, 9});
    CHECK_THAT(warnings.str(), ContainsSubstring("unit-a starts afresh"));
    CHECK_THAT(warnings.str(), ContainsSubstring("it is cut short"));
    CHECK_FALSE(fs::exists(nvram / "unit-a.bin"));
    CHECK(entry_count(nvram / "stale") == 1);
}

TEST_CASE("nvram: contents the unit refuses are kept aside", "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    const auto nvram = dir / "nvram";
    // The right layout and hardware, but four bytes where the unit holds three.
    FakeUnit unit("unit-a");
    unit.contents = {1, 2, 3, 4};
    save_battery_units({&unit}, nvram);

    unit.contents = {9, 9, 9};
    std::ostringstream warnings;
    CHECK(restore_battery_units({&unit}, nvram, warnings).empty());
    CHECK(unit.contents == std::vector<std::uint8_t>{9, 9, 9});
    CHECK_THAT(warnings.str(), ContainsSubstring("unit-a starts afresh"));
    CHECK_THAT(warnings.str(), ContainsSubstring("the unit cannot hold it"));
    CHECK_FALSE(fs::exists(nvram / "unit-a.bin"));
    CHECK(entry_count(nvram / "stale") == 1);
}

TEST_CASE("nvram: forgetting removes every unit's contents", "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    FakeUnit a("unit-a");
    save_battery_units({&a}, dir / "nvram");
    forget_battery_units(dir / "nvram");
    CHECK_FALSE(fs::exists(dir / "nvram"));
    forget_battery_units(dir / "nvram");  // nothing to forget is not an error
}

TEST_CASE("nvram: an Integra-B's board RAM and clock outlive the machine", "[nvram]") {
    beebium::test::ScopedTempDir dir("beebium_nvram_");
    {
        auto machine = std::make_unique<ModelBIntegraB>();
        machine->state().memory.shadow_and_private_ram.write(0x3456, 0x77);
        machine->state().memory.sideways.write_bank(7, 0x0010, 0x88);
        save_battery_units(machine->battery_units(), dir / "nvram");
    }
    auto machine = std::make_unique<ModelBIntegraB>();
    CHECK(machine->state().memory.shadow_and_private_ram.read(0x3456) == 0x00);
    const auto restored = restore_battery_units(machine->battery_units(), dir / "nvram");
    CHECK(restored == std::vector<std::string>{"integra-b-board-ram", "integra-b-rtc"});
    CHECK(machine->state().memory.shadow_and_private_ram.read(0x3456) == 0x77);
    CHECK(machine->state().memory.sideways.peek_bank(7, 0x0010) == 0x88);
}
