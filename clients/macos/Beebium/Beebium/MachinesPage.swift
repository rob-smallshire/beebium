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
import AppKit

/// The Welcome window's Machines page: the saved machines the user can reopen,
/// each with its last-screen thumbnail and when it was last used. Opening one
/// goes through the same flow as File > Open..., so a machine already running
/// is brought forward rather than launched twice.
struct MachinesPage: View {
    @ObservedObject var model: SavedMachinesModel
    let gridHeight: CGFloat
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        VStack(spacing: 16) {
            Group {
                if model.machines.isEmpty {
                    emptyState
                } else {
                    machinesGrid
                }
            }
            .frame(minHeight: 100, idealHeight: gridHeight, maxHeight: gridHeight)

            // Clear well away from New Machine... (destructive-ish, so it sits
            // left); New Machine... bottom right as on the Presets page. The
            // shared bar gives both the same padding from the window's edges.
            WelcomeBottomBar {
                Button("Clear") { model.clear() }
                    .disabled(model.machines.isEmpty)
                Spacer()
                NewMachineButton()
            }
        }
        .task {
            // The badge is a machine's lock, so it is only as fresh as the last
            // reload. Load when the page appears, then reload on a modest timer
            // while it stays visible -- a backstop for machines started outside
            // the app (the CLI) or a stale lock the server's sweep has not yet
            // cleared. The task is cancelled when the page goes away, which stops
            // the timer. Machine windows opening and closing are caught off-view
            // by the model; these are coalesced with those.
            await model.reload()
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 3_000_000_000)
                if Task.isCancelled { break }
                await model.reload()
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .savedMachinesDidChange)) { _ in
            Task { await model.reload() }
        }
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in
            // The Welcome window coming forward while this page is up is a cue the
            // badges may be stale (a machine was started or stopped elsewhere).
            Task { await model.reload() }
        }
    }

    private var machinesGrid: some View {
        ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 176, maximum: 176), spacing: 16)],
                      spacing: 16) {
                ForEach(model.machines) { machine in
                    MachineCard(machine: machine,
                                onOpen: { open(machine) },
                                onReveal: { reveal(machine) })
                }
            }
            .padding(.horizontal, 24)
        }
    }

    private var emptyState: some View {
        VStack(spacing: 8) {
            Image(systemName: "externaldrive.badge.questionmark")
                .font(.system(size: 36))
                .foregroundColor(.secondary)
            Text("No saved machines yet")
                .foregroundColor(.secondary)
            Text("Save a running machine and it appears here.")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, minHeight: 180)
    }

    private func open(_ machine: MachineDescription) {
        let url = URL(fileURLWithPath: machine.machineDirpath)
        Task { @MainActor in
            await SavedMachineOpening.open(openedURL: url,
                                           openMainWindow: { openWindow(id: "main") })
        }
    }

    private func reveal(_ machine: MachineDescription) {
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: machine.machineDirpath)])
    }
}

/// A clickable card for one saved machine: its thumbnail, name and last-used
/// time, with Open and Reveal in Finder in a context menu.
struct MachineCard: View {
    let machine: MachineDescription
    let onOpen: () -> Void
    let onReveal: () -> Void

    @State private var thumbnail: NSImage?

    private static let lastUsedFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter
    }()

    var body: some View {
        Button(action: onOpen) {
            VStack(spacing: 0) {
                thumbnailView
                    .frame(width: 160, height: 120)
                    .clipped()
                VStack(spacing: 2) {
                    Text(machine.name)
                        .font(.callout)
                        .fontWeight(.medium)
                        .lineLimit(2)
                        .multilineTextAlignment(.center)
                    Text(subtitle)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }
                .frame(width: 144, height: 48, alignment: .top)
                .padding(.vertical, 6)
                .padding(.horizontal, 8)
            }
            .background(Color(nsColor: .controlBackgroundColor))
            .cornerRadius(8)
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(Color(nsColor: .separatorColor), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
        .overlay(alignment: .topTrailing) {
            if machine.isRunning { runningBadge }
        }
        .contextMenu {
            Button("Open") { onOpen() }
            Button("Reveal in Finder") { onReveal() }
        }
        .task(id: machine.id) {
            thumbnail = Self.loadThumbnail(machine.thumbnailFilepath)
        }
    }

    /// The last-used time, or that the machine has not been run yet. Created
    /// time is not shown: what matters on this page is when it was last left.
    private var subtitle: String {
        if let lastUsed = machine.lastUsed,
           let date = ISO8601DateFormatter().date(from: lastUsed) {
            return "Last used \(Self.lastUsedFormatter.string(from: date))"
        }
        return "Not run yet"
    }

    private var runningBadge: some View {
        Text("Running")
            .font(.caption2)
            .fontWeight(.semibold)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Color.green.opacity(0.85))
            .foregroundColor(.white)
            .cornerRadius(4)
            .padding(6)
    }

    @ViewBuilder
    private var thumbnailView: some View {
        if let thumbnail {
            ZStack {
                Color.black
                Image(nsImage: thumbnail)
                    .resizable()
                    .interpolation(.high)
                    .antialiased(true)
                    .aspectRatio(contentMode: .fit)
            }
        } else {
            ZStack {
                Color.black
                Image(systemName: "desktopcomputer")
                    .font(.system(size: 32))
                    .foregroundColor(.gray)
            }
        }
    }

    /// Load the thumbnail PNG the server reported, or nil when there is none or
    /// it has gone. The path comes from describe-machine's thumbnail_filepath,
    /// which is absent until a machine has a captured screen, so a missing file
    /// is normal and shows the placeholder.
    private static func loadThumbnail(_ filepath: String?) -> NSImage? {
        guard let filepath, FileManager.default.fileExists(atPath: filepath) else { return nil }
        return NSImage(contentsOfFile: filepath)
    }
}
