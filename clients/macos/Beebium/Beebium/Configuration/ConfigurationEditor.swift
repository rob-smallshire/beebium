// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

/// Configuration editor with sidebar navigation.
///
/// The file paths chosen here -- disc images, sideways ROM images -- need no
/// local-server gating, unlike their equivalents in the live sidebars. This
/// editor runs before there is a server to talk to: it is presented from
/// NewMachineDialog, and the machine it configures is spawned afterwards as a
/// local Process reached at 127.0.0.1 (see PresetManager.launchCore and
/// MachineManager.register). The paths become command-line arguments to a
/// process on this host, so they always mean here.
///
/// If a future flow ever launches or configures a machine on another host,
/// that assumption breaks and these panels need the same treatment as the
/// Storage and Memory sidebars. See docs/frontend-local-server-gating.md.
///
/// Shows a sidebar with configuration sections on the left and the selected
/// section's content on the right. The section list is data-driven: a section
/// appears only when the model offers it (Storage when the model has a storage
/// schema, Memory when it has sideways sockets). Further sections -- Peripherals,
/// Network, Processor, Keyboard -- slot into the same mechanism as they are
/// built, keyed on their own availability, rather than being hardcoded here.
struct ConfigurationEditor: View {
    /// The draft being edited; every section is an editor over part of it.
    @Binding var draft: MachineDraft

    /// The preset as loaded, so sections can mark what the user changed.
    let originalDraft: MachineDraft

    /// Storage schema describing available options
    let storageSchema: StorageSchemaSection?

    /// Sideways bank schema describing the machine's sockets
    let memorySchema: SidewaysSchemaSection?

    /// The model's hard-disc capability, if it has one (shows the Storage tab's
    /// Hard discs group).
    let hardDiscCapability: HardDiscCapability?

    /// The model's extensions schema (attachment points), for Peripherals.
    let extensionsSchema: ExtensionsSchemaSection?

    /// The model's econet schema (station rules + transports), for Network;
    /// nil on a model with no Econet socket.
    let econetSchema: EconetSchemaSection?

    /// Resolves a ROM image to its header (describe-rom), for the Memory tab.
    let romResolver: (@MainActor (String) async -> RomHeaderInfo?)?

    /// Resolves an extension's manifest (describe-extension), for Peripherals.
    let manifestResolver: (@MainActor (String) async -> ExtensionManifest)?

    /// Host serial-port paths, for choices_from "serial-ports" fields.
    var serialPorts: [String] = []

    /// Current resolve-preset diagnostics, so a section can show its own rows'
    /// problems.
    var diagnostics: [PresetManager.PresetDiagnostic] = []

    /// Machine model ID (e.g., "model-b")
    let modelId: String

    /// Currently selected section (may not be available for the current model;
    /// `effectiveSection` resolves that).
    @State private var selectedSection: ConfigSection = .storage

    /// A configuration section the dialog can show.
    enum ConfigSection: String, Identifiable {
        case storage = "Storage"
        case memory = "Memory"
        case peripherals = "Peripherals"
        case network = "Network"

        var id: String { rawValue }

        var icon: String {
            switch self {
            case .storage: return "internaldrive"
            case .memory: return "memorychip"
            case .peripherals: return "puzzlepiece.extension"
            case .network: return "network"
            }
        }
    }

    /// The sections this model actually offers, in display order.
    private var availableSections: [ConfigSection] {
        var sections: [ConfigSection] = []
        if storageSchema != nil { sections.append(.storage) }
        if let memorySchema = memorySchema, !memorySchema.sockets.isEmpty {
            sections.append(.memory)
        }
        if let extensionsSchema = extensionsSchema,
           extensionsSchema.attachmentPoints.contains(where: {
               $0.providedBy == nil && !$0.extensions.isEmpty
           }) {
            sections.append(.peripherals)
        }
        if econetSchema != nil { sections.append(.network) }
        return sections
    }

    /// The section to show: the selection when it is available, else the first
    /// available one.
    private var effectiveSection: ConfigSection? {
        if availableSections.contains(selectedSection) { return selectedSection }
        return availableSections.first
    }

    var body: some View {
        HStack(spacing: 0) {
            // Sidebar
            sidebarView
                .frame(width: 120)

            Divider()

            // Content area
            contentView
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(6)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color(NSColor.separatorColor), lineWidth: 1)
        )
    }

    // MARK: - Sidebar

    private var sidebarView: some View {
        VStack(alignment: .leading, spacing: 2) {
            ForEach(availableSections) { section in
                sidebarRow(for: section)
            }
            Spacer()
        }
        .padding(8)
    }

    private func sidebarRow(for section: ConfigSection) -> some View {
        Button {
            selectedSection = section
        } label: {
            HStack(spacing: 6) {
                Image(systemName: section.icon)
                    .font(.system(size: 12))
                    .frame(width: 16)
                Text(section.rawValue)
                    .font(.subheadline)
                Spacer()
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(
                effectiveSection == section
                    ? Color.accentColor.opacity(0.2)
                    : Color.clear
            )
            .cornerRadius(4)
        }
        .buttonStyle(.plain)
    }

    // MARK: - Content

    @ViewBuilder
    private var contentView: some View {
        switch effectiveSection {
        case .storage:
            StorageSectionView(
                draft: $draft,
                schema: storageSchema,
                hardDiscCapability: hardDiscCapability,
                diagnostics: diagnostics
            )
        case .memory:
            MemorySectionView(
                draft: $draft,
                originalDraft: originalDraft,
                schema: memorySchema,
                romResolver: romResolver
            )
        case .peripherals:
            if let extensionsSchema = extensionsSchema {
                PeripheralsSectionView(
                    draft: $draft,
                    schema: extensionsSchema,
                    diagnostics: diagnostics,
                    manifestResolver: manifestResolver,
                    serialPorts: serialPorts
                )
            }
        case .network:
            if let econetSchema = econetSchema {
                NetworkSectionView(
                    draft: $draft,
                    schema: econetSchema,
                    diagnostics: diagnostics,
                    manifestResolver: manifestResolver,
                    serialPorts: serialPorts
                )
            }
        case nil:
            Text("This machine has no configurable options yet.")
                .font(.caption)
                .foregroundColor(.secondary)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}
