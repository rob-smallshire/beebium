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

/// One attachment point rendered as a tree node, shared by the Peripherals and
/// Processor sections: the point is the node, the extension instances on it are
/// its children, and a child that itself provides points (e.g. a SCSI host
/// adapter providing the SCSI bus) shows those as further child nodes -- this
/// view recursing on itself.
///
/// Every point is the same shape, whatever its occupancy. A single-occupancy
/// point (the user port, the Tube) is a node with at most one child: Add fills
/// the empty node, and the child carries Change (swap for another extension) and
/// Remove. A multi-occupancy point (the SCSI bus) is the same node with Add
/// available while there is room and each child removable. Being a concrete View
/// struct, the recursion for nested provided points needs no type erasure
/// (unlike a recursive @ViewBuilder function, which crashes the Release
/// optimiser).
struct AttachmentPointGroup: View {
    @Binding var draft: MachineDraft
    let schema: ExtensionsSchemaSection
    let point: AttachmentPoint
    var diagnostics: [PresetManager.PresetDiagnostic] = []
    var serialPorts: [String] = []

    private var model: ExtensionInstanceModel { ExtensionInstanceModel(schema: schema) }
    private var instances: [ExtensionInstanceModel.Instance] {
        model.instances(onPoint: point.id, in: draft)
    }
    private var isSingleOccupancy: Bool {
        model.occupancy(onPoint: point.id, in: draft).max == 1
    }
    private var addable: [String] {
        model.addableExtensions(onPoint: point.id, in: draft)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            header
            if let description = point.description, !description.isEmpty {
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            // The point's children: each attached instance, indented beneath the
            // node so the whole point reads as a tree.
            if instances.isEmpty {
                Text("Nothing attached.")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .padding(.leading, Self.childIndent)
            } else {
                ForEach(instances) { instance in
                    instanceNode(instance)
                        .padding(.leading, Self.childIndent)
                }
            }
        }
    }

    /// Indentation of a node's children, giving the tree its stepped shape.
    private static let childIndent: CGFloat = 14

    // MARK: - Header (the point node)

    private var header: some View {
        HStack {
            Text(point.displayName ?? point.id)
                .font(.headline)
            Text(occupancyLabel)
                .font(.caption)
                .foregroundColor(.secondary)
            Spacer()
            if !addable.isEmpty {
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
            ForEach(addable, id: \.self) { name in
                Button(schema.displayName(forExtension: name)) {
                    draft = model.draft(adding: name, to: draft)
                }
            }
        } label: {
            Label("Add", systemImage: "plus")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }

    // MARK: - Instance node (a child of the point)

    private func instanceNode(_ instance: ExtensionInstanceModel.Instance) -> some View {
        let description = schema.description(forExtension: instance.name)
        return VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline) {
                // The extension names itself: its friendly name as the title, its
                // identifier as a quieter subheading beneath.
                VStack(alignment: .leading, spacing: 1) {
                    Text(schema.displayName(forExtension: instance.name))
                        .font(.subheadline).fontWeight(.medium)
                    Text(instance.name)
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
                Spacer()
                // On a single-occupancy point, Change swaps the one instance for
                // another extension the point hosts (a tree replace).
                if isSingleOccupancy, !changeAlternatives(for: instance).isEmpty {
                    changeMenu(instance)
                }
                Button {
                    draft = model.draft(removingInstanceAt: instance.index, from: draft)
                } label: {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
                .help("Remove \(schema.displayName(forExtension: instance.name))")
            }
            if !description.isEmpty {
                Text(description)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            instanceDetail(instance)
        }
        .padding(8)
        .background(Color(NSColor.textBackgroundColor).opacity(0.5))
        .cornerRadius(6)
    }

    /// The extensions a single-occupancy instance could be swapped for.
    private func changeAlternatives(for instance: ExtensionInstanceModel.Instance) -> [String] {
        point.extensions.filter { $0 != instance.name }
    }

    private func changeMenu(_ instance: ExtensionInstanceModel.Instance) -> some View {
        Menu {
            ForEach(changeAlternatives(for: instance), id: \.self) { name in
                Button(schema.displayName(forExtension: name)) {
                    draft = model.draft(settingSoleInstance: name, onPoint: point.id, in: draft)
                }
            }
        } label: {
            Text("Change")
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }

    // MARK: - Instance detail (form + nested provided points)

    private func instanceDetail(_ instance: ExtensionInstanceModel.Instance) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            ExtensionParameterForm(
                manifest: schema.manifest(forExtension: instance.name),
                config: config(forInstanceAt: instance.index),
                diagnostics: diagnostics.forExtension(index: instance.index),
                bundledImages: schema.bundledImages ?? [:],
                serialPortSuggestions: serialPorts,
                setValue: { key, value in setConfig(key, value, forInstanceAt: instance.index) })
                .padding(.leading, 8)
            // Points this instance provides (e.g. the SCSI bus) nest beneath it
            // as further child nodes.
            ForEach(model.pointsProvided(by: instance)) { provided in
                AttachmentPointGroup(
                    draft: $draft, schema: schema, point: provided,
                    diagnostics: diagnostics, serialPorts: serialPorts)
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
