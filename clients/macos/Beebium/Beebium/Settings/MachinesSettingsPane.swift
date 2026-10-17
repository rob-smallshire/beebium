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
import AppKit
import UniformTypeIdentifiers

struct MachinesSettingsPane: View {
    @StateObject private var presetManager = PresetManager.shared

    @State private var presetToDuplicate: MachinePreset?
    @State private var presetToEdit: MachinePreset?
    @State private var presetToDelete: MachinePreset?
    @State private var showDeleteConfirmation = false
    @State private var scrollFlashTrigger = 0
    @State private var contextMenuPresetId: UUID?

    private let menuDidClosePublisher = NotificationCenter.default.publisher(for: NSMenu.didEndTrackingNotification)

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            if presetManager.isDiscovering {
                ProgressView("Discovering machines...")
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if presetManager.systemPresets.isEmpty {
                emptyStateView
            } else {
                presetListView
            }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .task {
            await presetManager.discoverPresets()
        }
        .onReceive(menuDidClosePublisher) { _ in
            contextMenuPresetId = nil
        }
        .sheet(item: $presetToDuplicate) { preset in
            DuplicatePresetSheet(
                sourcePreset: preset,
                existingNames: Set(presetManager.userPresets.map(\.name)),
                onDuplicate: { newName in
                    Task {
                        _ = await presetManager.duplicatePreset(preset, newName: newName)
                    }
                    presetToDuplicate = nil
                },
                onCancel: {
                    presetToDuplicate = nil
                }
            )
        }
        .sheet(item: $presetToEdit) { preset in
            PresetEditSheet(
                preset: preset,
                onSave: {
                    presetToEdit = nil
                },
                onCancel: {
                    presetToEdit = nil
                }
            )
        }
        .alert("Delete Preset?", isPresented: $showDeleteConfirmation, presenting: presetToDelete) { preset in
            Button("Cancel", role: .cancel) {
                presetToDelete = nil
            }
            Button("Delete", role: .destructive) {
                Task {
                    _ = await presetManager.deletePreset(preset)
                }
                presetToDelete = nil
            }
        } message: { preset in
            Text("Are you sure you want to delete \"\(preset.name)\"? This action cannot be undone.")
        }
    }

    private var emptyStateView: some View {
        VStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 48))
                .foregroundColor(.secondary)
            Text("No Machine Cores Found")
                .font(.headline)
            if let error = presetManager.discoveryError {
                Text(error)
                    .foregroundColor(.secondary)
                    .font(.caption)
            } else {
                Text("Reinstall Beebium to restore default cores.")
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var presetListView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text("Built-in Machine Presets")
                    .font(.headline)

                ForEach(presetManager.systemPresets) { preset in
                    PresetRow(
                        preset: preset,
                        contextMenuPresetId: $contextMenuPresetId,
                        onDuplicate: { presetToDuplicate = preset },
                        onEdit: nil,
                        onDelete: nil,
                        onExport: { exportPreset(preset) },
                        onRevealInFinder: { presetManager.revealInFinder(preset) }
                    )
                }

                Divider()

                Text("My Machine Presets")
                    .font(.headline)

                if presetManager.userPresets.isEmpty {
                    Text("No custom presets yet")
                        .foregroundColor(.secondary)
                        .font(.caption)
                } else {
                    ForEach(presetManager.userPresets) { preset in
                        PresetRow(
                            preset: preset,
                            contextMenuPresetId: $contextMenuPresetId,
                            onDuplicate: { presetToDuplicate = preset },
                            onEdit: { presetToEdit = preset },
                            onDelete: {
                                presetToDelete = preset
                                showDeleteConfirmation = true
                            },
                            onExport: { exportPreset(preset) },
                            onRevealInFinder: { presetManager.revealInFinder(preset) }
                        )
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .modifier(ScrollIndicatorsFlashTriggerModifier(trigger: scrollFlashTrigger))
        .task(id: presetManager.systemPresets.count + presetManager.userPresets.count) {
            // Delay the flash so it happens after layout settles
            try? await Task.sleep(nanoseconds: 300_000_000)
            scrollFlashTrigger += 1
        }
    }

    private func exportPreset(_ preset: MachinePreset) {
        let panel = NSSavePanel()
        // Suggest the stored file's own name and type, taken from its
        // filepath, so the client does not hard-code the form. The server
        // stores presets as .beebium-preset archives, whose extension resolves
        // to the exported com.beebium.preset type declared in the Info.plist.
        let suggestedName = URL(fileURLWithPath: preset.presetFilepath).lastPathComponent
        panel.nameFieldStringValue = suggestedName
        let suggestedExtension = (suggestedName as NSString).pathExtension
        if let type = UTType(filenameExtension: suggestedExtension) {
            panel.allowedContentTypes = [type]
        }
        panel.title = "Export Preset"
        panel.message = "Choose a location to save the preset file."

        if panel.runModal() == .OK, let url = panel.url {
            Task {
                _ = await presetManager.exportPreset(preset, to: url.path)
            }
        }
    }
}

struct PresetRow: View {
    let preset: MachinePreset
    @Binding var contextMenuPresetId: UUID?
    let onDuplicate: () -> Void
    let onEdit: (() -> Void)?
    let onDelete: (() -> Void)?
    let onExport: () -> Void
    let onRevealInFinder: () -> Void

    private var isHighlighted: Bool {
        contextMenuPresetId == preset.id
    }

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: "desktopcomputer")
                .font(.title2)
                .foregroundColor(.accentColor)

            VStack(alignment: .leading, spacing: 2) {
                Text(preset.name)
                    .font(.body)
                if let description = preset.modelDescription {
                    Text(description)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()

            if preset.isEditable {
                Button("Edit") { onEdit?() }
                    .buttonStyle(.bordered)
            }
            Button("Duplicate") { onDuplicate() }
                .buttonStyle(.bordered)
        }
        .padding(.vertical, 4)
        .padding(.horizontal, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isHighlighted ? Color.accentColor.opacity(0.2) : Color.clear)
        )
        .background(RightClickDetector { contextMenuPresetId = preset.id })
        .contextMenu {
            if preset.isEditable {
                Button("Edit") { contextMenuPresetId = nil; onEdit?() }
                Divider()
            }
            Button("Duplicate") { contextMenuPresetId = nil; onDuplicate() }
            Button("Export...") { contextMenuPresetId = nil; onExport() }
            Divider()
            if preset.isEditable {
                Button("Delete", role: .destructive) { contextMenuPresetId = nil; onDelete?() }
                Divider()
            }
            Button("Reveal in Finder") { contextMenuPresetId = nil; onRevealInFinder() }
        }
    }
}

struct RightClickDetector: NSViewRepresentable {
    let onRightClick: () -> Void

    func makeNSView(context: Context) -> RightClickView {
        let view = RightClickView()
        view.onRightClick = onRightClick
        return view
    }

    func updateNSView(_ nsView: RightClickView, context: Context) {
        nsView.onRightClick = onRightClick
    }

    class RightClickView: NSView {
        var onRightClick: (() -> Void)?

        override func rightMouseDown(with event: NSEvent) {
            onRightClick?()
            super.rightMouseDown(with: event)
        }
    }
}

struct DuplicatePresetSheet: View {
    let sourcePreset: MachinePreset
    let existingNames: Set<String>
    @State private var newName: String
    let onDuplicate: (String) -> Void
    let onCancel: () -> Void

    init(sourcePreset: MachinePreset, existingNames: Set<String>, onDuplicate: @escaping (String) -> Void, onCancel: @escaping () -> Void) {
        self.sourcePreset = sourcePreset
        self.existingNames = existingNames
        self._newName = State(initialValue: Self.generateCopyName(from: sourcePreset.name, existingNames: existingNames))
        self.onDuplicate = onDuplicate
        self.onCancel = onCancel
    }

    private static func generateCopyName(from sourceName: String, existingNames: Set<String>) -> String {
        // Extract base name and existing copy number (if any)
        // Patterns: "Foo Copy", "Foo Copy 2", "Foo Copy 3", etc.
        let copyPattern = #"^(.+?) Copy(?: (\d+))?$"#
        let baseName: String
        let startNumber: Int

        if let regex = try? NSRegularExpression(pattern: copyPattern),
           let match = regex.firstMatch(in: sourceName, range: NSRange(sourceName.startIndex..., in: sourceName)),
           let baseRange = Range(match.range(at: 1), in: sourceName) {
            baseName = String(sourceName[baseRange])
            if match.range(at: 2).location != NSNotFound,
               let numRange = Range(match.range(at: 2), in: sourceName),
               let num = Int(sourceName[numRange]) {
                startNumber = num + 1
            } else {
                startNumber = 2
            }
        } else {
            baseName = sourceName
            startNumber = 1
        }

        // Generate unique name
        if startNumber == 1 {
            let candidate = "\(baseName) Copy"
            if !existingNames.contains(candidate) {
                return candidate
            }
        }

        var counter = max(startNumber, 2)
        while existingNames.contains("\(baseName) Copy \(counter)") {
            counter += 1
        }
        return "\(baseName) Copy \(counter)"
    }

    private var nameIsValid: Bool {
        !newName.trimmingCharacters(in: .whitespaces).isEmpty
    }

    private var nameIsDuplicate: Bool {
        existingNames.contains(newName.trimmingCharacters(in: .whitespaces))
    }

    var body: some View {
        VStack(spacing: 16) {
            Text("Duplicate Preset")
                .font(.headline)

            TextField("Name", text: $newName)
                .textFieldStyle(.roundedBorder)

            if nameIsDuplicate {
                Text("A preset with this name already exists")
                    .foregroundColor(.orange)
                    .font(.caption)
            } else {
                Text("Based on: \(sourcePreset.name)")
                    .foregroundColor(.secondary)
                    .font(.caption)
            }

            HStack {
                Button("Cancel", action: onCancel)
                    .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Duplicate") { onDuplicate(newName) }
                    .keyboardShortcut(.defaultAction)
                    .disabled(!nameIsValid || nameIsDuplicate)
            }
        }
        .padding()
        .frame(width: 350)
    }
}

struct PresetEditSheet: View {
    let preset: MachinePreset
    @State private var presetName: String
    let onSave: () -> Void
    let onCancel: () -> Void

    init(preset: MachinePreset, onSave: @escaping () -> Void, onCancel: @escaping () -> Void) {
        self.preset = preset
        self._presetName = State(initialValue: preset.name)
        self.onSave = onSave
        self.onCancel = onCancel
    }

    var body: some View {
        VStack(spacing: 16) {
            Text("Edit Preset")
                .font(.headline)

            TextField("Name", text: $presetName)
                .textFieldStyle(.roundedBorder)

            Text("Based on: \(preset.modelName)")
                .foregroundColor(.secondary)
                .font(.caption)

            Text("Configuration editing coming soon...")
                .foregroundColor(.secondary)
                .italic()
                .padding(.vertical, 8)

            HStack {
                Button("Cancel", action: onCancel)
                    .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Save", action: onSave)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding()
        .frame(width: 350)
    }
}

struct ScrollIndicatorsFlashTriggerModifier: ViewModifier {
    let trigger: Int

    func body(content: Content) -> some View {
        if #available(macOS 14.0, *) {
            content.scrollIndicatorsFlash(trigger: trigger)
        } else {
            content
        }
    }
}
