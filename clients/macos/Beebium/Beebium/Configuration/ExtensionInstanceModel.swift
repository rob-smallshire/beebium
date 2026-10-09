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

/// Projects a model's attachment points (from the extensions schema) over a
/// draft's extension instances: which instances sit on which point, what may be
/// added, and the add/remove edits -- all writing through to draft.extensions.
///
/// This is the shared instance model behind the Peripherals section and the
/// Storage hard-disc rows. It stores nothing; an instance is addressed by its
/// index in `draft.extensions`, and each edit returns a new draft.
struct ExtensionInstanceModel {
    let schema: ExtensionsSchemaSection

    /// One extension instance in the draft.
    struct Instance: Identifiable, Equatable {
        /// Index in `draft.extensions` (stable address for edits).
        let index: Int
        let name: String
        var id: Int { index }
    }

    // MARK: - Points

    /// The model-built attachment points (not provided by an extension): the
    /// top-level groups the Peripherals section shows.
    var modelPoints: [AttachmentPoint] {
        schema.attachmentPoints.filter { $0.providedBy == nil }
    }

    /// The attachment point an extension attaches to -- the first point whose
    /// `extensions` lists it -- or nil if none does.
    func point(forExtension name: String) -> AttachmentPoint? {
        schema.attachmentPoints.first { $0.extensions.contains(name) }
    }

    /// The points `instance` provides (it appears in their `provided_by`), whose
    /// groups nest under it.
    func pointsProvided(by instance: Instance) -> [AttachmentPoint] {
        schema.attachmentPoints.filter { $0.providedBy?.contains(instance.name) ?? false }
    }

    // MARK: - Instances on a point

    /// The draft instances attached to `pointId`, in draft order.
    func instances(onPoint pointId: String, in draft: MachineDraft) -> [Instance] {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }) else { return [] }
        let hosted = Set(point.extensions)
        return (draft.extensions ?? []).enumerated().compactMap { index, ext in
            hosted.contains(ext.name) ? Instance(index: index, name: ext.name) : nil
        }
    }

    /// Current and maximum occupancy of a point (max nil = unbounded).
    func occupancy(onPoint pointId: String, in draft: MachineDraft) -> (count: Int, max: Int?) {
        let count = instances(onPoint: pointId, in: draft).count
        let max = schema.attachmentPoints.first { $0.id == pointId }?.maxOccupancy
        return (count, max)
    }

    /// The extensions that may still be added to `pointId`: those it hosts, if it
    /// has room (an unbounded point always has room).
    func addableExtensions(onPoint pointId: String, in draft: MachineDraft) -> [String] {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }) else { return [] }
        let occ = occupancy(onPoint: pointId, in: draft)
        if let max = occ.max, occ.count >= max { return [] }
        return point.extensions
    }

    // MARK: - Edits (write through)

    /// The framework id of the instance at `index`: its explicit config "id",
    /// else its extension name (the implicit id the server would assign a sole
    /// instance). Used to reference a specific provider (e.g. a SCSI adapter).
    func effectiveId(ofInstanceAt index: Int, in draft: MachineDraft) -> String {
        guard let extensions = draft.extensions, extensions.indices.contains(index) else { return "" }
        let ext = extensions[index]
        if case .string(let id)? = ext.config?["id"], !id.isEmpty { return id }
        return ext.name
    }

    /// Add an extension instance (with no config) to the draft. The caller has
    /// already decided the extension attaches somewhere with room.
    ///
    /// When another instance of the same extension already exists, the new one
    /// is given a distinct framework id (name-1, name-2, ...) so it can be
    /// referenced immediately -- the first instance keeps the implicit id (its
    /// name), matching the server's own id assignment.
    func draft(adding name: String, to draft: MachineDraft) -> MachineDraft {
        var result = draft
        var extensions = result.extensions ?? []
        var instance = MachineDraft.Extension(name: name)
        if extensions.contains(where: { $0.name == name }) {
            instance.config = ["id": .string(uniqueId(forName: name, in: extensions))]
        }
        extensions.append(instance)
        result.extensions = extensions
        return result
    }

    /// The first "name-K" (K from 1) not already taken as a framework id.
    private func uniqueId(forName name: String, in extensions: [MachineDraft.Extension]) -> String {
        let taken = Set(extensions.map { ext -> String in
            if case .string(let id)? = ext.config?["id"], !id.isEmpty { return id }
            return ext.name
        })
        var k = 1
        while taken.contains("\(name)-\(k)") { k += 1 }
        return "\(name)-\(k)"
    }

    /// Set the single instance on a point (a max-occupancy-1 point presented as
    /// a picker): remove any instance currently on it, then add `name` unless it
    /// is nil ("None"). A fresh config is left for resolve-preset to default.
    func draft(settingSoleInstance name: String?, onPoint pointId: String, in draft: MachineDraft) -> MachineDraft {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }) else { return draft }
        let hosted = Set(point.extensions)
        var result = draft
        var extensions = (result.extensions ?? []).filter { !hosted.contains($0.name) }
        if let name = name, !name.isEmpty {
            extensions.append(MachineDraft.Extension(name: name))
        }
        result.extensions = extensions
        return result
    }

    /// Remove the instance at `index`. Deliberately does NOT cascade: a provider
    /// left with nothing attached, or a dependent left with no provider, is the
    /// user's to resolve -- resolve-preset reports the orphan as a diagnostic
    /// rather than the client silently deleting more than the user asked.
    func draft(removingInstanceAt index: Int, from draft: MachineDraft) -> MachineDraft {
        var result = draft
        guard var extensions = result.extensions, extensions.indices.contains(index) else { return draft }
        extensions.remove(at: index)
        result.extensions = extensions
        return result
    }
}
