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

    /// The model's startup schema (screen-mode / auto-boot / links), for Keyboard.
    let startupSchema: StartupSchemaSection?

    /// Resolves a ROM image to its header (describe-rom), for the Memory tab.
    let romResolver: (@MainActor (String) async -> RomHeaderInfo?)?

    /// Host serial-port paths, for choices_from "serial-ports" fields.
    var serialPorts: [String] = []

    /// Current resolve-preset diagnostics, so a section can show its own rows'
    /// problems.
    var diagnostics: [PresetManager.PresetDiagnostic] = []

    /// Currently selected section (may not be available for the current model;
    /// `effectiveSection` resolves that).
    @State private var selectedSection: ConfigSection = .storage

    /// The one focus owner for every section's text fields. Owning it here, rather
    /// than one per section, means a new section cannot forget to coordinate focus:
    /// it takes this binding, and a background click on the content area (below)
    /// releases whichever field is focused. (Release on another control stays
    /// explicit in each control: a central draft-watch would wrongly release a
    /// field the moment the user clicked from one field to another, because
    /// committing the first changes the draft.)
    @FocusState private var focusedField: String?

    /// A configuration section the dialog can show.
    enum ConfigSection: String, Identifiable, CaseIterable {
        case storage = "Storage"
        case memory = "Memory"
        case peripherals = "Peripherals"
        case network = "Network"
        case processor = "Processor"
        case keyboard = "Keyboard"

        var id: String { rawValue }

        /// The live sidebar mode this section corresponds to. The Configurator
        /// shows its sections in the same order the emulator window's sidebar
        /// does (SidebarMode's case order), so the two cannot drift; the
        /// Configurator simply omits the modes it has nothing to configure for
        /// (Video, Sound).
        var sidebarMode: SidebarMode {
            switch self {
            case .storage: return .storage
            case .memory: return .memory
            case .peripherals: return .peripherals
            case .network: return .network
            case .processor: return .processor
            case .keyboard: return .keyboard
            }
        }

        var icon: String {
            switch self {
            case .storage: return "internaldrive"
            case .memory: return "memorychip"
            case .peripherals: return "puzzlepiece.extension"
            case .network: return "network"
            case .processor: return "cpu"
            case .keyboard: return "keyboard"
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
        // Processor is shown for every model: emulation speed is universal, and
        // the coprocessor sub-group appears only when the model has a tube point.
        if extensionsSchema != nil { sections.append(.processor) }
        if startupSchema != nil { sections.append(.keyboard) }
        // Show them in the live sidebar's order, so the Configurator and the
        // emulator window can never present their sections differently.
        return sections.sorted { $0.sidebarMode.rawValue < $1.sidebarMode.rawValue }
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
                .frame(width: Self.sidebarWidth)

            Divider()

            // Content area. A tap on the content background (empty space not
            // consumed by a control or field) releases the focused field.
            contentView
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .contentShape(Rectangle())
                .onTapGesture { focusedField = nil }
        }
        .background(Color(NSColor.controlBackgroundColor))
        .cornerRadius(6)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color(NSColor.separatorColor), lineWidth: 1)
        )
    }

    // MARK: - Sidebar

    /// Width of the section list. Sized to hold the longest label ("Peripherals")
    /// on one line at the dialog's minimum size, so no row ever wraps.
    static let sidebarWidth: CGFloat = 150

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
                    .lineLimit(1)
                    .fixedSize(horizontal: true, vertical: false)
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

    // Returns AnyView (type-erased) rather than `some View`: a @ViewBuilder
    // switch returning several different concrete View types makes the Release
    // whole-module optimiser crash swift-frontend with no diagnostic (the same
    // bug ExtensionViewRenderer documents). Type-erasure sidesteps it; the cost
    // is negligible for a handful of sections.
    private var contentView: AnyView {
        switch effectiveSection {
        case .storage:
            return AnyView(StorageSectionView(
                draft: $draft,
                schema: storageSchema,
                hardDiscCapability: hardDiscCapability,
                diagnostics: diagnostics))
        case .memory:
            return AnyView(MemorySectionView(
                draft: $draft,
                originalDraft: originalDraft,
                schema: memorySchema,
                romResolver: romResolver))
        case .peripherals:
            if let extensionsSchema = extensionsSchema {
                return AnyView(PeripheralsSectionView(
                    draft: $draft,
                    schema: extensionsSchema,
                    diagnostics: diagnostics,
                    serialPorts: serialPorts,
                    excludedPointIds: [ProcessorSectionView.tubePointId],
                    focusedField: $focusedField))
            }
            return AnyView(EmptyView())
        case .network:
            if let econetSchema = econetSchema {
                return AnyView(NetworkSectionView(
                    draft: $draft,
                    schema: econetSchema,
                    extensionsSchema: extensionsSchema,
                    diagnostics: diagnostics,
                    serialPorts: serialPorts,
                    focusedField: $focusedField))
            }
            return AnyView(EmptyView())
        case .processor:
            if let extensionsSchema = extensionsSchema {
                return AnyView(ProcessorSectionView(
                    draft: $draft,
                    extensionsSchema: extensionsSchema,
                    diagnostics: diagnostics,
                    serialPorts: serialPorts,
                    focusedField: $focusedField))
            }
            return AnyView(EmptyView())
        case .keyboard:
            if let startupSchema = startupSchema {
                return AnyView(KeyboardSectionView(
                    draft: $draft,
                    schema: startupSchema,
                    diagnostics: diagnostics,
                    focusedField: $focusedField))
            }
            return AnyView(EmptyView())
        case nil:
            return AnyView(Text("This machine has no configurable options yet.")
                .font(.caption)
                .foregroundColor(.secondary)
                .frame(maxWidth: .infinity, maxHeight: .infinity))
        }
    }
}
