// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Integration tests: AcornScsiHostAdapter through the ModelBHardware memory map.
// Verifies that 6502-style reads/writes to 0xFC40-0xFC43 correctly interact
// with the SCSI bus protocol.

#include <catch2/catch_test_macros.hpp>
#include "test_extension_ids.hpp"

#include <beebium/ModelBHardware.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/ExtensionRegistry.hpp>
#include <beebium/indicators/Indicators.hpp>
#include <AcornScsiHostAdapter.hpp>
#include <ScsiTestDevice.hpp>
#include <ScsiConstants.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

using namespace beebium;
using namespace beebium::scsi;

namespace {

struct ScsiIntegrationFixture {
    ModelBHardware hw;
    ExtensionRegistry registry;
    AcornScsiHostAdapter* adapter = nullptr;

    ScsiIntegrationFixture() {
        registry.register_extension_point("1mhz-bus");
        auto ext = AcornScsiHostAdapter::create();
        adapter = ext.get();
        registry.register_extension(with_default_id(std::move(ext)));

        ExtensionContext ctx(&hw.one_mhz_bus(), nullptr, nullptr, &hw.indicators);
        registry.resolve_and_init(ctx);
    }

    ~ScsiIntegrationFixture() {
        registry.shutdown();
    }

    void install_test_device(uint8_t id) {
        auto dev = std::make_unique<ScsiTestDevice>();
        dev->set_present(true);
        adapter->target_registry().install(id, std::move(dev));
        adapter->target_registry().wire_to_bus(adapter->bus());
    }

    // Drive a complete TEST UNIT READY transaction on the given target ID
    // through the memory map. The bus fires its activity callback on/off
    // around this transaction.
    void run_test_unit_ready(uint8_t target_id) {
        hw.write(0xFC40, static_cast<uint8_t>(1 << target_id));
        hw.write(0xFC42, 0x00);
        hw.write(0xFC40, OP_TEST_UNIT_READY);
        for (int i = 0; i < 5; ++i) hw.write(0xFC40, 0x00);
        hw.read(0xFC40);  // status
        hw.read(0xFC40);  // message
    }
};

}  // namespace

TEST_CASE("AcornScsiHostAdapter accessible through ModelBHardware",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;

    // Status register should be readable at 0xFC41
    REQUIRE(f.hw.read(0xFC41) == SR_REQ);  // BUS_FREE: only REQ set (Acorn adapter quirk)
}

TEST_CASE("AcornScsiHostAdapter claims correct address range",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;

    // Adjacent addresses should return open bus (0xFF)
    REQUIRE(f.hw.read(0xFC3F) == 0xFF);
    REQUIRE(f.hw.read(0xFC44) == 0xFF);
}

TEST_CASE("AcornScsiHostAdapter name and extension points",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;

    REQUIRE(f.adapter->name() == "acorn-scsi");
    REQUIRE(f.adapter->attaches_to().size() == 1);
    REQUIRE(f.adapter->attaches_to()[0] == "1mhz-bus");
    REQUIRE(f.adapter->provides().size() == 1);
    REQUIRE(f.adapter->provides()[0] == "scsi");
}

TEST_CASE("AcornScsiHostAdapter complete TEST UNIT READY through memory map",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;
    f.install_test_device(0);

    // Select target 0: write target ID then trigger selection
    f.hw.write(0xFC40, 0x01);  // write target ID
    f.hw.write(0xFC42, 0x00);  // trigger Selection → Command

    // Now in COMMAND phase -- write 6-byte TEST UNIT READY CDB
    f.hw.write(0xFC40, OP_TEST_UNIT_READY);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);

    // Should be in STATUS phase
    uint8_t sr = f.hw.read(0xFC41);
    REQUIRE((sr & SR_CD) != 0);
    REQUIRE((sr & SR_IO) != 0);

    // Read status byte
    uint8_t status = f.hw.read(0xFC40);
    REQUIRE(status == STATUS_GOOD);

    // Read message byte
    uint8_t message = f.hw.read(0xFC40);
    REQUIRE(message == MSG_COMMAND_COMPLETE);

    // Back to BUS_FREE
    REQUIRE(f.hw.read(0xFC41) == SR_REQ);  // BUS_FREE
}

TEST_CASE("AcornScsiHostAdapter READ(6) through memory map",
          "[scsi][integration]") {
    ScsiIntegrationFixture f;
    f.install_test_device(0);

    // Configure test device with known data
    auto* dev = static_cast<ScsiTestDevice*>(f.adapter->target_registry().target(0));
    std::vector<uint8_t> sector(256);
    for (int i = 0; i < 256; ++i) sector[i] = static_cast<uint8_t>(i);
    dev->set_read_data(sector);

    // Select target 0
    f.hw.write(0xFC40, 0x01);
    f.hw.write(0xFC42, 0x00);

    // Write READ(6) CDB: 1 block from LBA 0
    f.hw.write(0xFC40, OP_READ_6);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x00);
    f.hw.write(0xFC40, 0x01);  // 1 block
    f.hw.write(0xFC40, 0x00);

    // Should be in DATA_IN -- read 256 bytes
    uint8_t sr = f.hw.read(0xFC41);
    REQUIRE((sr & SR_IO) != 0);  // Data In
    REQUIRE((sr & SR_CD) == 0);  // Data, not command

    for (int i = 0; i < 256; ++i) {
        uint8_t byte = f.hw.read(0xFC40);
        REQUIRE(byte == static_cast<uint8_t>(i));
    }

    // Status + Message + BUS_FREE
    f.hw.read(0xFC40);  // status
    f.hw.read(0xFC40);  // message
    REQUIRE(f.hw.read(0xFC41) == SR_REQ);  // BUS_FREE (only REQ set)
}

// ---------------------------------------------------------------------------
// Per-LUN activity indicators
// ---------------------------------------------------------------------------

TEST_CASE("AcornScsiHostAdapter register_target_indicator registers the indicator",
          "[scsi][integration][indicators]") {
    ScsiIntegrationFixture f;
    f.adapter->register_target_indicator(
        0, "hdd-0-activity-led",
        {{"label", "HDD 0"}, {"color", "rgb(255,191,0)"}, {"shape", "rectangular"}});

    auto names = f.hw.indicators.names();
    REQUIRE(std::find(names.begin(), names.end(), "hdd-0-activity-led") != names.end());

    auto meta = f.hw.indicators.metadata("hdd-0-activity-led");
    REQUIRE(meta["label"] == "HDD 0");
    REQUIRE(meta["color"] == "rgb(255,191,0)");
    REQUIRE(meta["shape"] == "rectangular");
}

TEST_CASE("AcornScsiHostAdapter SCSI activity drives the registered LUN indicator",
          "[scsi][integration][indicators]") {
    ScsiIntegrationFixture f;
    f.install_test_device(0);
    f.adapter->register_target_indicator(0, "hdd-0-activity-led", {});

    f.run_test_unit_ready(0);

    // The bus fired (lun=0, true) and (lun=0, false) around the transaction.
    // The RetriggerableMonostable filter holds the LED on for the configured
    // pulse width regardless of the off event, so immediately after the
    // transaction the published value should be 255.
    f.hw.indicators.process_pending();
    REQUIRE(f.hw.indicators.get("hdd-0-activity-led") == 255);

    // After the pulse window expires (real wall clock), it must drop to 0.
    // The pulse width (kActivityPulseWidth in AcornScsiHostAdapter) is 250 ms
    // so wait comfortably past it before sampling.
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    f.hw.indicators.process_pending();
    REQUIRE(f.hw.indicators.get("hdd-0-activity-led") == 0);
}

TEST_CASE("AcornScsiHostAdapter activity for unregistered LUN is silently dropped",
          "[scsi][integration][indicators]") {
    ScsiIntegrationFixture f;
    f.install_test_device(2);  // present at LUN 2, but no indicator registered

    f.run_test_unit_ready(2);  // must not crash

    f.hw.indicators.process_pending();
    // No hdd-* indicator should exist; the keyboard LEDs registered by
    // ModelBHardware are unrelated.
    auto names = f.hw.indicators.names();
    bool any_hdd = std::any_of(names.begin(), names.end(),
        [](const std::string& n) { return n.starts_with("hdd-"); });
    REQUIRE_FALSE(any_hdd);
}

TEST_CASE("AcornScsiHostAdapter activity does not bleed across LUNs",
          "[scsi][integration][indicators]") {
    ScsiIntegrationFixture f;
    f.install_test_device(0);
    f.install_test_device(3);
    f.adapter->register_target_indicator(0, "hdd-0-activity-led", {});
    f.adapter->register_target_indicator(3, "hdd-3-activity-led", {});

    f.run_test_unit_ready(0);

    f.hw.indicators.process_pending();
    REQUIRE(f.hw.indicators.get("hdd-0-activity-led") == 255);
    REQUIRE(f.hw.indicators.get("hdd-3-activity-led") == 0);
}
