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

/// Remembers, per file-dialog function, the directory the user last chose, so
/// every open and save panel reopens where that function last left off, rather
/// than leaning on the platform's per-app memory (which every panel shares).
/// Client specification 3.6.
///
/// Open and save are distinct functions here, so they never share a location:
/// one opens media from wherever a collection lives, the other saves new media
/// where the user keeps their own work. On first use, or when a remembered
/// directory has gone (or, for a save, is not writable), a panel starts in the
/// platform's Documents folder. A directory inside one of the server's template
/// trees (blank-discs, blank-tapes) or the per-user state tree is never started
/// in nor remembered; a file the user chooses there anyway is honoured, and the
/// remembered location is left unchanged.
@MainActor
final class DialogLocations {
    static let shared = DialogLocations()

    /// A file dialog, named by its function and direction. Open and save are
    /// separate cases, so a function that both opens and saves keeps two.
    enum Function: String, CaseIterable {
        case insertDisc
        case insertTape
        case newBlankDisc
        case newBlankTape
        case openMachine
        case saveMachine
        case importPresets
        case exportPreset
        case chooseRom
        case chooseTape
        case chooseHardDisc
        case chooseFloppy
        case chooseMapping
        case extensionFileOpen
        case extensionFileSave

        /// Whether the dialog writes a file, so its remembered start directory
        /// must still be writable to be reused.
        var isSave: Bool {
            switch self {
            case .newBlankDisc, .newBlankTape, .saveMachine, .exportPreset, .extensionFileSave:
                return true
            case .insertDisc, .insertTape, .openMachine, .importPresets, .chooseRom, .chooseTape,
                 .chooseHardDisc, .chooseFloppy, .chooseMapping, .extensionFileOpen:
                return false
            }
        }
    }

    private let defaults: UserDefaults
    private let fileManager: FileManager
    private let excludedRoots: [URL]
    private let keyPrefix = "fileDialogDirectory."

    /// `defaults`, `fileManager` and `excludedRoots` are injectable so tests run
    /// against an isolated suite and a known set of excluded directories.
    init(defaults: UserDefaults = .standard,
         fileManager: FileManager = .default,
         excludedRoots: [URL]? = nil) {
        self.defaults = defaults
        self.fileManager = fileManager
        self.excludedRoots = excludedRoots ?? Self.defaultExcludedRoots(fileManager)
    }

    /// Where a panel for `function` should start: the remembered directory if it
    /// still exists (and, for a save, is writable and not excluded), else the
    /// platform's Documents folder.
    func startDirectory(for function: Function) -> URL {
        if let remembered = rememberedDirectory(for: function),
           isUsable(remembered, forSave: function.isSave) {
            return remembered
        }
        return documentsDirectory()
    }

    /// Record the directory of the `url` the user chose in `function` -- its
    /// containing folder -- unless that folder is an excluded (template or state)
    /// directory, in which case the remembered location is left unchanged.
    func remember(_ url: URL, for function: Function) {
        let directory = url.deletingLastPathComponent().resolvingSymlinksInPath().standardizedFileURL
        guard !isExcluded(directory) else { return }
        defaults.set(directory.path, forKey: keyPrefix + function.rawValue)
    }

    // MARK: - Internals

    private func rememberedDirectory(for function: Function) -> URL? {
        guard let path = defaults.string(forKey: keyPrefix + function.rawValue), !path.isEmpty else {
            return nil
        }
        return URL(fileURLWithPath: path, isDirectory: true)
    }

    private func isUsable(_ directory: URL, forSave: Bool) -> Bool {
        if isExcluded(directory) { return false }
        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: directory.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            return false
        }
        if forSave && !fileManager.isWritableFile(atPath: directory.path) { return false }
        return true
    }

    /// Whether `directory` is, or is inside, one of the excluded roots.
    private func isExcluded(_ directory: URL) -> Bool {
        let target = directory.resolvingSymlinksInPath().standardizedFileURL.path
        for root in excludedRoots {
            let base = root.resolvingSymlinksInPath().standardizedFileURL.path
            if target == base || target.hasPrefix(base + "/") { return true }
        }
        return false
    }

    private func documentsDirectory() -> URL {
        (try? fileManager.url(for: .documentDirectory, in: .userDomainMask,
                              appropriateFor: nil, create: false))
            ?? fileManager.homeDirectoryForCurrentUser
    }

    /// The directories a chosen location is never remembered in nor started in:
    /// the hidden application-support and caches trees (the per-user state tree
    /// the server manages lives under application support), and, for a server
    /// bundled in this app, its own blank-discs and blank-tapes template trees.
    private static func defaultExcludedRoots(_ fileManager: FileManager) -> [URL] {
        var roots: [URL] = []
        for directory in [FileManager.SearchPathDirectory.applicationSupportDirectory, .cachesDirectory] {
            if let url = try? fileManager.url(for: directory, in: .userDomainMask,
                                              appropriateFor: nil, create: false) {
                roots.append(url.standardizedFileURL)
            }
        }
        if let frameworks = Bundle.main.privateFrameworksURL {
            roots.append(frameworks.appendingPathComponent("blank-tapes").standardizedFileURL)
            roots.append(frameworks.appendingPathComponent("blank-discs").standardizedFileURL)
        }
        return roots
    }
}
