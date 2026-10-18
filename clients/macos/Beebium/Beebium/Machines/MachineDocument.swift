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

import AppKit
import UniformTypeIdentifiers

/// The AppKit document object behind a machine window.
///
/// The server owns every byte in the machine directory; this object is only the
/// macOS document face of the running machine. Backing the window with an
/// `NSDocument` that has a file URL and autosaves in place is what makes AppKit
/// offer the standard title-bar popover -- the chevron beside the name with
/// Rename, Move To (Where), Tags and Locked -- which the user asked for in place
/// of the app's own right-click rename menu.
///
/// It never reads or writes the package itself. Save, Save As and Move call the
/// server; opening is the app's own flow (`application(_:open:)` ->
/// `OpenMachinePlan`), not `NSDocumentController`'s. AppKit is told the document
/// is never edited and autosave is a no-op, so no bytes are ever written from
/// here and no AppKit save-on-close prompt appears: the unsaved-changes dot and
/// that prompt stay on the window and in `WindowCloseCoordinator`, driven by the
/// server's `template_media_written` signal.
///
/// This is the document object; the on-disk package format is `MachinePackage`.
@MainActor
final class MachineDocument: NSDocument {
    private weak var systemClient: SystemClient?
    private let recents: MachineRecents

    override init() {
        recents = MachineRecents()
        super.init()
    }

    /// For tests: a document wired to a client and a recents store without a
    /// window.
    init(systemClient: SystemClient?, recents: MachineRecents) {
        self.systemClient = systemClient
        self.recents = recents
        super.init()
    }

    // MARK: - No bytes written here

    // Required for the title-bar document popover, but the server owns the bytes:
    // the document is never edited as far as AppKit is concerned, so no autosave
    // ever runs.
    override class var autosavesInPlace: Bool { true }
    override var isDocumentEdited: Bool { false }
    override var isLocked: Bool { false }

    override func save(to url: URL, ofType typeName: String,
                       for saveOperation: NSDocument.SaveOperationType,
                       completionHandler: @escaping (Error?) -> Void) {
        // Save and Save As are the server's SaveMachine, driven by the File menu
        // and the popover's Duplicate; an autosave is a no-op success. Nothing is
        // written to the package from AppKit.
        completionHandler(nil)
    }

    /// The window's title: the server's plain name, which for a saved machine is
    /// its document (directory) name but follows a server-pushed rename at once,
    /// before the file URL is re-synced. Falls back to AppKit's own name if the
    /// server has not said one yet.
    override var displayName: String! {
        get {
            let name = systemClient?.machineName ?? ""
            return name.isEmpty ? super.displayName : name
        }
        set { super.displayName = newValue }
    }

    // MARK: - Attach to a window / follow the server

    /// Put this document behind `window` so the title bar shows the document
    /// popover. The window is SwiftUI's; a window controller (its own, or a fresh
    /// one over it) ties the two together, and the shared document controller is
    /// told so the recents and popover machinery see a real document.
    func attach(to window: NSWindow, systemClient: SystemClient) {
        self.systemClient = systemClient
        fileType = UTType.beebiumMachine.identifier
        let controller = window.windowController ?? NSWindowController(window: window)
        addWindowController(controller)
        NSDocumentController.shared.addDocument(self)
        syncFromServer()
    }

    /// Reflect the server's current identity on the document and its window: the
    /// file URL (the saved directory; see `savedFileURL`), the title (the plain
    /// name), and the unsaved-changes dot (the server's template-media signal).
    /// The document is only attached to a saved machine, but a Save As to a new
    /// directory still moves the file URL here.
    func syncFromServer() {
        guard let systemClient else { return }
        let url = Self.savedFileURL(machineSaved: systemClient.machineSaved,
                                    machineDirpath: systemClient.machineDirpath)
        if fileURL != url { fileURL = url }
        for controller in windowControllers {
            controller.synchronizeWindowTitleWithDocumentName()
            controller.window?.isDocumentEdited = systemClient.templateMediaWritten
        }
    }

    /// The document's file URL for a server state: the saved directory, or none.
    /// A transient machine never has one -- its working directory is
    /// `machines/<uuid>`, deleted on shutdown, so it must never show as a proxy
    /// icon or breadcrumb (which is also why the document is not attached until
    /// the machine is saved). Pure.
    nonisolated static func savedFileURL(machineSaved: Bool, machineDirpath: String) -> URL? {
        (machineSaved && !machineDirpath.isEmpty) ? URL(fileURLWithPath: machineDirpath) : nil
    }

    // MARK: - Rename and Move (the popover's inline rename and Where)

    /// Where the title bar's inline rename and Move To both land, with `url` the
    /// chosen destination directory (its leaf is `<name>.beebium-machine`), the
    /// Where popup choosing the folder. The document is only attached to a saved
    /// machine, so it always has a directory to move: MoveMachine moves (and so
    /// renames) it in place, the window follows, and the recents entry moves with
    /// it; on failure the server's message is handed back for AppKit to present.
    /// Renaming a transient machine is a different act entirely (File > Rename
    /// Machine..., which does not save), so a move while unsaved is declined, not
    /// turned into a save.
    override func move(to url: URL, completionHandler: ((Error?) -> Void)? = nil) {
        guard let systemClient, fileURL != nil else {
            completionHandler?(fileURL == nil ? SystemClient.MachineMoveError.failed("") : nil)
            return
        }
        let previousDirpath = fileURL
        Task { @MainActor in
            switch await systemClient.moveMachine(toDirpath: url.path) {
            case .success(let newDirpath):
                syncFromServer()
                let newURL = URL(fileURLWithPath: newDirpath)
                if let previousDirpath,
                   previousDirpath.standardizedFileURL != newURL.standardizedFileURL {
                    recents.rename(from: previousDirpath, to: newURL)
                    NotificationCenter.default.post(name: .savedMachinesDidChange, object: nil)
                }
                completionHandler?(nil)
            case .failure(let error):
                completionHandler?(error)
            }
        }
    }

    // MARK: - Duplicate (the popover's Duplicate) -> Save As

    /// The popover's Duplicate is Save As: the server saves a copy under a new
    /// directory and uuid and the machine runs on from it, which is exactly what
    /// File > Save As... does. The default NSDocument duplicate would copy the
    /// package's bytes, which the server owns, so it is replaced.
    override func duplicate(_ sender: Any?) {
        guard let systemClient else { return }
        Task { @MainActor in await MachineSaving.saveAs(systemClient: systemClient) }
    }

    // MARK: - Lock

    // A running machine cannot be locked: the directory's immutable flag would
    // break the server's atomic writes inside it. There is no supported way to
    // hide just the Locked checkbox in the popover, so lock and unlock decline
    // with a message AppKit presents.
    override func lock(completionHandler: ((Error?) -> Void)? = nil) {
        completionHandler?(Self.lockUnsupported)
    }
    override func unlock(completionHandler: ((Error?) -> Void)? = nil) {
        completionHandler?(Self.lockUnsupported)
    }
    private static let lockUnsupported = NSError(
        domain: "com.beebium.machine", code: 1,
        userInfo: [NSLocalizedDescriptionKey:
                    "A machine cannot be locked while it is running."])
}
