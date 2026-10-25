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

#include <beebium/discovery/Advertiser.hpp>

namespace beebium::discovery {

namespace {

/// An advertiser for a platform or host without a working mDNS responder.
///
/// Always reports available=false, with the reason, and advertising=false.
/// start() reports the reason and fails; stop() has nothing to withdraw.
class NullAdvertiser final : public Advertiser {
public:
    explicit NullAdvertiser(std::string reason) : reason_(std::move(reason)) {}

    AdvertiserState state() const override {
        return AdvertiserState{
            .available = false,
            .advertising = false,
            .actual_name = {},
            .unavailable_reason = reason_,
        };
    }

private:
    std::string begin(const ServiceInfo& /*info*/) override { return reason_; }

    void withdraw() override {}

    std::string reason_;
};

}  // namespace

std::unique_ptr<Advertiser> make_null_advertiser(std::string reason) {
    return std::make_unique<NullAdvertiser>(std::move(reason));
}

// Factory function implementation for platforms without native mDNS.
// This is overridden by platform-specific implementations when available.
#if !defined(BEEBIUM_HAS_BONJOUR) && !defined(BEEBIUM_HAS_AVAHI) && \
    !defined(BEEBIUM_HAS_WINDOWS_MDNS)

std::unique_ptr<Advertiser> create_advertiser() {
    return make_null_advertiser("no mDNS support in this build");
}

#endif

}  // namespace beebium::discovery
