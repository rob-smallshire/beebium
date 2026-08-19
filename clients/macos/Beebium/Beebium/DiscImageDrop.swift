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

import SwiftUI
import UniformTypeIdentifiers

/// Why a drag hovering over a drive cannot be dropped on it.
///
/// Only reasons that can be known while the drag is still in the air, without
/// reading the file: its bytes are not available before the drop, and whether
/// it is a usable disc image is not the client's to decide. A running server
/// judges that on insert, and the preset editor by asking the server
/// executable; either way the answer comes after the drop, not during the
/// hover. So a hovering drag is refused only for the drive's state or the
/// server's location, never for what the file is.
enum DiscDropRefusal: Equatable {
    /// The drive already holds a disc.
    case slotOccupied(String)
    /// The server is on another host, so it cannot open a file from this one.
    case serverElsewhere

    var message: String {
        switch self {
        case .slotOccupied(let reason):
            return reason
        case .serverElsewhere:
            return "Server is on another host"
        }
    }
}

/// Whether a drag may be dropped, and if not, why not.
///
/// Judged from the drive's state and the server's location alone. What the
/// file is does not enter into it -- see DiscDropRefusal.
func discDropRefusal(isSlotEmpty: Bool,
                     isServerLocal: Bool,
                     occupiedReason: String) -> DiscDropRefusal? {
    if !isServerLocal {
        return .serverElsewhere
    }
    if !isSlotEmpty {
        return .slotOccupied(occupiedReason)
    }
    return nil
}

/// Drop handling for a drive slot.
///
/// The drag is judged only by what can be known without the file -- the
/// drive's state and the server's location -- so a droppable drag is accepted
/// on sight and the file is handed to `accept` on release. Whether it is a
/// usable disc image is decided downstream: a running server rejects a bad
/// image on insert with its own precise error, and the preset editor validates
/// through the server executable. Neither can be done here, because the file's
/// bytes are not available while the drag hovers.
struct DiscImageDropDelegate: DropDelegate {
    /// Whether the slot can take a disc right now.
    let isSlotEmpty: Bool
    /// What to say when it cannot, e.g. "Eject disc first".
    let occupiedReason: String
    /// Whether the server is on this host. A path is only meaningful to a
    /// process that shares this filesystem.
    var isServerLocal: Bool = true
    /// Set while a droppable drag hovers, for the target highlight.
    @Binding var isTargeted: Bool
    /// Set while a refused drag hovers, to show the reason.
    @Binding var refusal: DiscDropRefusal?
    /// Called on the main actor with a dropped file. Its suitability as a disc
    /// image is the caller's to judge.
    let accept: (URL) -> Void
    /// Called on the main actor when a drop cannot be carried out at all.
    /// Never let a failure pass without saying so.
    let report: (String) -> Void

    func validateDrop(info: DropInfo) -> Bool {
        // Claim any file drag so dropUpdated is consulted; the proposal it
        // returns is what permits or forbids the drop.
        info.hasItemsConforming(to: [.fileURL])
    }

    func dropEntered(info: DropInfo) {
        refresh()
    }

    func dropUpdated(info: DropInfo) -> DropProposal? {
        refresh()
        return DropProposal(operation: currentRefusal == nil ? .copy : .forbidden)
    }

    func dropExited(info: DropInfo) {
        setTargeted(false)
        setRefusal(nil)
    }

    func performDrop(info: DropInfo) -> Bool {
        setTargeted(false)
        setRefusal(nil)

        // The drive's state or the server may have changed since the drag
        // began; re-check what the hover checked.
        guard currentRefusal == nil else { return false }

        let providers = info.itemProviders(for: [.fileURL])
        guard providers.count == 1, let provider = providers.first else {
            // A drive holds one disc, so a drop of several files is ambiguous.
            if providers.count > 1 {
                report("Drop a single disc image")
            }
            return false
        }

        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier,
                          options: nil) { item, error in
            Task { @MainActor in
                if let error {
                    report("Could not read the dropped file: \(error.localizedDescription)")
                    return
                }
                guard let data = item as? Data,
                      let url = URL(dataRepresentation: data, relativeTo: nil) else {
                    report("Could not read the dropped file")
                    return
                }
                accept(url)
            }
        }
        return true
    }

    // MARK: - Refusal

    private var currentRefusal: DiscDropRefusal? {
        discDropRefusal(isSlotEmpty: isSlotEmpty,
                        isServerLocal: isServerLocal,
                        occupiedReason: occupiedReason)
    }

    private func refresh() {
        let reason = currentRefusal
        setRefusal(reason)
        setTargeted(reason == nil)
    }

    // Writing a binding on every pointer move would invalidate the view
    // continuously, so only write when the value actually changes.

    private func setRefusal(_ value: DiscDropRefusal?) {
        if refusal != value {
            refusal = value
        }
    }

    private func setTargeted(_ value: Bool) {
        if isTargeted != value {
            isTargeted = value
        }
    }
}
