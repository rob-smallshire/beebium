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

#ifndef BEEBIUM_EXTENSION_ECONET_TRANSPORT_EXTENSION_HPP
#define BEEBIUM_EXTENSION_ECONET_TRANSPORT_EXTENSION_HPP

#include "Export.hpp"
#include "Extension.hpp"
#include "../econet/NetworkBackend.hpp"
#include "../econet/StationSelection.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

class EconetStatusResponse;  // forward decl; defined in econet.proto

// Base class for Econet transport extensions: extensions that produce a
// NetworkBackend implementation for EconetSocket. Identity (manifest,
// config, name, id, label) is inherited from Extension; this class adds
// the transport-construction lifecycle.
//
// Each transport extension provides exactly one NetworkBackend per
// machine instance (e.g. one AUN UDP socket, one Piconet USB device).
// The future Acorn-Econet-Bridge machine type would have two transport
// extensions, one per ADLC -- nothing here precludes that.
class BEEBIUM_EXT_API EconetTransportExtension : public Extension {
public:
    // Out-of-line destructor anchors the vtable in beebium_extension_api
    // (required by MSVC for dllimport-decorated polymorphic classes).
    ~EconetTransportExtension() override;

    // Construct the backend. Called once per machine after extension
    // config is set, before EconetSocket::enable(). Returns nullptr on
    // construction failure (e.g. socket bind failed, device unavailable);
    // the caller decides what to do (typically print error and exit).
    virtual std::unique_ptr<NetworkBackend> create_backend(uint8_t station) = 0;

    // Forward station-ID changes (e.g. from gRPC SetStationId) so the
    // transport can update any of its own bookkeeping. Default no-op;
    // transports that need to push the change to a downstream device
    // (Piconet's SET_STATION command) override. Defined out-of-line in
    // EconetTransportExtension.cpp.
    virtual void on_station_id_changed(uint8_t new_station_id);

    // Whether this transport requires the emulation to run at real time (1x).
    // Default false: transports with no shared real-world clock (e.g. AUN over
    // UDP) work at any speed. A transport bridging to a real Econet line with
    // real stations (Piconet) overrides to true; the server then gates that
    // transport off while the speed is not 1x, since its protocol timing cannot
    // interleave with real peers in wall time at any other rate. See
    // docs/networking.md ("Emulation speed and real-time peers"). Defined
    // out-of-line in EconetTransportExtension.cpp.
    virtual bool requires_real_time_pacing() const;

    // Validate the current config, returning a human-readable reason (no
    // "Error:" prefix) when it is invalid, else nullopt. The server calls this
    // at machine assembly, before create_backend, so a bad parameter value is a
    // clear launch error rather than a silent fallback. Default: always valid.
    virtual std::optional<std::string> config_error() const { return std::nullopt; }

    // A transport's client-facing API (e.g. AUN peer-list RPCs) is served
    // through the core's ExtensionRpc channel via rpc_dispatchers() (declared
    // on Extension), not by hosting a gRPC service here.

    // The result of choosing a station number automatically (issue #67).
    struct AutoStationOutcome {
        enum class Status {
            Selected,     // `station` is a free number this transport claimed
            Exhausted,    // the whole range was in use; `station` is range.lo
            Unsupported,  // this transport cannot choose a number for itself
        };
        Status status = Status::Unsupported;
        std::uint8_t station = 0;  // meaningful for Selected and Exhausted
        std::string report;        // human-readable note for the non-Selected cases
    };

    // Choose a free station number in `range` at launch, BEFORE create_backend
    // and before the Econet socket is enabled, so the guest reads its final
    // number at its first boot and never needs a Break. Bounded in time and run
    // on the launch path, never the emulation thread. The default is
    // Unsupported: a transport with no peers to consult (Piconet bridges a real
    // wire) cannot pick a number, and the caller reports that --station auto is
    // not available for it. AUN overrides this with a browse-and-claim. Defined
    // out-of-line in EconetTransportExtension.cpp.
    virtual AutoStationOutcome select_auto_station(econet::StationRange range);

protected:
    // Wire `backend`'s destroyed notification so `teardown` runs once when
    // EconetSocket frees the backend, giving every transport the same safe
    // drop-the-pointer mechanism (#167; the #55 fix generalised). `teardown`
    // supplies the transport's own work: an identity check against its typed
    // backend pointer, nulling it, detaching peer sets, and joining any
    // background thread it owns. The base holds the liveness token so the
    // teardown is a no-op once this extension is destroyed (the gRPC server is
    // stopped before either is torn down, but destruction order between the
    // extension and a reader-co-owned backend is not fixed). The token is fresh
    // per arm, so an earlier backend's destruction, deferred past a re-Enable,
    // sees an expired token and does nothing -- alongside the identity check
    // the teardown itself makes. Call once, in create_backend, right after the
    // backend is built.
    void arm_backend_destroyed(NetworkBackend* backend,
                               std::function<void()> teardown);

    // Expire the armed token so a still-armed backend's destroyed callback
    // becomes a no-op. A derived destructor MUST call this first thing, because
    // this token lives in the base and so outlives the derived members the
    // teardown touches: an extension-owned backend (e.g. AUN's
    // preselected_backend_) destroyed during derived teardown would otherwise
    // run the teardown against members (mutexes, the peer set) already gone.
    void disarm_backend_destroyed() { backend_destroyed_token_.reset(); }

private:
    // Liveness token for arm_backend_destroyed's callback, replaced on each arm.
    std::shared_ptr<bool> backend_destroyed_token_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ECONET_TRANSPORT_EXTENSION_HPP
