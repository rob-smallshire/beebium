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

// check_attachments: before any extension is loaded, whether each one has
// somewhere to attach, within the point's occupancy, with its provider-naming
// and unique-per-attachment parameters consistent.

#include <beebium/extension/AttachmentPlan.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace beebium;
using Catch::Matchers::ContainsSubstring;

namespace {

// A SCSI host adapter on the 1 MHz bus providing "scsi", a SCSI disc that
// attaches to it, a serial device and a second serial device.
const std::vector<ExtensionManifest>& manifests() {
    static const std::vector<ExtensionManifest> kManifests = [] {
        ExtensionManifest adapter;
        adapter.name = "scsi-adapter";
        adapter.attaches_to = {"1mhz-bus"};
        adapter.provides = {"scsi"};

        ExtensionManifest disc;
        disc.name = "scsi-disc";
        disc.attaches_to = {"scsi"};
        ParameterSchema scsi_id{"scsi-id", "integer", "SCSI target ID", 0, false, false, "0"};
        scsi_id.unique = true;
        ParameterSchema adapter_id{"adapter-id", "string", "Adapter", -1, false, false, ""};
        adapter_id.provider_of = "scsi";
        disc.parameters = {scsi_id, adapter_id};

        ExtensionManifest serial;
        serial.name = "serial-thing";
        serial.attaches_to = {"serial-port"};

        return std::vector<ExtensionManifest>{adapter, disc, serial};
    }();
    return kManifests;
}

// Every extension the server knows, as the resolver lists them.
const std::vector<const ExtensionManifest*>& known() {
    static const std::vector<const ExtensionManifest*> kKnown = [] {
        std::vector<const ExtensionManifest*> v;
        for (const auto& m : manifests()) v.push_back(&m);
        return v;
    }();
    return kKnown;
}

const std::vector<std::string> kMachinePoints{"1mhz-bus", "user-port", "printer-port", "tube",
                                              "serial-port"};

}  // namespace

TEST_CASE("check_attachments: an adapter and its discs attach", "[extension][attachment-plan]") {
    auto problems = check_attachments({{"scsi-adapter", {}},
                                       {"scsi-disc", {{"scsi-id", "0"}}},
                                       {"scsi-disc", {{"scsi-id", "1"}}}},
                                      known(), kMachinePoints);
    CHECK(problems.empty());
}

TEST_CASE("check_attachments: a disc with no adapter has nowhere to attach",
          "[extension][attachment-plan]") {
    auto problems = check_attachments({{"scsi-disc", {{"scsi-id", "0"}}}}, known(), kMachinePoints);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].instance == 0);
    CHECK(problems[0].key.empty());
    CHECK_THAT(problems[0].message, ContainsSubstring("scsi"));
    CHECK_THAT(problems[0].message, ContainsSubstring("scsi-adapter"));  // what would provide it
}

TEST_CASE("check_attachments: two discs with one scsi-id on one adapter clash",
          "[extension][attachment-plan]") {
    auto problems = check_attachments({{"scsi-adapter", {}},
                                       {"scsi-disc", {{"scsi-id", "3"}}},
                                       {"scsi-disc", {{"scsi-id", "3"}}}},
                                      known(), kMachinePoints);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].instance == 2);  // the second one is reported
    CHECK(problems[0].key == "scsi-id");
    CHECK_THAT(problems[0].message, ContainsSubstring("3"));
}

TEST_CASE("check_attachments: one scsi-id on two adapters is fine",
          "[extension][attachment-plan]") {
    auto problems = check_attachments({{"scsi-adapter", {{"id", "a"}}},
                                       {"scsi-adapter", {{"id", "b"}}},
                                       {"scsi-disc", {{"scsi-id", "0"}, {"adapter-id", "a"}}},
                                       {"scsi-disc", {{"scsi-id", "0"}, {"adapter-id", "b"}}}},
                                      known(), {"1mhz-bus"});
    CHECK(problems.empty());
}

TEST_CASE("check_attachments: an adapter-id naming no adapter is an error",
          "[extension][attachment-plan]") {
    auto problems = check_attachments({{"scsi-adapter", {{"id", "a"}}},
                                       {"scsi-disc", {{"scsi-id", "0"}, {"adapter-id", "zz"}}}},
                                      known(), kMachinePoints);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].instance == 1);
    CHECK(problems[0].key == "adapter-id");
    CHECK_THAT(problems[0].message, ContainsSubstring("zz"));
}

TEST_CASE("check_attachments: a disc without an adapter-id goes on the last adapter",
          "[extension][attachment-plan]") {
    // The runtime's default provider is the last one registered, so two
    // id-less discs with one scsi-id clash there even with two adapters.
    auto problems = check_attachments({{"scsi-adapter", {{"id", "a"}}},
                                       {"scsi-adapter", {{"id", "b"}}},
                                       {"scsi-disc", {{"scsi-id", "0"}, {"adapter-id", "b"}}},
                                       {"scsi-disc", {{"scsi-id", "0"}}}},
                                      known(), kMachinePoints);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].instance == 3);
}

TEST_CASE("check_attachments: a single-occupancy point holds one extension",
          "[extension][attachment-plan]") {
    auto problems = check_attachments({{"serial-thing", {}}, {"serial-thing", {}}}, known(),
                                      kMachinePoints);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].instance == 1);
    CHECK_THAT(problems[0].message, ContainsSubstring("Serial Port"));
}

TEST_CASE("check_attachments: a point the machine lacks", "[extension][attachment-plan]") {
    auto problems = check_attachments({{"serial-thing", {}}}, known(), {"1mhz-bus"});
    REQUIRE(problems.size() == 1);
    CHECK_THAT(problems[0].message, ContainsSubstring("serial-port"));
}

TEST_CASE("check_attachments: an unknown extension is left to the name check",
          "[extension][attachment-plan]") {
    CHECK(check_attachments({{"no-such", {}}}, known(), kMachinePoints).empty());
}

// ---------------------------------------------------------------------------
// A placement among alternatives: attaches_to_one_of and an instance's `at`
// (a coprocessor on the Tube or the Master's internal Tube).
// ---------------------------------------------------------------------------

namespace {

ExtensionManifest coprocessor_manifest() {
    ExtensionManifest coprocessor;
    coprocessor.name = "coprocessor";
    coprocessor.attaches_to_one_of = {"tube", "internal-tube"};
    return coprocessor;
}

}  // namespace

TEST_CASE("A one-of manifest places an instance at its at, else the first alternative",
          "[extension][attachment-plan][placement]") {
    const auto m = coprocessor_manifest();
    CHECK(m.placement("") == std::optional<std::string>{"tube"});
    CHECK(m.placement("internal-tube") == std::optional<std::string>{"internal-tube"});
    CHECK_FALSE(m.placement("serial-port"));
    CHECK(m.points_for("") == std::vector<std::string>{"tube"});
    CHECK(m.points_for("internal-tube") == std::vector<std::string>{"internal-tube"});
    CHECK(m.all_points() == std::vector<std::string>{"tube", "internal-tube"});
    CHECK_FALSE(m.placement_error(""));
    CHECK_FALSE(m.placement_error("internal-tube"));
    REQUIRE(m.placement_error("serial-port"));
    CHECK_THAT(*m.placement_error("serial-port"), ContainsSubstring("tube, internal-tube"));

    ExtensionManifest plain;
    plain.name = "serial-thing";
    plain.attaches_to = {"serial-port"};
    CHECK_FALSE(plain.placement(""));
    CHECK(plain.points_for("") == std::vector<std::string>{"serial-port"});
    REQUIRE(plain.placement_error("tube"));
    CHECK_THAT(*plain.placement_error("tube"), ContainsSubstring("takes no at"));
}

TEST_CASE("check_attachments: a one-of instance attaches to its placement only",
          "[extension][attachment-plan][placement]") {
    const auto coprocessor = coprocessor_manifest();
    const std::vector<const ExtensionManifest*> with{&coprocessor};
    const std::vector<std::string> master{"tube", "internal-tube"};
    const std::vector<std::string> model_b{"tube"};

    CHECK(check_attachments({{"coprocessor", {}}}, with, master).empty());
    CHECK(check_attachments({{"coprocessor", {{"at", "internal-tube"}}}}, with, master).empty());
    CHECK(check_attachments({{"coprocessor", {}}}, with, model_b).empty());

    // A Model B has no internal Tube.
    auto problems = check_attachments({{"coprocessor", {{"at", "internal-tube"}}}}, with, model_b);
    REQUIRE(problems.size() == 1);
    CHECK_THAT(problems[0].message, ContainsSubstring("does not have"));

    // An at that is not an alternative.
    problems = check_attachments({{"coprocessor", {{"at", "user-port"}}}}, with, master);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].key == "at");

    // One on each Tube is within each Tube's occupancy; two on one is not.
    CHECK(check_attachments({{"coprocessor", {}}, {"coprocessor", {{"at", "internal-tube"}}}},
                            with, master).empty());
    CHECK(check_attachments({{"coprocessor", {}}, {"coprocessor", {{"at", "tube"}}}}, with, master)
              .size() == 1);
}
