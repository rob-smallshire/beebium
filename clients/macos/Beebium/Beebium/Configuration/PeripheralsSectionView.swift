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

/// Caches extension manifests by name for the Peripherals forms, loaded on
/// demand from the host (describe-extension). Transient UI adornment over the
/// draft.
@MainActor
final class ManifestCache: ObservableObject {
    @Published private(set) var byName: [String: ExtensionManifest] = [:]
    var resolver: (@MainActor (String) async -> ExtensionManifest)?

    func load(_ name: String) async {
        guard byName[name] == nil, let resolver else { return }
        byName[name] = await resolver(name)
    }
}

/// The Peripherals section: one group per attachment point, listing the draft's
/// extension instances on it, with Add (the extensions that attach there, minus
/// those excluded by occupancy) and Remove. Points an extension provides (e.g.
/// the SCSI bus) nest under their provider instance.
///
/// A projection over the draft via ExtensionInstanceModel; every edit writes
/// through. The Econet socket and the Tube appear here too for now; Network
/// (Stage 3) and Processor (Stage 4) will take those groups, so the group view
/// is kept reusable.
struct PeripheralsSectionView: View {
    @Binding var draft: MachineDraft
    let schema: ExtensionsSchemaSection
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    let manifestResolver: (@MainActor (String) async -> ExtensionManifest)?

    @StateObject private var manifests = ManifestCache()

    private var model: ExtensionInstanceModel { ExtensionInstanceModel(schema: schema) }

    /// Every extension name currently in the draft, to preload manifests for.
    private var instanceNames: [String] {
        (draft.extensions ?? []).map(\.name)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                ForEach(model.modelPoints) { point in
                    pointGroup(point)
                }
            }
            .padding(12)
        }
        .onAppear { manifests.resolver = manifestResolver }
        .task(id: instanceNames.joined(separator: "|")) {
            for name in instanceNames { await manifests.load(name) }
        }
    }

    // MARK: - Point group

    @ViewBuilder
    private func pointGroup(_ point: AttachmentPoint) -> some View {
        let instances = model.instances(onPoint: point.id, in: draft)
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(point.displayName ?? point.id)
                    .font(.headline)
                Text(occupancyLabel(point))
                    .font(.caption)
                    .foregroundColor(.secondary)
                Spacer()
                addMenu(point)
            }

            if instances.isEmpty {
                Text("Nothing attached.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } else {
                ForEach(instances) { instance in
                    instanceView(instance)
                }
            }
        }
    }

    private func occupancyLabel(_ point: AttachmentPoint) -> String {
        let occ = model.occupancy(onPoint: point.id, in: draft)
        if let max = occ.max { return "\(occ.count) of \(max)" }
        return "\(occ.count)"
    }

    @ViewBuilder
    private func addMenu(_ point: AttachmentPoint) -> some View {
        let addable = model.addableExtensions(onPoint: point.id, in: draft)
        if !addable.isEmpty {
            Menu {
                ForEach(addable, id: \.self) { name in
                    Button(name) { draft = model.draft(adding: name, to: draft) }
                }
            } label: {
                Label("Add", systemImage: "plus")
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
        }
    }

    // MARK: - Instance

    @ViewBuilder
    private func instanceView(_ instance: ExtensionInstanceModel.Instance) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(instance.name)
                    .font(.subheadline)
                    .fontWeight(.medium)
                Spacer()
                Button {
                    draft = model.draft(removingInstanceAt: instance.index, from: draft)
                } label: {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
                .help("Remove \(instance.name)")
            }

            if let manifest = manifests.byName[instance.name] {
                ExtensionParameterForm(
                    manifest: manifest,
                    config: draft.extensions?[safe: instance.index]?.config ?? [:],
                    diagnostics: diagnostics.filter {
                        PresetDiagnosticLocation.extensionIndex(path: $0.path) == instance.index
                    },
                    setValue: { key, value in setConfig(key, value, forInstanceAt: instance.index) })
                    .padding(.leading, 8)
            }

            // Points this instance provides (e.g. the SCSI bus) nest beneath it.
            ForEach(model.pointsProvided(by: instance)) { provided in
                pointGroup(provided)
                    .padding(.leading, 16)
            }
        }
        .padding(8)
        .background(Color(NSColor.textBackgroundColor).opacity(0.5))
        .cornerRadius(6)
    }

    // MARK: - Write-through

    private func setConfig(_ key: String, _ value: JSONValue?, forInstanceAt index: Int) {
        guard var extensions = draft.extensions, extensions.indices.contains(index) else { return }
        var config = extensions[index].config ?? [:]
        if let value = value {
            config[key] = value
        } else {
            config.removeValue(forKey: key)
        }
        extensions[index].config = config.isEmpty ? nil : config
        draft.extensions = extensions
    }
}

private extension Array {
    subscript(safe index: Int) -> Element? {
        indices.contains(index) ? self[index] : nil
    }
}
