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

/// Projects a model's hard-disc capability over a draft's extension instances
/// into the rows the Storage "Hard discs" group shows, and writes edits back
/// into `draft.extensions`.
///
/// Like SidewaysSocketEditor, it stores nothing: a disc is an extension instance
/// in the draft (`{name: <discExtension>, config: {scsi-id, image, adapter-id?}}`)
/// and the adapter is another instance (`{name: <adapterExtension>}`). The
/// adapter is the user's means, not their goal, so adding the first disc adds an
/// adapter and removing the last disc removes the orphaned adapter.
struct ScsiDiscEditor {
    let capability: HardDiscCapability

    /// One hard-disc row.
    struct Disc: Identifiable {
        /// Index of the disc's instance in `draft.extensions` (stable address
        /// for edits; distinct from the displayed SCSI ID).
        let instanceIndex: Int
        var scsiId: Int
        /// Bare name (a bundled master, copied on use), or a file path.
        var image: String?
        /// Which adapter instance this disc attaches to (only set with >1 adapter).
        var adapterId: String?

        var id: Int { instanceIndex }

        /// A bare image name is a bundled master; a path (has "/") is a file.
        var isBundledMaster: Bool {
            guard let image = image, !image.isEmpty else { return false }
            return !image.contains("/")
        }

        /// Filename for display, or nil when no image is set.
        var displayName: String? {
            guard let image = image, !image.isEmpty else { return nil }
            return image.contains("/") ? URL(fileURLWithPath: image).lastPathComponent : image
        }
    }

    // MARK: - Projection

    /// The hard discs in the draft, sorted by SCSI ID for display.
    func discs(in draft: MachineDraft) -> [Disc] {
        let extensions = draft.extensions ?? []
        return extensions.enumerated().compactMap { index, ext in
            guard ext.name == capability.discExtension else { return nil }
            return Disc(
                instanceIndex: index,
                scsiId: ext.configInt("scsi-id") ?? 0,
                image: ext.configString("image"),
                adapterId: ext.configString("adapter-id"))
        }
        .sorted { $0.scsiId < $1.scsiId }
    }

    /// The indices of adapter instances in the draft.
    func adapterIndices(in draft: MachineDraft) -> [Int] {
        (draft.extensions ?? []).enumerated()
            .filter { $0.element.name == capability.adapterExtension }
            .map(\.offset)
    }

    /// The lowest SCSI ID 0-7 not already used by a disc.
    func nextFreeScsiId(in draft: MachineDraft) -> Int {
        let used = Set(discs(in: draft).map(\.scsiId))
        return (0...7).first { !used.contains($0) } ?? 0
    }

    // MARK: - Write-through

    /// Add a hard disc (with the next free SCSI ID and no image yet), adding an
    /// adapter first if the draft has none.
    func draft(addingDiscTo draft: MachineDraft) -> MachineDraft {
        var result = draft
        var extensions = result.extensions ?? []
        if adapterIndices(in: result).isEmpty {
            extensions.append(MachineDraft.Extension(name: capability.adapterExtension))
        }
        let scsiId = nextFreeScsiId(in: result)
        extensions.append(MachineDraft.Extension(
            name: capability.discExtension,
            config: ["scsi-id": .int(scsiId)]))
        result.extensions = extensions
        return result
    }

    /// Remove the disc at `instanceIndex`. If no discs remain afterwards, the
    /// adapters have nothing attached, so remove them too.
    func draft(removingDiscAt instanceIndex: Int, from draft: MachineDraft) -> MachineDraft {
        var result = draft
        guard var extensions = result.extensions, extensions.indices.contains(instanceIndex),
              extensions[instanceIndex].name == capability.discExtension else { return draft }
        extensions.remove(at: instanceIndex)
        let discsRemain = extensions.contains { $0.name == capability.discExtension }
        if !discsRemain {
            extensions.removeAll { $0.name == capability.adapterExtension }
        }
        result.extensions = extensions
        return result
    }

    /// Set a disc's SCSI ID.
    func draft(settingScsiId scsiId: Int, forDiscAt instanceIndex: Int, in draft: MachineDraft) -> MachineDraft {
        mutatingDisc(at: instanceIndex, in: draft) { $0.setConfig("scsi-id", .int(scsiId)) }
    }

    /// Set a disc's image (bare master name or file path); nil clears it.
    func draft(settingImage image: String?, forDiscAt instanceIndex: Int, in draft: MachineDraft) -> MachineDraft {
        mutatingDisc(at: instanceIndex, in: draft) { ext in
            if let image = image, !image.isEmpty {
                ext.setConfig("image", .string(image))
            } else {
                ext.removeConfig("image")
            }
        }
    }

    private func mutatingDisc(at instanceIndex: Int,
                              in draft: MachineDraft,
                              _ mutate: (inout MachineDraft.Extension) -> Void) -> MachineDraft {
        var result = draft
        guard var extensions = result.extensions, extensions.indices.contains(instanceIndex),
              extensions[instanceIndex].name == capability.discExtension else { return draft }
        var ext = extensions[instanceIndex]
        mutate(&ext)
        extensions[instanceIndex] = ext
        result.extensions = extensions
        return result
    }
}

// MARK: - Extension config access

extension MachineDraft.Extension {
    func configInt(_ key: String) -> Int? {
        if case .int(let value)? = config?[key] { return value }
        return nil
    }

    func configString(_ key: String) -> String? {
        if case .string(let value)? = config?[key] { return value }
        return nil
    }

    mutating func setConfig(_ key: String, _ value: JSONValue) {
        var map = config ?? [:]
        map[key] = value
        config = map
    }

    mutating func removeConfig(_ key: String) {
        guard var map = config else { return }
        map.removeValue(forKey: key)
        config = map.isEmpty ? nil : map
    }
}
