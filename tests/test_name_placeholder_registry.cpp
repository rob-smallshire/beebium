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

// The name-placeholder registry (#153): providers, the domain rule, and the
// extension hook.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "beebium/NamePlaceholderRegistry.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/NamePlaceholderProvider.hpp"

#include <map>
#include <string>
#include <vector>

using namespace beebium;
using Catch::Matchers::ContainsSubstring;

namespace {

// A provider with fixed domains, placeholders and values.
class FakeProvider : public NamePlaceholderProvider {
public:
    FakeProvider(std::vector<std::string> domains, std::vector<NamePlaceholderInfo> infos,
                 std::map<std::string, NamePlaceholderValue> values = {})
        : domains_(std::move(domains)), infos_(std::move(infos)), values_(std::move(values)) {}

    std::vector<std::string> placeholder_domains() const override { return domains_; }
    std::vector<NamePlaceholderInfo> placeholders() const override { return infos_; }
    NamePlaceholderValue placeholder_value(std::string_view key) const override {
        auto it = values_.find(std::string(key));
        return it == values_.end() ? NamePlaceholderValue{"", false} : it->second;
    }

    std::map<std::string, NamePlaceholderValue> values_;

private:
    std::vector<std::string> domains_;
    std::vector<NamePlaceholderInfo> infos_;
};

NamePlaceholderInfo info(std::string key, std::string group = "Test") {
    return {key, "Label of " + key, "Describes " + key, std::move(group)};
}

}  // namespace

TEST_CASE("A registry lists its providers' placeholders with values and insertion text",
          "[name][placeholders]") {
    FakeProvider machine({"machine"}, {info("machine-model", "Machine")},
                         {{"machine-model", {"BBC Model B", true}}});
    FakeProvider econet({"econet"}, {info("econet-station", "Econet"), info("econet-net", "Econet")},
                        {{"econet-station", {"80", true}}, {"econet-net", {"", false}}});

    NamePlaceholderRegistry registry;
    registry.add(machine, "machine");
    registry.add(econet, "econet socket");

    auto all = registry.snapshot();
    REQUIRE(all.size() == 3);
    CHECK(all[0].key == "machine-model");
    CHECK(all[0].group == "Machine");
    CHECK(all[0].label == "Label of machine-model");
    CHECK(all[0].description == "Describes machine-model");
    CHECK(all[0].value == "BBC Model B");
    CHECK(all[0].applicable);
    CHECK(all[0].insertion == "{machine-model}");
    CHECK(all[1].key == "econet-station");
    CHECK(all[1].value == "80");
    CHECK(all[2].key == "econet-net");
    CHECK_FALSE(all[2].applicable);
    CHECK(all[2].value.empty());
}

TEST_CASE("A registry renders templates against current values", "[name][placeholders]") {
    FakeProvider econet({"econet"}, {info("econet-station"), info("econet-net")},
                        {{"econet-station", {"80", true}}, {"econet-net", {"", false}}});
    NamePlaceholderRegistry registry;
    registry.add(econet, "econet socket");

    auto r = registry.render("Station {econet-station} net[{econet-net}] {econet-port}");
    CHECK(r.text == "Station 80 net[] {econet-port}");
    CHECK(r.unknown_keys == std::vector<std::string>{"econet-port"});
    CHECK(r.inapplicable_keys == std::vector<std::string>{"econet-net"});

    // Values are read at render time, not at registration.
    econet.values_["econet-station"] = {"81", true};
    CHECK(registry.render("{econet-station}").text == "81");
}

TEST_CASE("A key outside its provider's domains is refused", "[name][placeholders]") {
    NamePlaceholderRegistry registry;

    FakeProvider bare({"econet"}, {info("station")});
    CHECK_THROWS_WITH(registry.add(bare, "bare"),
                      ContainsSubstring("station") && ContainsSubstring("bare") &&
                      ContainsSubstring("econet"));

    // Starting with the domain's letters is not enough: the domain is a word.
    FakeProvider prefix({"econet"}, {info("econetwork-name")});
    CHECK_THROWS(registry.add(prefix, "prefix"));

    // Nor is the domain alone.
    FakeProvider domain_only({"econet"}, {info("econet")});
    CHECK_THROWS(registry.add(domain_only, "domain-only"));

    FakeProvider foreign({"scsi"}, {info("machine-model")});
    CHECK_THROWS(registry.add(foreign, "foreign"));

    CHECK(registry.snapshot().empty());
}

TEST_CASE("A domain belongs to one provider", "[name][placeholders]") {
    FakeProvider first({"econet"}, {info("econet-station")});
    FakeProvider second({"econet"}, {info("econet-port")});
    NamePlaceholderRegistry registry;
    registry.add(first, "first");
    CHECK_THROWS_WITH(registry.add(second, "second"),
                      ContainsSubstring("econet") && ContainsSubstring("first"));
    CHECK(registry.snapshot().size() == 1);
}

TEST_CASE("Malformed domains and keys are refused", "[name][placeholders]") {
    NamePlaceholderRegistry registry;
    FakeProvider hyphenated({"aun-map"}, {info("aun-map-file")});
    CHECK_THROWS(registry.add(hyphenated, "hyphenated"));
    FakeProvider upper({"Econet"}, {info("Econet-station")});
    CHECK_THROWS(registry.add(upper, "upper"));
    FakeProvider bad_key({"econet"}, {info("econet-Station")});
    CHECK_THROWS(registry.add(bad_key, "bad-key"));
    FakeProvider no_domains({}, {info("econet-station")});
    CHECK_THROWS(registry.add(no_domains, "no-domains"));
    FakeProvider duplicate({"econet"}, {info("econet-station"), info("econet-station")});
    CHECK_THROWS(registry.add(duplicate, "duplicate"));
    CHECK(registry.snapshot().empty());
}

TEST_CASE("A refused provider leaves the registry unchanged", "[name][placeholders]") {
    FakeProvider good({"machine"}, {info("machine-model")});
    FakeProvider half_bad({"econet"}, {info("econet-station"), info("station")});
    NamePlaceholderRegistry registry;
    registry.add(good, "good");
    CHECK_THROWS(registry.add(half_bad, "half-bad"));
    auto all = registry.snapshot();
    REQUIRE(all.size() == 1);
    CHECK(all[0].key == "machine-model");
    // Its domain was not claimed either.
    FakeProvider later({"econet"}, {info("econet-station")});
    CHECK_NOTHROW(registry.add(later, "later"));
}

namespace {

// A test extension contributing a placeholder through the extension hook.
class GizmoExtension : public Extension, public NamePlaceholderProvider {
public:
    GizmoExtension() {
        ExtensionManifest manifest;
        manifest.name = "gizmo";
        set_manifest(std::move(manifest));
        set_config_value("id", "gizmo-1");
    }
    const NamePlaceholderProvider* name_placeholders() const override { return this; }

    std::vector<std::string> placeholder_domains() const override { return {"gizmo"}; }
    std::vector<NamePlaceholderInfo> placeholders() const override {
        return {{"gizmo-colour", "Gizmo colour", "The gizmo's colour.", "Gizmo"}};
    }
    NamePlaceholderValue placeholder_value(std::string_view) const override {
        return {"green", true};
    }
};

class PlainExtension : public Extension {
public:
    PlainExtension() {
        ExtensionManifest manifest;
        manifest.name = "plain";
        set_manifest(std::move(manifest));
    }
};

}  // namespace

TEST_CASE("An extension contributes placeholders through its hook", "[name][placeholders]") {
    GizmoExtension gizmo;
    PlainExtension plain;
    NamePlaceholderRegistry registry;
    registry.add_extension(gizmo);
    registry.add_extension(plain);  // no placeholders: nothing to add

    auto all = registry.snapshot();
    REQUIRE(all.size() == 1);
    CHECK(all[0].key == "gizmo-colour");
    CHECK(all[0].group == "Gizmo");
    CHECK(registry.render("{gizmo-colour} box").text == "green box");
}

TEST_CASE("An extension's placeholders obey the domain rule too", "[name][placeholders]") {
    class Rogue : public GizmoExtension {
    public:
        std::vector<NamePlaceholderInfo> placeholders() const override {
            return {{"machine-model", "Model", "Claims a core key.", "Gizmo"}};
        }
    };
    Rogue rogue;
    NamePlaceholderRegistry registry;
    CHECK_THROWS_WITH(registry.add_extension(rogue), ContainsSubstring("gizmo-1"));
}
