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

#ifndef BEEBIUM_ECONET_AUN_DISCOVERY_ANNOUNCER_HPP
#define BEEBIUM_ECONET_AUN_DISCOVERY_ANNOUNCER_HPP

// Publishes a DNS-SD announcement for this AUN station so other
// AUN-capable peers (Beebium, BeebEm, PiEconetBridge, future
// implementations) can discover it without manual --aun map=
// configuration.
//
// Service type is the vendor-neutral "_aun._udp"; see
// docs/discussion/aun-mdns-peer-discovery.md for the rationale and TXT
// record schema. The schema is:
//   version       "1"           (mandatory; protocol-version key)
//   net           "<n>"         (mandatory; absolute Econet net number)
//   station       "<n>"         (mandatory; Econet station number)
//   port          "<n>"         (mandatory; UDP port mirror of SRV.port)
//   impl          "beebium"     (optional; identifies the implementation)
//   impl-version  "<version>"   (optional; build/release version string)
//   impl-identity "<opaque>"    (optional; per-instance identifier.
//                                Vendor-neutral key; the value's
//                                format is scoped by impl=, so each
//                                implementation picks what makes sense
//                                for it. Beebium populates this with
//                                the same machine UUID published as
//                                "uuid" on _beebium._tcp, so a
//                                discovering tool can correlate the
//                                two announcements; another vendor
//                                might use a hostname, a hash, or
//                                nothing at all.)
//   since         "<unix secs>" (optional; when this station bound its
//                                socket -- i.e. first claimed this
//                                number. Two instances racing for one
//                                number compare it to decide which is
//                                the incumbent: the earlier `since`
//                                wins, ties break on impl-identity, and
//                                a peer with no `since` counts as
//                                earlier. A station change refreshes it,
//                                because it is a fresh claim. See #147.)
//
// Lifetime is RAII-shaped: construction stores parameters, start()
// publishes the announcement (using a real platform Advertiser unless
// one was injected for testing), stop() / destruction tears it down.

#include <beebium/discovery/Advertiser.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace beebium {

class AunDiscoveryAnnouncer {
public:
    // Construct with the values that populate the TXT record and the
    // SRV port. impl identifies the implementation in the TXT record
    // (defaults to "beebium"); impl_version is a free-form version
    // string from the caller (BEEBIUM_VERSION at the call site).
    //
    // If advertiser is null, the constructor allocates a platform
    // advertiser via discovery::create_advertiser(). Tests inject a
    // double to capture the ServiceInfo without touching the real
    // mDNS responder.
    AunDiscoveryAnnouncer(std::uint8_t local_net,
                          std::uint8_t local_stn,
                          std::uint16_t local_port,
                          std::string impl,
                          std::string impl_version,
                          std::string impl_identity,
                          std::unique_ptr<discovery::Advertiser> advertiser = nullptr);

    ~AunDiscoveryAnnouncer();

    // Non-copyable; the underlying advertiser owns OS-level state.
    AunDiscoveryAnnouncer(const AunDiscoveryAnnouncer&) = delete;
    AunDiscoveryAnnouncer& operator=(const AunDiscoveryAnnouncer&) = delete;

    // Publish the announcement. Returns true if the platform accepted
    // the registration (the actual mDNS appearance happens
    // asynchronously). Returns false if mDNS is unavailable on this
    // platform (the announcer becomes a no-op for the rest of its
    // life). Calling start() twice is idempotent: it stops any
    // existing announcement and re-publishes with the current values.
    bool start();

    // Withdraw the announcement. Safe to call before start() or after
    // stop(); also called by the destructor.
    void stop();

    // Update the station this announcer advertises. Does NOT re-publish on its
    // own: the caller follows with start() (idempotent) to withdraw the old
    // station's announcement and publish the new one. Used when the guest's
    // station number is changed at runtime (EconetService::SetStationId), so
    // peers stop seeing the stale station. The instance name embeds the
    // station, so this changes the DNS-SD name as well as the TXT record.
    void set_local_station(std::uint8_t local_stn) { local_stn_ = local_stn; }

    // Set the bind-time "since" published in the TXT record (unix seconds).
    // The transport sets this to the moment it bound the socket, and refreshes
    // it on a station change (a fresh claim). The same value is given to the
    // subscriber, so both halves agree on who is the incumbent in a race for a
    // number (see #147). Defaults to construction time if never set.
    void set_since(std::int64_t since_unix_seconds) {
        since_ = since_unix_seconds;
    }
    std::int64_t since() const { return since_; }

    // True between a successful start() and a stop() / destruction.
    // Reflects the platform advertiser's view, which may take a few
    // tens of milliseconds to update on macOS Bonjour.
    bool is_advertising() const;

    // Test-only: inspect the ServiceInfo that would be published given
    // the current configuration. The instance_name follows the form
    // "Beebium <impl-version> <net>.<stn>" so multiple Beebium
    // instances on the same LAN can be told apart by humans browsing
    // _aun._udp.
    discovery::ServiceInfo build_service_info() const;

    // Override the DNS-SD service type (default "_aun._udp"). Production
    // always uses the default; this exists so real-mDNS tests can run on
    // a per-test-unique type and stay isolated from any stray _aun._udp
    // records on the machine. Call before start().
    void set_service_type(std::string service_type) {
        service_type_ = std::move(service_type);
    }

private:
    std::uint8_t local_net_;
    std::uint8_t local_stn_;
    std::uint16_t local_port_;
    std::string impl_;
    std::string impl_version_;
    std::string impl_identity_;
    std::int64_t since_;  // unix seconds the socket was bound (this claim)
    std::string service_type_ = "_aun._udp";
    std::unique_ptr<discovery::Advertiser> advertiser_;
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_DISCOVERY_ANNOUNCER_HPP
