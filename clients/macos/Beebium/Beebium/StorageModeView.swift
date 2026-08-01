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
import UniformTypeIdentifiers

/// Storage mode view: floppy drives at the top (powered by DiscClient
/// as before), then a section per media type for storage devices
/// published by peripheral extensions (hard discs today; RAM discs,
/// microdrives, etc. once we have them).
///
/// Peripheral storage devices are read from PeripheralsClient.tree by
/// walking the node hierarchy and collecting StorageDevice entries
/// in tree order. Activity indicators stream through the existing
/// IndicatorClient -- the StorageDevice.activityIndicatorName is the
/// key into that client's published values dictionary.
struct StorageModeView: View {
    @ObservedObject var discClient: DiscClient
    @ObservedObject var peripheralsClient: PeripheralsClient
    @ObservedObject var indicatorClient: IndicatorClient
    /// Whether the server is on this host.
    ///
    /// Everything that hands the server a path, or opens a path the server
    /// reported, depends on the two processes sharing a filesystem.
    let isServerLocal: Bool

    var body: some View {
        if !discClient.isLoaded || !peripheralsClient.isLoaded {
            loadingView
        } else if !discClient.hasDiscController && peripheralStorage.isEmpty {
            noStorageView
        } else {
            contentView
        }
    }

    /// All peripheral storage devices reachable from the current
    /// PeripheralTree, flattened in tree order. Recomputed on every
    /// access; the tree itself doesn't change often.
    private var peripheralStorage: [PeripheralStorageDevice] {
        var out: [PeripheralStorageDevice] = []
        for group in peripheralsClient.tree.groups {
            for node in group.nodes {
                collect(node, into: &out)
            }
        }
        for orphan in peripheralsClient.tree.orphans {
            collect(orphan, into: &out)
        }
        return out
    }

    private func collect(_ node: PeripheralNode,
                         into out: inout [PeripheralStorageDevice]) {
        out.append(contentsOf: node.storageDevices)
        for child in node.children {
            collect(child, into: &out)
        }
    }

    private var loadingView: some View {
        VStack(spacing: 12) {
            if let error = discClient.errorMessage {
                Image(systemName: "exclamationmark.triangle")
                    .font(.system(size: 24))
                    .foregroundColor(.yellow)
                Text("Connection Error")
                    .font(.headline)
                    .foregroundColor(.secondary)
                Text(error)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
            } else {
                ProgressView()
                Text("Loading...")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var noStorageView: some View {
        VStack(spacing: 12) {
            Image(systemName: "internaldrive")
                .font(.system(size: 32))
                .foregroundColor(.secondary)
            Text("No Storage Devices")
                .font(.headline)
                .foregroundColor(.secondary)
            Text("This machine has no disc controller and no peripheral storage devices.")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var contentView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                if discClient.hasDiscController {
                    floppyList
                }
                if !peripheralStorage.isEmpty {
                    if discClient.hasDiscController {
                        Divider().padding(.horizontal, 12)
                    }
                    peripheralStorageList
                }
            }
            .padding(.vertical, 8)
        }
    }

    private var floppyList: some View {
        LazyVStack(spacing: 0) {
            ForEach(Array(discClient.drives.enumerated()), id: \.offset) { index, drive in
                DriveRowView(
                    drive: drive,
                    discClient: discClient,
                    isServerLocal: isServerLocal
                )
                if index < discClient.drives.count - 1 {
                    Divider()
                        .padding(.horizontal, 12)
                }
            }
        }
    }

    private var peripheralStorageList: some View {
        // Flat list of rows in tree order, with the same inter-row
        // divider treatment the floppy list uses above. No category
        // headers: the row title itself carries the device class
        // ("Hard Disc Drive 0", "RAM Disc Drive 0", ...).
        VStack(alignment: .leading, spacing: 0) {
            ForEach(Array(peripheralStorage.enumerated()),
                    id: \.element.id) { i, device in
                StorageDeviceRowView(device: device,
                                     indicatorClient: indicatorClient,
                                     isServerLocal: isServerLocal)
                if i < peripheralStorage.count - 1 {
                    Divider().padding(.horizontal, 12)
                }
            }
        }
    }
}

/// Row for one peripheral-published storage device. Mirrors the visual
/// layout of DriveRowView for floppies (header line with label +
/// activity dot, secondary line with detail) but without the eject /
/// browse buttons -- FIXED devices have no user-actionable affordance,
/// and REMOVABLE-device UI isn't built yet.
private struct StorageDeviceRowView: View {
    let device: PeripheralStorageDevice
    @ObservedObject var indicatorClient: IndicatorClient
    let isServerLocal: Bool

    /// The indicator value is a brightness 0-255; treat any non-zero
    /// value as "active" for the simple dot rendering. The 250ms
    /// pulse stretcher on the server side ensures brief bus
    /// transactions still register as a visible blink.
    private var isActive: Bool {
        guard !device.activityIndicatorName.isEmpty else { return false }
        return (indicatorClient.values[device.activityIndicatorName] ?? 0) > 0
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Text(device.name)
                    .font(.headline)
                if isActive {
                    Image(systemName: "circle.fill")
                        .font(.system(size: 6))
                        .foregroundColor(.green)
                        .help("Disc activity")
                }
                Spacer()
            }
            if device.backingPath.isEmpty {
                Text("No image")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            } else {
                // Show the filename only, matching the floppy row's
                // disc-name convention. The full path stays available
                // via the tooltip and the Copy Path context menu item.
                Text(URL(fileURLWithPath: device.backingPath).lastPathComponent)
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .help(device.backingPath)
                    // Same affordances as the floppy disc-name row.
                    .contextMenu {
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(device.backingPath,
                                                           forType: .string)
                        } label: {
                            Label("Copy Path", systemImage: "doc.on.doc")
                        }
                        if isServerLocal {
                            Button {
                                let url = URL(fileURLWithPath: device.backingPath)
                                NSWorkspace.shared.activateFileViewerSelecting([url])
                            } label: {
                                Label("Reveal in Finder", systemImage: "folder")
                            }
                        }
                    }
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// Row view for a single floppy drive
private struct DriveRowView: View {
    let drive: Beebium_DriveStatus
    @ObservedObject var discClient: DiscClient
    /// See StorageModeView.isServerLocal.
    let isServerLocal: Bool
    @State private var isDropTargeted = false
    @State private var isProcessing = false
    /// Reason a hovering drag will not be accepted, shown while it hovers.
    @State private var dropRefusal: DiscDropRefusal?
    /// Reason the last insert or eject did not happen, shown until the next
    /// attempt. A request the server turned down has to be visible: the row
    /// would otherwise simply not change and look like nothing was tried.
    @State private var actionError: String?
    /// True once a pending eject has been waiting long enough that the drive
    /// is evidently busy. The server waits indefinitely rather than pulling
    /// the disc out of a spinning drive, so this is where the user is offered
    /// that decision.
    @State private var ejectIsStalled = false
    /// Counts the wait so the offer appears at a steady delay.
    @State private var ejectWaitTask: Task<Void, Never>?

    private var isEmpty: Bool {
        drive.state == .empty
    }

    private var isEjecting: Bool {
        drive.state == .ejecting
    }

    private var isLoaded: Bool {
        drive.state == .loaded
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            // Drive header
            HStack {
                Text("Floppy Disc Drive \(drive.drive)")
                    .font(.headline)
                Spacer()
                if drive.motorOn {
                    Image(systemName: "circle.fill")
                        .font(.system(size: 6))
                        .foregroundColor(.green)
                        .help("Motor running")
                }
            }

            // Content based on state
            if isEjecting {
                ejectingContent
            } else if isLoaded {
                loadedContent
            } else {
                emptyContent
            }

            if let message = statusMessage {
                statusLine(message)
            }

            // Actions row
            HStack(spacing: 8) {
                browseButton
                Spacer()
                if isEjecting {
                    cancelEjectButton
                    if ejectIsStalled {
                        forceEjectButton
                    }
                }
                ejectButton
            }
        }
        .padding(12)
        .background(isDropTargeted ? Color.accentColor.opacity(0.1) : Color.clear)
        .contentShape(Rectangle())
        .onAppear { trackEjectProgress() }
        .onDisappear {
            ejectWaitTask?.cancel()
            ejectWaitTask = nil
        }
        .onChange(of: drive.state) { _ in trackEjectProgress() }
        .onDrop(of: [.fileURL], delegate: DiscImageDropDelegate(
            isSlotEmpty: isEmpty,
            occupiedReason: "Eject disc first",
            isServerLocal: isServerLocal,
            isTargeted: $isDropTargeted,
            refusal: $dropRefusal,
            accept: { url in insertDisc(url: url) },
            report: { message in actionError = message }
        ))
    }

    /// The refusal for a drag in flight wins over an older failure: it is
    /// about what the user is doing right now.
    private var statusMessage: String? {
        dropRefusal?.message ?? actionError
    }

    private func statusLine(_ message: String) -> some View {
        HStack(spacing: 4) {
            Image(systemName: dropRefusal != nil
                  ? "nosign" : "exclamationmark.triangle.fill")
                .font(.caption2)
            Text(message)
                .font(.caption)
                .lineLimit(2)
        }
        .foregroundColor(.red)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    // MARK: - Content Views

    private var emptyContent: some View {
        Text(isServerLocal
             ? "Empty - Drop disc image here"
             : "Empty - server is on another host")
            .font(.caption)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var loadedContent: some View {
        VStack(alignment: .leading, spacing: 4) {
            // Disc name with path tooltip and context menu
            if !drive.discName.isEmpty {
                Text(drive.discName)
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .lineLimit(1)
                    .help(fullPath)
                    .contextMenu {
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(fullPath, forType: .string)
                        } label: {
                            Label("Copy Path", systemImage: "doc.on.doc")
                        }

                        // The path names a file on the server's host, so
                        // Finder can only find it when that is this host.
                        // Copy Path stays either way: the path is still what
                        // the server reported, and still worth quoting.
                        if isServerLocal {
                            Button {
                                NSWorkspace.shared.activateFileViewerSelecting([fileURL])
                            } label: {
                                Label("Reveal in Finder", systemImage: "folder")
                            }
                        }
                    }
            }

            // Write protection status
            if drive.writeProtected {
                HStack(spacing: 4) {
                    Image(systemName: "lock.fill")
                        .font(.caption2)
                    Text("Read-only")
                        .font(.caption)
                }
                .foregroundColor(.secondary)
                .help("Disc is write-protected")
            }
        }
    }

    private var fullPath: String {
        // drive.discURL can be either a file:// URL or a plain
        // filesystem path depending on how the disc was mounted
        // (drop vs CLI). URL(string:) parses both, but for the
        // already-plain case it returns a URL with no scheme so
        // url.path strips information; fall through to the raw
        // string in that case.
        if let url = URL(string: drive.discURL),
           url.scheme != nil && !url.scheme!.isEmpty {
            return url.path
        }
        return drive.discURL
    }

    private var fileURL: URL {
        // Always produce a file:// URL from the path string.
        // URL(string:) succeeds for plain paths too but the result
        // has no scheme, which NSWorkspace.activateFileViewerSelecting
        // silently refuses to act on -- previously the Reveal in
        // Finder menu item never fired for plain-path mounts.
        URL(fileURLWithPath: fullPath)
    }

    private var ejectingContent: some View {
        HStack(spacing: 8) {
            ProgressView()
                .scaleEffect(0.7)
            Text(ejectIsStalled ? "Ejecting - drive busy" : "Ejecting...")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .help(ejectIsStalled
              ? "Waiting for the drive to stop. Force Eject removes the disc now."
              : "Waiting for the drive to stop")
    }

    // MARK: - Buttons

    private var browseButton: some View {
        // Browsing picks a file on this host and sends its path for the
        // server to open, which only works when the server is here too.
        let isEnabled = isEmpty && !isProcessing && isServerLocal
        return Button {
            browseForDisc()
        } label: {
            Image(systemName: "folder")
                .font(.system(size: 16))
                .foregroundColor(isEnabled ? .primary.opacity(0.6) : .secondary.opacity(0.3))
        }
        .buttonStyle(.borderless)
        .disabled(!isEnabled)
        .help(browseHelp)
    }

    private var browseHelp: String {
        if !isServerLocal {
            return "The server is on another host, so it cannot open a disc image from this one"
        }
        return isEmpty ? "Browse for disc image" : "Eject disc first"
    }

    private var ejectButton: some View {
        Button {
            ejectDisc()
        } label: {
            Image(systemName: "eject.fill")
                .font(.system(size: 14))
        }
        .buttonStyle(.bordered)
        .controlSize(.regular)
        .disabled(isEmpty || isEjecting || isProcessing)
        .help("Eject disc")
    }

    private var cancelEjectButton: some View {
        Button("Cancel") {
            cancelEject()
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
        .disabled(isProcessing)
        .help("Stop ejecting and keep the disc in the drive")
    }

    private var forceEjectButton: some View {
        // Only offered once waiting has plainly not worked. Forcing takes the
        // disc out from under whatever the drive is doing, so it is never
        // something the machine decides by itself.
        Button("Force Eject") {
            forceEject()
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
        .disabled(isProcessing)
        .help("Remove the disc now, without waiting for the drive to stop")
    }

    // MARK: - Actions

    private func browseForDisc() {
        let panel = NSOpenPanel()
        panel.title = "Select Disc Image"
        panel.allowedContentTypes = DiscImageTypes.contentTypes
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        if panel.runModal() == .OK, let url = panel.url {
            insertDisc(url: url)
        }
    }

    private func insertDisc(url: URL) {
        guard isEmpty else {
            actionError = "Eject disc first"
            return
        }

        actionError = nil
        isProcessing = true
        Task {
            let result = await discClient.insertDisc(drive: Int(drive.drive), url: url)
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[StorageModeView] Insert failed: \(error.localizedDescription)")
                    actionError = error.localizedDescription
                }
            }
        }
    }

    private func ejectDisc() {
        guard isLoaded else { return }

        actionError = nil
        isProcessing = true
        Task {
            let result = await discClient.ejectDisc(drive: Int(drive.drive), immediate: false)
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[StorageModeView] Eject failed: \(error.localizedDescription)")
                    actionError = error.localizedDescription
                }
            }
        }
    }

    private func forceEject() {
        actionError = nil
        isProcessing = true
        Task {
            let result = await discClient.ejectDisc(drive: Int(drive.drive), immediate: true)
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[StorageModeView] Force eject failed: \(error.localizedDescription)")
                    actionError = error.localizedDescription
                }
            }
        }
    }

    private func cancelEject() {
        actionError = nil
        isProcessing = true
        Task {
            let result = await discClient.cancelEject(drive: Int(drive.drive))
            await MainActor.run {
                isProcessing = false
                if case .failure(let error) = result {
                    NSLog("[StorageModeView] Cancel eject failed: \(error.localizedDescription)")
                    actionError = error.localizedDescription
                }
            }
        }
    }

    /// Start or stop the wait that decides when to offer Force Eject.
    private func trackEjectProgress() {
        ejectWaitTask?.cancel()
        ejectWaitTask = nil

        guard isEjecting else {
            ejectIsStalled = false
            return
        }

        ejectIsStalled = false
        ejectWaitTask = Task {
            try? await Task.sleep(nanoseconds: UInt64(Self.stalledEjectDelay * 1_000_000_000))
            guard !Task.isCancelled else { return }
            await MainActor.run { ejectIsStalled = true }
        }
    }

    /// How long an eject may be pending before the drive counts as busy. Long
    /// enough that an ordinary eject -- which waits 500ms for the motor --
    /// completes without ever showing the offer.
    private static let stalledEjectDelay: Double = 2.0
}

#if DEBUG
struct StorageModeView_Previews: PreviewProvider {
    static var previews: some View {
        StorageModeView(discClient: DiscClient(),
                        peripheralsClient: PeripheralsClient(),
                        indicatorClient: IndicatorClient(),
                        isServerLocal: true)
            .frame(width: 220, height: 300)
            .background(Color(nsColor: .windowBackgroundColor))
    }
}
#endif
