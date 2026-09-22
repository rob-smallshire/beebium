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

import Foundation
import GRPC

/// Live sideways-memory state for the running emulator, fed by
/// `SidewaysService.GetSlotStatus` and refreshed on `SubscribeEvents`.
@MainActor
final class SidewaysClient: ObservableObject, Disconnectable {

    enum SocketKind: Sendable { case empty, rom, ram }

    /// Parsed sideways ROM header for a slot's current contents, when one is
    /// recognised. Mirrors the gRPC RomHeader message and the describe-rom
    /// JSON (in the configurator path).
    struct RomHeader: Sendable, Equatable {
        let title: String
        let version: String
        let copyright: String
        let containsRomfs: Bool
        let kinds: [String]
    }

    /// One physical socket on the running machine.
    struct Socket: Identifiable, Sendable, Equatable {
        let socketIndex: UInt32
        let label: String
        let slots: [Int]
        let kind: SocketKind
        let populated: Bool
        let imageName: String
        let romHeader: RomHeader?

        var id: UInt32 { socketIndex }
        /// Effective boot priority: the highest slot the socket answers.
        var priority: Int { slots.max() ?? 0 }
    }

    /// Which access a protection group inhibits (mirrors the proto enum).
    enum ProtectionKind: Sendable {
        case writeProtect, hide
        var proto: Beebium_SidewaysProtectionKind {
            self == .writeProtect ? .writeProtect : .hide
        }
    }

    /// A named set of sideways slots whose protection is engaged as a whole by
    /// one board switch/link -- the single, board-agnostic model of sideways
    /// protection. A one-slot group (ATPL slot-15, a ROM/RAM board slot) behaves
    /// like a per-slot control; a whole-board group (Watford S2) moves together.
    struct ProtectionGroup: Identifiable, Sendable, Equatable {
        let id: String
        let label: String
        let slots: [Int]
        let supportsWriteProtect: Bool
        let supportsHide: Bool
        let writeProtected: Bool
        let hidden: Bool
    }

    @Published private(set) var sockets: [Socket] = []
    @Published private(set) var protectionGroups: [ProtectionGroup] = []
    @Published private(set) var hasAliasing: Bool = false
    @Published private(set) var isLoaded: Bool = false
    @Published private(set) var errorMessage: String?

    private var client: Beebium_SidewaysServiceNIOClient?
    private var subscriptionTask: Task<Void, Never>?

    /// Connect using an existing gRPC channel (same pattern as the other clients).
    /// Fetches GetSlotStatus once and then subscribes to SubscribeEvents, so the
    /// live view refreshes when a slot is reconfigured.
    func connect(channel: GRPCChannel) {
        client = Beebium_SidewaysServiceNIOClient(channel: channel)
        subscriptionTask = Task { [weak self] in
            await self?.fetchSlotStatus()
            await self?.subscribeEvents()
        }
    }

    func disconnect() {
        subscriptionTask?.cancel()
        subscriptionTask = nil
        client = nil
        sockets = []
        protectionGroups = []
        hasAliasing = false
        isLoaded = false
        errorMessage = nil
    }

    /// The protection group covering `socket` that offers the given kind of
    /// protection, if any. A group covers a socket when their slot sets overlap,
    /// so an aliased Model B socket and a whole-board Watford group both match.
    func group(forSocket socket: Socket, kind: ProtectionKind) -> ProtectionGroup? {
        Self.group(in: protectionGroups, forSocket: socket, kind: kind)
    }

    /// Pure lookup so the covering rule is unit-testable without a live client.
    static func group(in groups: [ProtectionGroup], forSocket socket: Socket,
                      kind: ProtectionKind) -> ProtectionGroup? {
        let socketSlots = Set(socket.slots)
        return groups.first { group in
            let offersKind = kind == .writeProtect ? group.supportsWriteProtect : group.supportsHide
            return offersKind && !socketSlots.isDisjoint(with: Set(group.slots))
        }
    }

    // MARK: - Fetch + subscribe

    private func fetchSlotStatus() async {
        guard let client = client else { return }
        do {
            let response = try await client.getSlotStatus(.init()).response.get()
            let updated = response.sockets.map(Self.mapSocket)
            let groups = response.protectionGroups.map(Self.mapGroup)
            await MainActor.run {
                // Highest priority (highest slot) first - mirrors the MOS scan
                // and the New Machine dialog's Memory tab ordering.
                self.sockets = updated.sorted { $0.priority > $1.priority }
                self.protectionGroups = groups
                self.hasAliasing = response.hasAliasing_p
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Sideways status failed: \(error.localizedDescription)"
            }
        }
    }

    // MARK: - Slot protection (write-protect / hide, by group)

    /// Engage or release a protection group's write-protect or hide switch. The
    /// switch acts on the whole group, so on success we re-fetch GetSlotStatus to
    /// reflect every covered slot together (the RPC has no push event). On
    /// failure the reason is surfaced via `errorMessage`.
    func setSlotProtection(groupID: String, kind: ProtectionKind, engaged: Bool) async {
        guard let client = client else { return }
        var request = Beebium_SetSlotProtectionRequest()
        request.groupID = groupID
        request.kind = kind.proto
        request.engaged = engaged
        do {
            let response = try await client.setSlotProtection(request).response.get()
            switch Self.protectionOutcome(success: response.success,
                                          error: response.error, groupID: groupID) {
            case .applied:
                await fetchSlotStatus()
                errorMessage = nil
            case .rejected(let reason):
                errorMessage = reason
            }
        } catch {
            errorMessage = "Protection change failed: \(error.localizedDescription)"
        }
    }

    /// How a `SetSlotProtection` response maps to an outcome. Pure so the
    /// success / rejected / empty-error branches are unit testable.
    enum ProtectionOutcome: Equatable {
        case applied            // re-fetch to reflect every covered slot
        case rejected(String)   // surface this reason via errorMessage
    }

    static func protectionOutcome(success: Bool, error: String, groupID: String) -> ProtectionOutcome {
        guard success else {
            return .rejected(error.isEmpty
                ? "Could not change protection for \(groupID)."
                : error)
        }
        return .applied
    }

    private func subscribeEvents() async {
        guard let client = client else { return }
        var request = Beebium_SubscribeEventsRequest()
        request.minIntervalMs = 100
        // Opt into the live header scanner so *SRLOAD (and any other
        // CPU write to sideways RAM) shows up in the sidebar without
        // re-fetching the whole GetSlotStatus snapshot. The flag is
        // off on the wire by default precisely to make this an
        // intentional choice on the client side.
        request.monitorHeaderChanges = true

        let call = client.subscribeEvents(request) { [weak self] event in
            Task { @MainActor [weak self] in
                self?.handleEvent(event)
            }
        }
        do {
            _ = try await call.status.get()
        } catch {
            // Stream ended (disconnect or server shutdown). Not an error worth
            // surfacing - the host will tear us down via disconnect().
        }
    }

    private func handleEvent(_ event: Beebium_SidewaysEvent) {
        guard let payload = event.event else { return }
        switch payload {
        case .slotConfigured:
            // A slot was reconfigured via ConfigureSlot: re-fetch so the
            // parsed rom_header reflects the new contents.
            Task { [weak self] in await self?.fetchSlotStatus() }
        case .slotHeaderChanged(let change):
            // The live scanner detected that a RAM slot's parsed header
            // changed (e.g. *SRLOAD into sideways RAM). Patch the
            // affected socket's romHeader in place rather than doing a
            // full re-fetch.
            applyHeaderChange(slot: Int(change.slot),
                              header: change.hasRomHeader
                                ? Self.mapHeader(change.romHeader)
                                : nil)
        }
    }

    private func applyHeaderChange(slot: Int, header: RomHeader?) {
        guard let socketIndex = sockets.firstIndex(where: { $0.slots.contains(slot) }) else {
            return
        }
        let existing = sockets[socketIndex]
        sockets[socketIndex] = Socket(
            socketIndex: existing.socketIndex,
            label: existing.label,
            slots: existing.slots,
            kind: existing.kind,
            populated: existing.populated || header != nil,
            imageName: existing.imageName,
            romHeader: header)
    }

    // MARK: - Proto -> Swift mapping

    private static func mapSocket(_ status: Beebium_SocketStatus) -> Socket {
        Socket(
            socketIndex: status.socketIndex,
            label: status.socketLabel,
            slots: status.aliasedSlots.map(Int.init),
            kind: mapKind(status.type),
            populated: status.populated,
            imageName: status.imageName,
            romHeader: status.hasRomHeader ? mapHeader(status.romHeader) : nil)
    }

    private static func mapGroup(_ g: Beebium_SidewaysProtectionGroup) -> ProtectionGroup {
        ProtectionGroup(
            id: g.id,
            label: g.label,
            slots: g.slots.map(Int.init),
            supportsWriteProtect: g.supportsWriteProtect,
            supportsHide: g.supportsHide,
            writeProtected: g.writeProtected,
            hidden: g.hidden)
    }

    private static func mapKind(_ type: Beebium_SidewaysSlotType) -> SocketKind {
        switch type {
        case .rom: return .rom
        case .ram: return .ram
        default:   return .empty
        }
    }

    private static func mapHeader(_ h: Beebium_RomHeader) -> RomHeader? {
        guard h.recognised else { return nil }
        return RomHeader(
            title: h.title,
            version: h.version,
            copyright: h.copyright,
            containsRomfs: h.containsRomfs,
            kinds: h.kinds)
    }
}
