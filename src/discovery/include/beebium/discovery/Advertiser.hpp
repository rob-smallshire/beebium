// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_DISCOVERY_ADVERTISER_HPP
#define BEEBIUM_DISCOVERY_ADVERTISER_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace beebium::discovery {

/// Information about the service to advertise.
struct ServiceInfo {
    /// Display name for the service (becomes DNS-SD instance name).
    std::string instance_name;

    /// DNS-SD service type. Defaults to "_beebium._tcp" for the gRPC
    /// machine service. AUN peer discovery uses the vendor-neutral
    /// "_aun._udp" instead so other AUN implementations (BeebEm,
    /// PiEconetBridge, real Acorn hardware with AUN clients) can share
    /// the announcement namespace.
    std::string service_type = "_beebium._tcp";

    /// Port the gRPC server is listening on.
    uint16_t port;

    /// TXT record key-value pairs (e.g., uuid, model, provenance, version).
    std::map<std::string, std::string> txt_records;
};

/// Current state of the advertiser.
struct AdvertiserState {
    /// True if the platform has mDNS support (Bonjour/Avahi/Windows mDNS).
    bool available = false;

    /// True if currently advertising.
    bool advertising = false;

    /// The actual registered name. May differ from requested instance_name
    /// if DNS-SD resolved a name collision (e.g., "Machine" -> "Machine (2)").
    std::string actual_name;

    /// When not available, why not, e.g. "avahi client library not found".
    std::string unavailable_reason;
};

/// Receives one line, without its newline, for each thing an advertiser does
/// on the network: a registration completed (under the name the responder
/// gave it), a registration that could not be made or failed, and a
/// withdrawal. Called on whichever thread the event happened on.
using AdvertiserReporter = std::function<void(const std::string& line)>;

/// The reporter a server uses: writes the line to stderr.
void report_to_stderr(const std::string& line);

/// Interface for DNS-SD service advertisement.
///
/// Implementations handle platform-specific mDNS registration:
/// - macOS: dns_sd.h (Bonjour)
/// - Linux: Avahi
/// - Windows: windns.h
///
/// Use create_advertiser() to get the appropriate implementation for the
/// current platform, or NullAdvertiser if mDNS is unavailable.
///
/// The advertiser says what it did through its reporter, if it has one; an
/// implementation calls registered() and registration_failed() from its
/// registration callback and the base class words the lines.
class Advertiser {
public:
    virtual ~Advertiser() = default;

    /// Start advertising the service.
    ///
    /// @param info Service information to advertise.
    /// @return true if registration began, false if mDNS is unavailable or
    ///         the registration could not be made. Registration completes
    ///         asynchronously; state().advertising says when it has.
    ///
    /// If already advertising, replaces the current advertisement with one
    /// for the updated info.
    bool start(const ServiceInfo& info);

    /// Stop advertising. Safe to call if not currently advertising.
    void stop();

    /// Get current advertiser state.
    virtual AdvertiserState state() const = 0;

    /// Report what the advertiser does to `reporter`. Without one it is
    /// silent.
    void set_reporter(AdvertiserReporter reporter);

    // Non-copyable
    Advertiser(const Advertiser&) = delete;
    Advertiser& operator=(const Advertiser&) = delete;

protected:
    Advertiser() = default;

    /// Begin registering `info`; nothing is registered when this is called.
    /// Returns empty when registration has begun, or why it could not.
    virtual std::string begin(const ServiceInfo& info) = 0;

    /// Withdraw the registration, if any, and return once no registration
    /// callback can still run.
    virtual void withdraw() = 0;

    /// From the registration callback: registered under `actual_name`, which
    /// the responder may have changed from the name asked for.
    void registered(const std::string& actual_name);

    /// From the registration callback: the registration failed, or cannot
    /// complete for now, for `reason`.
    void registration_failed(const std::string& reason);

private:
    void report(const std::string& line) const;  // with mutex_ held

    mutable std::mutex mutex_;
    AdvertiserReporter reporter_;
    ServiceInfo info_;              // as last started
    std::string registered_name_;   // empty until registered
    std::string replacing_name_;    // the registration a start() replaced
};

/// An advertiser that cannot advertise, for `reason`: state().available is
/// false and start() reports the reason and returns false.
std::unique_ptr<Advertiser> make_null_advertiser(std::string reason);

/// Create a platform-appropriate advertiser.
///
/// Returns:
/// - BonjourAdvertiser on macOS
/// - AvahiAdvertiser on Linux (if Avahi is available)
/// - WindowsAdvertiser on Windows (if mDNS is available)
/// - NullAdvertiser if no mDNS support is available
///
/// The returned advertiser's state().available indicates whether
/// mDNS is actually functional on this system.
std::unique_ptr<Advertiser> create_advertiser();

}  // namespace beebium::discovery

#endif  // BEEBIUM_DISCOVERY_ADVERTISER_HPP
