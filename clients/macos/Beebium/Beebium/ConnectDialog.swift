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

import AppKit
import SwiftUI

/// Shared state for the Connect window and pending connections
@MainActor
class ConnectWindowState: ObservableObject {
    static let shared = ConnectWindowState()

    /// Target for the next window to connect to (set before opening a new window)
    @Published var pendingTarget: ConnectionTarget?

    /// Whether the pending target needs Run() RPC after connection.
    /// Set to true when launching a new core with --wait=api.
    @Published var pendingNeedsRun: Bool = false

    /// Provenance UUID for a core launched by this app (nil for external connections)
    @Published var pendingProvenanceUUID: String?

    /// Initial sidebar visibility requested for the next window (e.g. from a
    /// beebium://…&sidebar=closed launch); nil leaves the app default.
    @Published var pendingShowSidebar: Bool?

    private init() {}

    /// Consume the pending target (returns it and clears it)
    /// - Returns: Tuple of (target, needsRun, provenanceUUID, showSidebar)
    func consumePendingTarget() -> (ConnectionTarget?, Bool, String?, Bool?) {
        let target = pendingTarget
        let needsRun = pendingNeedsRun
        let provenanceUUID = pendingProvenanceUUID
        let showSidebar = pendingShowSidebar
        pendingTarget = nil
        pendingNeedsRun = false
        pendingProvenanceUUID = nil
        pendingShowSidebar = nil
        return (target, needsRun, provenanceUUID, showSidebar)
    }
}

struct ConnectWindowContent: View {
    @ObservedObject var windowState = ConnectWindowState.shared
    @ObservedObject private var discoveryClient = DiscoveryClient.shared
    @ObservedObject private var recentManager = RecentConnectionsManager.shared
    @ObservedObject private var connectionRegistry = ConnectionRegistry.shared
    @Environment(\.openWindow) private var openWindow

    // Single address field (the source of truth)
    @State private var address: String = "localhost:48875"

    // Error state
    @State private var errorMessage: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            // Error banner
            if let error = errorMessage {
                errorBanner(error)
            }

            // Discovered machines section
            discoveredMachinesSection

            // Address field with recent dropdown
            addressSection

            // Buttons
            buttonBar
        }
        .padding(.horizontal, 20)
        .padding(.top, 12)
        .padding(.bottom, 16)
        .frame(width: 450)
        .onAppear {
            discoveryClient.startDiscovery()
            recentManager.load()
        }
        // No onDisappear teardown: the browser is shared and outlives this
        // dialog, because naming a new machine and flagging duplicate names
        // both depend on it still running. startDiscovery() is idempotent.
    }

    // MARK: - Discovered Machines Section

    @ViewBuilder
    private var discoveredMachinesSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Discovered Machines")
                .font(.subheadline)
                .foregroundColor(.secondary)

            if discoveryClient.machines.isEmpty {
                emptyDiscoveryView
            } else {
                discoveredMachinesList
            }
        }
    }

    @ViewBuilder
    private var discoveredMachinesList: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                // This Mac section
                if !discoveryClient.localMachines.isEmpty {
                    sectionHeader("This Mac")
                    ForEach(discoveryClient.localMachines) { machine in
                        machineRow(machine, showHost: false)
                    }
                }

                // Network section
                if !discoveryClient.remoteMachines.isEmpty {
                    if !discoveryClient.localMachines.isEmpty {
                        Spacer().frame(height: 8)
                    }
                    sectionHeader("Network")
                    ForEach(discoveryClient.remoteMachines) { machine in
                        machineRow(machine, showHost: true)
                    }
                }
            }
            .padding(.vertical, 4)
        }
        .frame(height: 160)
        .background(Color(nsColor: .controlBackgroundColor))
        .cornerRadius(6)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
        )
    }

    @ViewBuilder
    private func sectionHeader(_ title: String) -> some View {
        Text(title)
            .font(.caption)
            .fontWeight(.semibold)
            .foregroundColor(.secondary)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
    }

    @ViewBuilder
    private func machineRow(_ machine: DiscoveredMachine, showHost: Bool) -> some View {
        let isSelected = address == "\(machine.host):\(machine.port)"
        let isConnected = connectionRegistry.isConnected(to: machine.target)

        HStack {
            // Connection status indicator
            // Filled = connected, Empty = available
            Circle()
                .fill(isConnected ? Color.green : Color.clear)
                .overlay(Circle().stroke(Color.green, lineWidth: 1.5))
                .frame(width: 8, height: 8)

            // Machine info
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Text(machine.instanceName)
                        .fontWeight(.medium)
                    Text(friendlyModelName(machine.modelType))
                        .foregroundColor(.secondary)
                }
                .font(.callout)

                if showHost {
                    Text(machine.displayHost)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(isSelected ? Color.accentColor.opacity(0.2) : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture {
            // Clicking a discovered machine populates the address field
            address = "\(machine.host):\(machine.port)"
            errorMessage = nil
        }
        .help("\(machine.host):\(machine.port)")
        .contextMenu {
            Button("Copy Address") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString("\(machine.host):\(machine.port)", forType: .string)
            }
        }
    }

    @ViewBuilder
    private var emptyDiscoveryView: some View {
        VStack(spacing: 8) {
            if discoveryClient.isDiscoveryAvailable {
                Text("No machines found on the local network.")
                    .foregroundColor(.secondary)
                Text("Start a machine with the --advertise flag, or enter an address below.")
                    .foregroundColor(.secondary)
            } else {
                Text("Network discovery unavailable.")
                    .foregroundColor(.secondary)
                Text("Enter the machine address below.")
                    .foregroundColor(.secondary)
            }
        }
        .font(.caption)
        .frame(maxWidth: .infinity)
        .frame(height: 160)
        .background(Color(nsColor: .controlBackgroundColor))
        .cornerRadius(6)
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
        )
    }

    // MARK: - Address Section

    @ViewBuilder
    private var addressSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Address")
                .font(.subheadline)
                .foregroundColor(.secondary)

            // Native NSComboBox - editable text field with dropdown
            ComboBox(
                text: $address,
                items: recentManager.connections.map { "\($0.host):\($0.port)" }
            )
            .frame(height: 24)
            .onChange(of: address) { _ in
                errorMessage = nil
            }
        }
    }

    // MARK: - Buttons

    @ViewBuilder
    private var buttonBar: some View {
        HStack {
            Spacer()
            Button("Cancel") {
                closeWindow()
            }
            .keyboardShortcut(.cancelAction)

            Button("Connect") {
                connect()
            }
            .keyboardShortcut(.defaultAction)
            .disabled(!canConnect)
        }
    }

    /// Close the Connect window
    private func closeWindow() {
        // Find and close the Connect window by title
        // TODO: When minimum macOS version is raised to 14.0+, replace with:
        //   @Environment(\.dismissWindow) private var dismissWindow
        //   dismissWindow(id: "connect")
        if let window = NSApp.windows.first(where: { $0.title == "Connect to Machine" }) {
            window.close()
        }
    }

    // MARK: - Error Banner

    @ViewBuilder
    private func errorBanner(_ message: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundColor(.yellow)
            Text(message)
                .font(.callout)
            Spacer()
            Button {
                errorMessage = nil
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundColor(.secondary)
            }
            .buttonStyle(.plain)
        }
        .padding(10)
        .background(Color.yellow.opacity(0.1))
        .cornerRadius(6)
    }

    // MARK: - Helpers

    /// Map internal model identifiers to friendly display names.
    /// Model IDs match the executable suffix (e.g., beebium-model-b → "model-b").
    private func friendlyModelName(_ modelType: String) -> String {
        switch modelType {
        case "model-b":
            return "Model B"
        case "model-b-plus":
            return "Model B+"
        case "model-b-plus-128k":
            return "Model B+ 128K"
        case "model-b-romram":
            return "Model B (ROM/RAM)"
        case "master-128":
            return "Master 128"
        case "master-compact":
            return "Master Compact"
        case "model-a":
            return "Model A"
        default:
            // Fall back to the raw value with hyphens replaced by spaces and title-cased
            return modelType.replacingOccurrences(of: "-", with: " ").capitalized
        }
    }

    // MARK: - Actions

    private var canConnect: Bool {
        parseAddress() != nil
    }

    private func parseAddress() -> ConnectionTarget? {
        let trimmed = address.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return nil }

        // Try to parse host:port
        if let colonIndex = trimmed.lastIndex(of: ":") {
            let host = String(trimmed[..<colonIndex])
            let portString = String(trimmed[trimmed.index(after: colonIndex)...])
            if let port = Int(portString), port >= 1, port <= 65535, !host.isEmpty {
                return ConnectionTarget(host: host, port: port)
            }
        }

        // No colon or invalid port - assume default port
        if !trimmed.contains(":") {
            return ConnectionTarget(host: trimmed, port: 48875)
        }

        return nil
    }

    private func connect() {
        guard let target = parseAddress() else {
            errorMessage = "Invalid address format. Use host:port (e.g., localhost:48875)"
            return
        }

        // If already connected, just activate the existing window
        if connectionRegistry.activateWindow(for: target) {
            closeWindow()
            return
        }

        // Find display name if this matches a discovered machine
        let displayName = discoveryClient.machines.first {
            $0.host == target.host && $0.port == target.port
        }?.instanceName

        // Save to recent connections
        recentManager.addSuccessful(host: target.host, port: target.port, displayName: displayName)

        // Set pending target and open a new window
        windowState.pendingTarget = target
        openWindow(id: "main")
        closeWindow()
    }
}

// MARK: - ComboBox (NSComboBox wrapper)

/// SwiftUI wrapper for NSComboBox - an editable text field with a dropdown list
struct ComboBox: NSViewRepresentable {
    @Binding var text: String
    let items: [String]

    func makeNSView(context: Context) -> NSComboBox {
        let comboBox = NSComboBox()
        comboBox.isEditable = true
        comboBox.completes = true
        comboBox.delegate = context.coordinator
        comboBox.target = context.coordinator
        comboBox.action = #selector(Coordinator.comboBoxChanged(_:))
        return comboBox
    }

    func updateNSView(_ comboBox: NSComboBox, context: Context) {
        // Update items
        comboBox.removeAllItems()
        comboBox.addItems(withObjectValues: items)

        // Update text if it changed externally
        if comboBox.stringValue != text {
            comboBox.stringValue = text
        }
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(self)
    }

    class Coordinator: NSObject, NSComboBoxDelegate {
        var parent: ComboBox

        init(_ parent: ComboBox) {
            self.parent = parent
        }

        @objc func comboBoxChanged(_ sender: NSComboBox) {
            parent.text = sender.stringValue
        }

        func comboBoxSelectionDidChange(_ notification: Notification) {
            guard let comboBox = notification.object as? NSComboBox else { return }
            if comboBox.indexOfSelectedItem >= 0 {
                parent.text = comboBox.itemObjectValue(at: comboBox.indexOfSelectedItem) as? String ?? ""
            }
        }

        func controlTextDidChange(_ notification: Notification) {
            guard let comboBox = notification.object as? NSComboBox else { return }
            parent.text = comboBox.stringValue
        }
    }
}

// MARK: - Preview

#if DEBUG
struct ConnectWindowContent_Previews: PreviewProvider {
    static var previews: some View {
        ConnectWindowContent()
    }
}
#endif
