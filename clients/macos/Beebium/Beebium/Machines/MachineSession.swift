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

import Foundation
import AppKit

/// The document state of one machine window: whether it is a transient machine
/// or a saved one (and where), and whether the guest has changed it since the
/// last save. A transient machine has no file yet; a saved machine's directory
/// is its document. Pure and Equatable, so the transitions are testable.
struct MachineDocumentState: Equatable {
    /// The saved machine directory, or nil while the machine is transient.
    var savedDirpath: URL?

    /// The server's `template_media_written` signal, mirrored here: the guest
    /// has written a medium copied from the template since the last save. It is
    /// the one dirty signal -- there is no separate flag -- and drives both the
    /// window's unsaved-changes dot and the close-time "Save this machine?"
    /// prompt (sections 4.3 and 4.7). Cleared by a save.
    var hasUnsavedChanges: Bool = false

    /// Whether the machine has been saved to a directory.
    var isSaved: Bool { savedDirpath != nil }

    /// The file the window represents (its proxy icon), or nil for a transient
    /// machine that has no file yet.
    var representedURL: URL? { savedDirpath }

    /// Record a save to (or open from) `dirpath`: the directory becomes the
    /// machine's document and it starts with no unsaved changes.
    mutating func markSaved(to dirpath: URL) {
        savedDirpath = dirpath
        hasUnsavedChanges = false
    }
}

/// Owns the document state of one machine window and ties it to the recent-
/// machines list.
///
/// This is deliberately a plain main-actor object, not an `NSDocument`. The
/// live server owns the machine directory and every file in it, Save is an RPC
/// to that server, and the unsaved-changes signal arrives on the status stream
/// -- none of which fits `NSDocument`'s model of a document that owns its own
/// bytes and reads, writes and autosaves them itself (see docs section 4.11 and
/// the step-4 plan). The Save and Open actions that call the server arrive with
/// the RPC surface; this holds the state they move through.
@MainActor
final class MachineSession: ObservableObject {
    @Published private(set) var documentState: MachineDocumentState
    private let recents: MachineRecents

    init(documentState: MachineDocumentState = .init(),
         recents: MachineRecents? = nil) {
        self.documentState = documentState
        self.recents = recents ?? MachineRecents()
    }

    /// Record that the machine has been saved to, or opened from, a directory:
    /// the directory becomes the window's document, is remembered in recents,
    /// and the machine starts with no unsaved changes.
    func adoptSavedDirectory(_ dirpath: URL) {
        documentState.markSaved(to: dirpath)
        recents.remember(machineDirpath: dirpath)
    }

    /// Reflect the server's template-media-written signal.
    func setHasUnsavedChanges(_ changed: Bool) {
        documentState.hasUnsavedChanges = changed
    }
}

/// Applies a machine window's document state to its AppKit window: the proxy
/// icon (the represented URL) and the unsaved-changes dot. The Open and Save
/// paths call this whenever the state changes.
enum MachineWindowChrome {
    static func apply(_ state: MachineDocumentState, to window: NSWindow) {
        window.representedURL = state.representedURL
        window.isDocumentEdited = state.hasUnsavedChanges
    }
}
