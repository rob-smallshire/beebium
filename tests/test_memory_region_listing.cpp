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

// Every machine's memory region listing says of each sideways bank what a
// probe of it finds: writable when a written byte reads back, populated when
// it holds a ROM or RAM. And the by-name lookup that bounds a region access
// agrees with the listing on where each region lies.

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <beebium/ModelBAtplSidewiseHardware.hpp>
#include <beebium/ModelBHardware.hpp>
#include <beebium/ModelBIntegraBHardware.hpp>
#include <beebium/ModelBPlus128KHardware.hpp>
#include <beebium/ModelBPlusHardware.hpp>
#include <beebium/ModelBRomRamBoardHardware.hpp>
#include <beebium/ModelBWatfordRomRamHardware.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace beebium;

namespace {

// 16 KiB that is nowhere 0xFF, so a probe can tell it from an empty socket.
std::vector<uint8_t> rom_image() {
    std::vector<uint8_t> image(0x4000);
    for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i % 0xFF);
    return image;
}

// What a probe finds in a bank: whether a byte written to it reads back
// (restored afterwards), and whether anything in it reads other than 0xFF.
struct Probe {
    bool writable;
    bool populated;
};

template <typename Hardware>
Probe probe(Hardware& hw, const std::string& bank) {
    constexpr uint32_t address = 0x9234;
    const uint8_t before = hw.peek_region(bank, address);
    hw.write_region(bank, address, static_cast<uint8_t>(before ^ 0xFF));
    const bool writable = hw.peek_region(bank, address) == static_cast<uint8_t>(before ^ 0xFF);
    hw.write_region(bank, address, before);
    REQUIRE(hw.peek_region(bank, address) == before);

    bool content = false;
    for (uint32_t a = 0x8000; a < 0xC000 && !content; ++a) {
        content = hw.peek_region(bank, a) != 0xFF;
    }
    return {writable, writable || content};
}

// Wired as the server wires it before loading ROMs (load_roms): on the B+
// the default links leave IC71's BASIC at slots 14 and 15 only.
template <typename Hardware>
void apply_default_links(Hardware& hw) {
    if constexpr (requires(const typename Hardware::MotherboardLinks& links) {
                      hw.apply_motherboard_links(links);
                  }) {
        hw.apply_motherboard_links(typename Hardware::MotherboardLinks{});
    }
}

template <typename Hardware>
std::vector<MemoryRegionDescriptor> banks_of(const Hardware& hw) {
    std::vector<MemoryRegionDescriptor> banks;
    for (const auto& region : hw.get_memory_regions()) {
        if (std::string(region.name).rfind("bank_", 0) == 0) banks.push_back(region);
    }
    return banks;
}

}  // namespace

TEMPLATE_TEST_CASE("The region listing says of each sideways bank what a probe finds",
                   "[memory_regions]", ModelBHardware, ModelBPlusHardware,
                   ModelBPlus128KHardware, ModelBRomRamBoardHardware,
                   ModelBAtplSidewiseHardware, ModelBWatfordRomRamHardware,
                   ModelBIntegraBHardware) {
    TestType hw;
    apply_default_links(hw);
    const auto image = rom_image();
    hw.load_basic(image.data(), image.size());
    hw.load_sideways_rom(4, image.data(), image.size(), "probe.rom");

    const auto banks = banks_of(hw);
    REQUIRE(banks.size() == 16);
    for (const auto& bank : banks) {
        const std::string name(bank.name);
        CAPTURE(name);
        const auto found = probe(hw, name);
        CHECK(has_flag(bank.flags, RegionFlags::Writable) == found.writable);
        CHECK(has_flag(bank.flags, RegionFlags::Populated) == found.populated);
    }
}

TEMPLATE_TEST_CASE("The by-name lookup agrees with the listing on where each region lies",
                   "[memory_regions]", ModelBHardware, ModelBPlusHardware,
                   ModelBPlus128KHardware, ModelBRomRamBoardHardware,
                   ModelBAtplSidewiseHardware, ModelBWatfordRomRamHardware,
                   ModelBIntegraBHardware) {
    TestType hw;
    for (const auto& region : hw.get_memory_regions()) {
        CAPTURE(region.name);
        const auto extent = hw.region_extent(region.name);
        REQUIRE(extent.has_value());
        CHECK(extent->name == region.name);
        CHECK(extent->base_address == region.base_address);
        CHECK(extent->size == region.size);
    }
    CHECK_FALSE(hw.region_extent("bank_16").has_value());
}

TEST_CASE("A stock B+ has nothing at slots 12 and 13; the 128K has SRAM there",
          "[memory_regions]") {
    auto flags_of = [](const auto& hw, std::string_view name) {
        for (const auto& region : hw.get_memory_regions()) {
            if (region.name == name) return region.flags;
        }
        FAIL("no region " << name);
        return RegionFlags::None;
    };
    ModelBPlusHardware b_plus;
    ModelBPlus128KHardware b_plus_128k;
    apply_default_links(b_plus);
    apply_default_links(b_plus_128k);
    for (const auto* name : {"bank_12", "bank_13"}) {
        CAPTURE(name);
        CHECK_FALSE(has_flag(flags_of(b_plus, name), RegionFlags::Populated));
        CHECK_FALSE(has_flag(flags_of(b_plus, name), RegionFlags::Writable));
        CHECK(has_flag(flags_of(b_plus_128k, name), RegionFlags::Populated));
        CHECK(has_flag(flags_of(b_plus_128k, name), RegionFlags::Writable));
    }
    // An empty user socket half is not populated on either.
    CHECK_FALSE(has_flag(flags_of(b_plus, "bank_2"), RegionFlags::Populated));
    CHECK_FALSE(has_flag(flags_of(b_plus_128k, "bank_2"), RegionFlags::Populated));
}
