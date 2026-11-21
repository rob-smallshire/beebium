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

/// Classifies a sideways socket the server marks as cartridge-provided
/// (`provided_by_point`) against a draft's extensions: is a cartridge fitted at
/// that point, and if so what capabilities does it give the bank?
///
/// The socket exists only when a cartridge is fitted -- its own capabilities are
/// only "empty" -- so the Memory section cannot decide a cartridge socket's row
/// from the sideways schema alone. This pure projection joins the sideways schema
/// to the extensions schema (for the cartridge manifest's `provides_sideways`)
/// and the draft's extension instances, so the view reads a decided state rather
/// than re-deriving one. See spec 5.9 "Sockets a cartridge provides", #300/#301.
struct CartridgeSockets {
    let extensionsSchema: ExtensionsSchemaSection

    /// What a sideways socket is, once the draft's cartridges are taken into
    /// account.
    enum SocketState: Equatable {
        /// An ordinary motherboard socket: not provided by a cartridge point.
        /// The Memory row behaves exactly as before.
        case ordinary
        /// A cartridge point with no cartridge fitted. The row reads "No
        /// cartridge", offers no image browse or type change, and points the user
        /// to `point` under Peripherals.
        case cartridgeAbsent(point: String, pointName: String)
        /// A cartridge fitted at the point: the bank is an ordinary socket with
        /// `bankCapabilities`, and the row names the cartridge.
        case cartridgePresent(point: String, pointName: String,
                              cartridgeName: String, cartridgeDisplayName: String,
                              bankCapabilities: [String])
    }

    private var instanceModel: ExtensionInstanceModel {
        ExtensionInstanceModel(schema: extensionsSchema)
    }

    /// The state of a socket for the given draft. A socket with no
    /// `provided_by_point` is always `.ordinary`, so this never changes a
    /// motherboard socket's behaviour.
    func state(of socket: SidewaysSocketSchema, in draft: MachineDraft) -> SocketState {
        guard let point = socket.providedByPoint else { return .ordinary }
        let pointName = pointDisplayName(point)
        guard let cartridge = fittedCartridge(atPoint: point, in: draft) else {
            return .cartridgeAbsent(point: point, pointName: pointName)
        }
        let capabilities = extensionsSchema
            .providesSideways(forExtension: cartridge.name)?.bankCapabilities ?? []
        return .cartridgePresent(
            point: point, pointName: pointName,
            cartridgeName: cartridge.name,
            cartridgeDisplayName: extensionsSchema.displayName(forExtension: cartridge.name),
            bankCapabilities: capabilities)
    }

    /// The cartridge fitted at `point`: the first instance there whose extension
    /// provides sideways sockets (a ROM cartridge). nil when the point is empty
    /// or holds only extensions that provide none.
    func fittedCartridge(atPoint point: String,
                         in draft: MachineDraft) -> ExtensionInstanceModel.Instance? {
        instanceModel.instances(onPoint: point, in: draft).first { instance in
            extensionsSchema.providesSideways(forExtension: instance.name) != nil
        }
    }

    /// The display name of an attachment point (e.g. "Rear Cartridge Slot"),
    /// falling back to its id.
    func pointDisplayName(_ point: String) -> String {
        extensionsSchema.attachmentPoints.first { $0.id == point }?.displayName ?? point
    }
}
