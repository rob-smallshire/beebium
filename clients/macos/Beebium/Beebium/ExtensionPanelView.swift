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

/// Renders the server-driven UI for one named extension. Subscribes to
/// the extension's View stream when the panel appears; unsubscribes
/// when it disappears. Renders nothing when no View has arrived yet
/// (the panel collapses to zero height), so it's safe to drop into a
/// VStack regardless of whether the named extension is loaded.
struct ExtensionPanelView: View {
    @ObservedObject var client: ExtensionUiClient
    let extensionID: String

    /// Whether the server shares this host's filesystem; threaded to the renderer
    /// so a FileReference offers "Reveal in Finder" only on a local server. The
    /// default false (no reveal) suits call sites without the fingerprint to hand.
    var isServerLocal: Bool = false

    var body: some View {
        // VStack rather than Group: Group is a transparent passthrough,
        // and when its conditional content yields nothing the
        // .onAppear modifier has no concrete view to attach to. VStack
        // is a real layout container so .onAppear fires reliably even
        // when the panel has no content yet (which is the common case
        // before the first View arrives).
        VStack(alignment: .leading, spacing: 8) {
            if let view = client.views[extensionID] {
                ExtensionViewRenderer(
                    control: view.root,
                    viewRevision: view.viewRevision,
                    dispatch: { controlId, payload in
                        Task {
                            await client.dispatch(
                                extension: extensionID,
                                controlId: controlId,
                                viewRevision: view.viewRevision,
                                payload: payload
                            )
                        }
                    },
                    isServerLocal: isServerLocal
                )
            } else if let error = client.errors[extensionID] {
                // Only show errors after a stream actually started; a
                // missing extension produces NOT_FOUND from gRPC, which
                // surfaces here as an error string. Callers that want
                // to silently hide unloaded extensions can ignore the
                // .errors map and only render when .views has an entry.
                Text(error)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .onAppear { client.subscribe(to: extensionID) }
        .onDisappear { client.unsubscribe(from: extensionID) }
    }
}
