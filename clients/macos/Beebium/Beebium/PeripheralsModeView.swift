// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// Sidebar mode showing the configured peripheral extensions in a
/// hierarchical tree rooted at the built-in extension points (1 MHz
/// bus, user port, ...). Multi-port devices appear once under their
/// primary attachment with the other extension points shown as
/// secondary badges.
///
/// For each node whose extension implements ExtensionUi (the server-
/// side has_ui flag arrives via PeripheralExtensionService), the row
/// title is followed by the extension's declarative panel rendered
/// via ExtensionPanelView -- the same framework the Network sidebar
/// uses for AUN and Piconet.
struct PeripheralsModeView: View {
    @ObservedObject var client: PeripheralsClient
    @ObservedObject var extensionUiClient: ExtensionUiClient
    @ObservedObject var serialClient: SerialClient
    /// Whether the server shares this host's filesystem; threaded to a peripheral
    /// extension's panel so a FileReference can gate "Reveal in Finder" (#170).
    var isServerLocal: Bool = false

    var body: some View {
        Group {
            if !client.isLoaded {
                loadingView
            } else if let error = client.errorMessage,
                      client.tree.groups.isEmpty,
                      client.tree.orphans.isEmpty {
                errorView(error)
            } else if client.tree.groups.isEmpty && client.tree.orphans.isEmpty {
                emptyView
            } else {
                treeContent
            }
        }
    }

    // MARK: - States

    private var loadingView: some View {
        VStack(spacing: 8) {
            ProgressView()
            Text("Loading...")
                .font(.caption)
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func errorView(_ message: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 24))
                .foregroundColor(.yellow)
            Text("Couldn't load peripherals")
                .font(.headline)
                .foregroundColor(.secondary)
            Text(message)
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var emptyView: some View {
        VStack(spacing: 12) {
            Image(systemName: "puzzlepiece.extension")
                .font(.system(size: 32))
                .foregroundColor(.secondary)
            Text("No Peripherals")
                .font(.headline)
                .foregroundColor(.secondary)
            Text("This machine is configured without any external peripherals.")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Tree

    private var treeContent: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                ForEach(Array(client.tree.groups.enumerated()),
                        id: \.element.id) { index, group in
                    if index > 0 {
                        Divider().padding(.horizontal, 12)
                    }
                    ExtensionPointSection(group: group,
                                          extensionUiClient: extensionUiClient,
                                          serialClient: serialClient,
                                          isServerLocal: isServerLocal)
                }
                if !client.tree.orphans.isEmpty {
                    if !client.tree.groups.isEmpty {
                        Divider().padding(.horizontal, 12)
                    }
                    OrphanSection(orphans: client.tree.orphans,
                                  extensionUiClient: extensionUiClient,
                                  isServerLocal: isServerLocal)
                }
            }
            .padding(.vertical, 8)
        }
    }
}

// MARK: - View helpers

private extension View {
    /// Apply SwiftUI's `.help(_:)` modifier only when `text` is
    /// non-empty. Avoids attaching an empty help string, which
    /// accessibility tools may announce as an empty hint.
    @ViewBuilder
    func helpIfPresent(_ text: String) -> some View {
        if text.isEmpty {
            self
        } else {
            self.help(text)
        }
    }
}

// MARK: - Sections

private struct ExtensionPointSection: View {
    let group: PeripheralTree.ExtensionPointGroup
    @ObservedObject var extensionUiClient: ExtensionUiClient
    @ObservedObject var serialClient: SerialClient
    var isServerLocal: Bool = false

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            // Tier 1: the extension point is the section header -- the most
            // prominent element, so the extensions read as nested beneath it.
            Text(PeripheralLabels.extensionPoint(group.extensionPoint))
                .font(.headline)
                .foregroundColor(.primary)
                .padding(.horizontal, 16)
                .padding(.top, 10)
                .padding(.bottom, 2)

            // Interface (port) details, shown before the extension details: the
            // on-board serial hardware's own state (connector standard + the
            // emulated BBC RS423 TX/RX baud, distinct from any host-side rate).
            // Indented under the header, a readable secondary line.
            if group.extensionPoint == "serial-port", let label = serialPortInterface {
                Text(label)
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .padding(.leading, 32)
                    .padding(.trailing, 16)
                    .padding(.bottom, 4)
            }

            ForEach(group.nodes) { node in
                PeripheralNodeRow(node: node,
                                  depth: 0,
                                  extensionUiClient: extensionUiClient,
                                  isServerLocal: isServerLocal)
            }
        }
    }

    // The serial port's interface line, e.g. "RS423 Tx 75 / Rx 1200 baud".
    // nil when the machine has no serial socket. The baud is the emulated BBC
    // (Serial ULA) rate captured while RS423 is selected -- never the cassette
    // rate. Until RS423 has been selected we show just the connector standard.
    private var serialPortInterface: String? {
        guard serialClient.hasSerialSocket else { return nil }
        let connector = serialClient.connector.isEmpty ? "RS423" : serialClient.connector
        guard serialClient.hasRs423Rates else { return connector }
        return "\(connector) Tx \(serialClient.rs423TxBaud) / Rx \(serialClient.rs423RxBaud) baud"
    }
}

private struct OrphanSection: View {
    let orphans: [PeripheralNode]
    @ObservedObject var extensionUiClient: ExtensionUiClient
    var isServerLocal: Bool = false

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Unattached")
                .font(.headline)
                .foregroundColor(.primary)
                .padding(.horizontal, 16)
                .padding(.top, 10)
                .padding(.bottom, 2)

            ForEach(orphans) { node in
                PeripheralNodeRow(node: node,
                                  depth: 0,
                                  extensionUiClient: extensionUiClient,
                                  isServerLocal: isServerLocal)
            }
        }
    }
}

// MARK: - Row

private struct PeripheralNodeRow: View {
    let node: PeripheralNode
    let depth: Int
    @ObservedObject var extensionUiClient: ExtensionUiClient
    var isServerLocal: Bool = false

    private var leadingPadding: CGFloat {
        // Extensions sit one level in from their section header (which is at
        // 16pt), so a top-level extension starts at 32pt and its own details
        // (the ExtensionUi panel) at 48pt -- making the extension visibly
        // subservient to its extension point. +16pt per further nesting level.
        32 + CGFloat(depth) * 16
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                // Tier 2: the extension name -- semibold so it reads as a header
                // above its own (regular-weight) detail rows in the panel below.
                Text(node.displayName)
                    .font(.body.weight(.semibold))
                Spacer()
                ForEach(node.secondaryAttachments, id: \.self) { point in
                    Text("also: \(PeripheralLabels.extensionPoint(point))")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 1)
                        .background(
                            Capsule().stroke(Color.secondary.opacity(0.4),
                                             lineWidth: 1)
                        )
                }
            }
            .padding(.leading, leadingPadding)
            .padding(.trailing, 16)
            .padding(.vertical, 4)
            // The manifest description is verbose catalogue text --
            // address ranges, chip part numbers, file-format notes --
            // that's too long for the inline row but useful when
            // someone genuinely wants to know what they're looking
            // at. Surface it as hover help on the row title only
            // (not on the whole VStack, which would catch the child
            // panel and child rows too). Skip the modifier entirely
            // for descriptionless extensions so accessibility tools
            // don't announce a spurious empty help string.
            .helpIfPresent(node.description)

            // ExtensionUi panel for this node, when the server signalled
            // that the extension implements one. ExtensionPanelView
            // manages its own subscribe/unsubscribe lifecycle via
            // onAppear/onDisappear; switching sidebar mode tears down
            // the view tree and releases the stream.
            if node.hasUI {
                ExtensionPanelView(client: extensionUiClient,
                                   extensionID: node.id,
                                   isServerLocal: isServerLocal)
                    .padding(.leading, leadingPadding + 16)
                    .padding(.trailing, 16)
                    .padding(.bottom, 4)
            }

            ForEach(node.children) { child in
                PeripheralNodeRow(node: child,
                                  depth: depth + 1,
                                  extensionUiClient: extensionUiClient,
                                  isServerLocal: isServerLocal)
            }
        }
    }
}

#if DEBUG
struct PeripheralsModeView_Previews: PreviewProvider {
    static var previews: some View {
        PeripheralsModeView(client: PeripheralsClient(),
                            extensionUiClient: ExtensionUiClient(),
                            serialClient: SerialClient())
            .frame(width: 280, height: 400)
    }
}
#endif
