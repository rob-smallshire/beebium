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

    // MARK: - Providers of a provided point

    /// The provider instances of a provided point: the draft instances whose
    /// extension provides it (named in the point's `provided_by`), in draft
    /// order. Empty for a model-built point.
    func providers(ofPoint pointId: String, in draft: MachineDraft) -> [Instance] {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }),
              let names = point.providedBy else { return [] }
        let providerNames = Set(names)
        return (draft.extensions ?? []).enumerated().compactMap { index, ext in
            providerNames.contains(ext.name) ? Instance(index: index, name: ext.name) : nil
        }
    }

    /// Whether `instance` is the last provider of `pointId` (the one the server's
    /// "else the last provider" rule resolves a child with no named provider to).
    /// Its nested point group is where the point's orphans are surfaced.
    func isLastProvider(_ instance: Instance, ofPoint pointId: String, in draft: MachineDraft) -> Bool {
        providers(ofPoint: pointId, in: draft).last?.index == instance.index
    }

    /// The config key by which an extension names a provider of `pointId` (its
    /// `provider_of` parameter, e.g. "adapter-id"), or nil when it declares none.
    func providerParameter(forExtension name: String, point pointId: String) -> String? {
        schema.manifest(forExtension: name).parameters.first { $0.providerOf == pointId }?.key
    }

    /// The children on a provided point whose `provider_of` parameter names a
    /// provider that is not present -- orphans. The server's rule re-aims only a
    /// child that names NO provider (to the last); one that names a provider which
    /// has since gone is left dangling for the resolver to diagnose (spec 12.3).
    /// These belong to no provider group, so they are shown under the point in an
    /// "Unattached" group, with Remove, rather than silently re-aimed or hidden.
    func orphanInstances(onPoint pointId: String, in draft: MachineDraft) -> [Instance] {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }),
              point.providedBy != nil else { return [] }
        let hosted = Set(point.extensions)
        let all = (draft.extensions ?? []).enumerated().compactMap { index, ext in
            hosted.contains(ext.name) ? Instance(index: index, name: ext.name) : nil
        }
        let providers = providers(ofPoint: pointId, in: draft)
        let providerIds = Set(providers.map { effectiveId(ofInstanceAt: $0.index, in: draft) })
        return all.filter { child in
            !providerIds.contains(chosenProviderId(forChildAt: child.index, name: child.name,
                                                   onPoint: pointId, providers: providers, in: draft))
        }
    }

    /// The framework id of the provider a child belongs to on a provided point:
    /// the id its `provider_of` parameter names, else the last provider of the
    /// point -- the rule the server applies when it attaches the child. "" when
    /// the point has no providers.
    private func chosenProviderId(forChildAt index: Int, name: String, onPoint pointId: String,
                                  providers: [Instance], in draft: MachineDraft) -> String {
        if let key = providerParameter(forExtension: name, point: pointId) {
            let named = configString(key, ofInstanceAt: index, in: draft)
            if !named.isEmpty { return named }
        }
        guard let last = providers.last else { return "" }
        return effectiveId(ofInstanceAt: last.index, in: draft)
    }

    // MARK: - Instances on a point

    /// The draft instances attached to `pointId`, in draft order. On a provided
    /// point, pass the `provider` whose children you want and only the instances
    /// that belong to it are returned -- by the child's `provider_of` parameter,
    /// else the last provider of the point (the server's rule). nil, or a
    /// model-built point, returns every instance attached to the point.
    func instances(onPoint pointId: String, under provider: Instance? = nil,
                   in draft: MachineDraft) -> [Instance] {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }) else { return [] }
        let hosted = Set(point.extensions)
        let all = (draft.extensions ?? []).enumerated().compactMap { index, ext in
            hosted.contains(ext.name) ? Instance(index: index, name: ext.name) : nil
        }
        guard let provider = provider else { return all }
        let providers = providers(ofPoint: pointId, in: draft)
        let providerId = effectiveId(ofInstanceAt: provider.index, in: draft)
        return all.filter {
            chosenProviderId(forChildAt: $0.index, name: $0.name, onPoint: pointId,
                             providers: providers, in: draft) == providerId
        }
    }

    /// Current and maximum occupancy of a point (max nil = unbounded), counted
    /// per provider when a provided point's `provider` is given.
    func occupancy(onPoint pointId: String, under provider: Instance? = nil,
                   in draft: MachineDraft) -> (count: Int, max: Int?) {
        let count = instances(onPoint: pointId, under: provider, in: draft).count
        let max = schema.attachmentPoints.first { $0.id == pointId }?.maxOccupancy
        return (count, max)
    }

    /// The extensions that may still be added to `pointId`: those it hosts, if it
    /// has room (an unbounded point always has room). Room is measured for the
    /// given `provider` on a provided point, so one full adapter does not stop
    /// another from taking discs.
    func addableExtensions(onPoint pointId: String, under provider: Instance? = nil,
                           in draft: MachineDraft) -> [String] {
        guard let point = schema.attachmentPoints.first(where: { $0.id == pointId }) else { return [] }
        let occ = occupancy(onPoint: pointId, under: provider, in: draft)
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

    /// The string config value of `key` on the instance at `index`, or "" when
    /// absent or not a string.
    private func configString(_ key: String, ofInstanceAt index: Int, in draft: MachineDraft) -> String {
        guard let extensions = draft.extensions, extensions.indices.contains(index),
              case .string(let value)? = extensions[index].config?[key] else { return "" }
        return value
    }

    /// Add an extension instance to the draft, attached to `pointId`. The caller
    /// has already decided the extension attaches there with room.
    ///
    /// When another instance of the same extension already exists, the new one
    /// is given a distinct framework id (name-1, name-2, ...) so it can be
    /// referenced immediately -- the first instance keeps the implicit id (its
    /// name), matching the server's own id assignment.
    ///
    /// Beneath one of SEVERAL providers of a provided point (e.g. a second SCSI
    /// adapter), the new instance's `provider_of` parameter names the chosen
    /// provider, so the disc belongs to that adapter and not merely the last.
    /// With a single provider nothing is written -- the server's "else the last
    /// provider" rule already resolves to it -- so a one-adapter preset is
    /// unchanged, byte for byte.
    func draft(adding name: String, onPoint pointId: String, under provider: Instance? = nil,
               to draft: MachineDraft) -> MachineDraft {
        var result = draft
        var extensions = result.extensions ?? []
        var config: [String: JSONValue] = [:]
        if extensions.contains(where: { $0.name == name }) {
            config["id"] = .string(uniqueId(forName: name, in: extensions))
        }
        if let provider = provider,
           providers(ofPoint: pointId, in: result).count > 1,
           let key = providerParameter(forExtension: name, point: pointId) {
            config[key] = .string(effectiveId(ofInstanceAt: provider.index, in: result))
        }
        var instance = MachineDraft.Extension(name: name)
        instance.config = config.isEmpty ? nil : config
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
