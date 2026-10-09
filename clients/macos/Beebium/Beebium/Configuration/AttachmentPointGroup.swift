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

/// One attachment point's group, shared by the Peripherals and Processor
/// sections: the instances on the point, how to add and remove them, and each
/// instance's parameter form. A point an instance provides (e.g. the SCSI bus)
/// nests beneath that instance by this view recursing on itself.
///
/// It adapts to the point's occupancy: a point that holds at most one extension
/// is a picker ("None" plus the extensions that attach there) with the chosen
/// instance's form below; a point that holds several is an add menu and a list
/// of removable instances. Being a concrete View struct, the recursion for
/// nested provided points needs no type erasure (unlike a recursive
/// @ViewBuilder function, which crashes the Release optimiser).
struct AttachmentPointGroup: View {
    @Binding var draft: MachineDraft
    let schema: ExtensionsSchemaSection
    let point: AttachmentPoint
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    @ObservedObject var manifests: ManifestCache
    var serialPorts: [String] = []

    private var model: ExtensionInstanceModel { ExtensionInstanceModel(schema: schema) }
    private var instances: [ExtensionInstanceModel.Instance] {
        model.instances(onPoint: point.id, in: draft)
    }
    private var isSingleOccupancy: Bool {
        model.occupancy(onPoint: point.id, in: draft).max == 1
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            header
            if isSingleOccupancy {
                singleOccupancyPicker
            } else {
                multiOccupancyList
            }
        }
        .task(id: instances.map(\.name).joined(separator: "|")) {
            for instance in instances { await manifests.load(instance.name) }
        }
    }

    // MARK: - Header

    private var header: some View {
        HStack {
            Text(point.displayName ?? point.id)
                .font(.headline)
            Text(occupancyLabel)
                .font(.caption)
                .foregroundColor(.secondary)
            Spacer()
            if !isSingleOccupancy, !model.addableExtensions(onPoint: point.id, in: draft).isEmpty {
                addMenu
            }
        }
    }

    private var occupancyLabel: String {
        let occ = model.occupancy(onPoint: point.id, in: draft)
        if let max = occ.max { return "\(occ.count) of \(max)" }
        return "\(occ.count)"
    }

    private var addMenu: some View {
        Menu {
            ForEach(model.addableExtensions(onPoint: point.id, in: draft), id: \.self) { name in
                Button(name) { draft = model.draft(adding: name, to: draft) }
            }
        } label: {
            Label("Add", systemImage: "plus")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }

    // MARK: - Single-occupancy (picker)

    private var singleOccupancyPicker: some View {
        let current = instances.first
        return VStack(alignment: .leading, spacing: 6) {
            Picker("", selection: Binding(
                get: { current?.name ?? "" },
                set: { draft = model.draft(settingSoleInstance: $0.isEmpty ? nil : $0, onPoint: point.id, in: draft) }
            )) {
                Text("None").tag("")
                ForEach(point.extensions, id: \.self) { Text($0).tag($0) }
            }
            .labelsHidden()
            .frame(maxWidth: 260)

            if let instance = current {
                instanceDetail(instance)
            }
        }
    }

    // MARK: - Multi-occupancy (list)

    private var multiOccupancyList: some View {
        VStack(alignment: .leading, spacing: 6) {
            if instances.isEmpty {
                Text("Nothing attached.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } else {
                ForEach(instances) { instance in
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Text(instance.name)
                                .font(.subheadline).fontWeight(.medium)
                            Spacer()
                            Button {
                                draft = model.draft(removingInstanceAt: instance.index, from: draft)
                            } label: {
                                Image(systemName: "trash")
                            }
                            .buttonStyle(.borderless)
                            .help("Remove \(instance.name)")
                        }
                        instanceDetail(instance)
                    }
                    .padding(8)
                    .background(Color(NSColor.textBackgroundColor).opacity(0.5))
                    .cornerRadius(6)
                }
            }
        }
    }

    // MARK: - Instance detail (form + nested provided points)

    private func instanceDetail(_ instance: ExtensionInstanceModel.Instance) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            if let manifest = manifests.byName[instance.name] {
                ExtensionParameterForm(
                    manifest: manifest,
                    config: config(forInstanceAt: instance.index),
                    diagnostics: diagnostics.filter {
                        PresetDiagnosticLocation.extensionIndex(path: $0.path) == instance.index
                    },
                    bundledImages: schema.bundledImages ?? [:],
                    serialPortSuggestions: serialPorts,
                    setValue: { key, value in setConfig(key, value, forInstanceAt: instance.index) })
                    .padding(.leading, 8)
            }
            // Points this instance provides (e.g. the SCSI bus) nest beneath it.
            ForEach(model.pointsProvided(by: instance)) { provided in
                AttachmentPointGroup(
                    draft: $draft, schema: schema, point: provided,
                    diagnostics: diagnostics, manifests: manifests, serialPorts: serialPorts)
                    .padding(.leading, 16)
            }
        }
    }

    // MARK: - Config read/write

    private func config(forInstanceAt index: Int) -> [String: JSONValue] {
        guard let extensions = draft.extensions, extensions.indices.contains(index) else { return [:] }
        return extensions[index].config ?? [:]
    }

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
