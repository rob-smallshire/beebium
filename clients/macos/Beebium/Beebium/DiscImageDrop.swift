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
/// Every refusal carries a reason the user can read. A drop that is going to
/// be ignored has to say so while the drag is still in the air, because once
/// the pointer is released there is nothing left to explain.
enum DiscDropRefusal: Equatable {
    /// More than one file is being dragged.
    case severalFiles
    /// The file is not one of the formats a drive can load.
    case notADiscImage
    /// The drive already holds a disc.
    case slotOccupied(String)

    var message: String {
        switch self {
        case .severalFiles:
            return "Drop one disc image"
        case .notADiscImage:
            return "Not a disc image"
        case .slotOccupied(let reason):
            return reason
        }
    }
}

/// What a hovering drag appears to be carrying.
///
/// Named separately from `DropInfo` so the decision below can be exercised
/// without a live drag session.
enum DiscDropCandidate: Equatable {
    case none
    case one(isDiscImage: Bool)
    case several
}

/// Whether a drag may be dropped, and if not, why not.
///
/// The file's own suitability is judged before the state of the drive: told
/// "eject disc first", a user would reasonably expect that ejecting makes the
/// drop work, which is not true of a file that was never a disc image.
func discDropRefusal(for candidate: DiscDropCandidate,
                     isSlotEmpty: Bool,
                     occupiedReason: String) -> DiscDropRefusal? {
    switch candidate {
    case .none:
        return .notADiscImage
    case .several:
        return .severalFiles
    case .one(let isDiscImage):
        if !isDiscImage {
            return .notADiscImage
        }
        return isSlotEmpty ? nil : .slotOccupied(occupiedReason)
    }
}

/// Drop handling for a drive slot: one disc image, onto an empty slot, or
/// nothing happens and the user is told why.
///
/// Validation is up front rather than after the fact. `dropUpdated` answers
/// with a forbidden proposal for anything that will not be accepted, so the
/// pointer shows the refusal and macOS animates the file back to where it
/// came from, and `performDrop` is never reached.
struct DiscImageDropDelegate: DropDelegate {
    /// Whether the slot can take a disc right now.
    let isSlotEmpty: Bool
    /// What to say when it cannot, e.g. "Eject disc first".
    let occupiedReason: String
    /// Set while a droppable drag hovers, for the target highlight.
    @Binding var isTargeted: Bool
    /// Set while a refused drag hovers, to show the reason.
    @Binding var refusal: DiscDropRefusal?
    /// Called on the main actor with an accepted disc image.
    let accept: (URL) -> Void
    /// Called on the main actor when a drop that passed validation still
    /// could not be read. Never let a failure pass without saying so.
    let report: (String) -> Void

    func validateDrop(info: DropInfo) -> Bool {
        // Claim any file drag so that dropUpdated is consulted at all; the
        // proposal returned there is what permits or forbids the drop.
        info.hasItemsConforming(to: [.fileURL])
    }

    func dropEntered(info: DropInfo) {
        update(with: info)
    }

    func dropUpdated(info: DropInfo) -> DropProposal? {
        update(with: info)
        return DropProposal(operation: refusal == nil ? .copy : .forbidden)
    }

    func dropExited(info: DropInfo) {
        setTargeted(false)
        setRefusal(nil)
    }

    func performDrop(info: DropInfo) -> Bool {
        setTargeted(false)
        setRefusal(nil)

        guard currentRefusal(for: info) == nil,
              let provider = info.itemProviders(for: [.fileURL]).first else {
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
                // Hovering could only go on what the drag advertised about
                // itself; this is the first sight of the actual file.
                guard DiscImageTypes.isDiscImage(url) else {
                    report("\(url.lastPathComponent) is not a disc image")
                    return
                }
                accept(url)
            }
        }
        return true
    }

    // MARK: - Validation

    private func currentRefusal(for info: DropInfo) -> DiscDropRefusal? {
        discDropRefusal(for: candidate(in: info),
                        isSlotEmpty: isSlotEmpty,
                        occupiedReason: occupiedReason)
    }

    private func candidate(in info: DropInfo) -> DiscDropCandidate {
        let providers = info.itemProviders(for: [.fileURL])
        switch providers.count {
        case 0:
            return .none
        case 1:
            return .one(isDiscImage: looksLikeDiscImage(providers[0]))
        default:
            return .several
        }
    }

    private func looksLikeDiscImage(_ provider: NSItemProvider) -> Bool {
        if DiscImageTypes.contentTypes.contains(where: {
            provider.hasItemConformingToTypeIdentifier($0.identifier)
        }) {
            return true
        }
        // Finder does not always advertise a content type for extensions
        // macOS has no declaration for, so fall back to the name it offers.
        if let name = provider.suggestedName {
            return DiscImageTypes.isDiscImage(URL(fileURLWithPath: name))
        }
        return false
    }

    private func update(with info: DropInfo) {
        let reason = currentRefusal(for: info)
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
