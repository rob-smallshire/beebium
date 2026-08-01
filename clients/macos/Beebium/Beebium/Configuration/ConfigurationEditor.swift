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
/// Shows a sidebar with configuration sections on the left and the
/// selected section's content on the right. Currently only Storage
/// is implemented; other sections will be added in future phases.
struct ConfigurationEditor: View {
    /// Storage configuration state being edited
    @ObservedObject var storageConfig: StorageConfigurationState

    /// Storage schema describing available options
    let storageSchema: StorageSchemaSection?

    /// Memory (sideways ROM/RAM) configuration state being edited
    @ObservedObject var memoryConfig: MemoryConfigurationState

    /// Sideways bank schema describing the machine's sockets
    let memorySchema: SidewaysSchemaSection?

    /// Machine model ID (e.g., "model-b")
    let modelId: String

    /// Currently selected section
    @State private var selectedSection: ConfigSection = .storage

    /// Available configuration sections
    enum ConfigSection: String, CaseIterable, Identifiable {
        case storage = "Storage"
        case memory = "Memory"
        // Future: case peripherals = "Peripherals"
        // Future: case display = "Display"
        // Future: case keyboard = "Keyboard"

        var id: String { rawValue }

        var icon: String {
            switch self {
            case .storage: return "internaldrive"
            case .memory: return "memorychip"
            }
        }
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
            ForEach(ConfigSection.allCases) { section in
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
                selectedSection == section
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
        switch selectedSection {
        case .storage:
            StorageSectionView(
                storageConfig: storageConfig,
                schema: storageSchema
            )
        case .memory:
            MemorySectionView(
                memoryConfig: memoryConfig,
                schema: memorySchema
            )
        }
    }
}
