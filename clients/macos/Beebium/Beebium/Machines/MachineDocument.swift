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
import UniformTypeIdentifiers

/// What a saved machine is to the app: a `.beebium-machine` document. On macOS
/// it is a document package -- a directory Finder shows as one item and opens
/// in Beebium -- and on every platform the directory also carries a small
/// openable document file of the same name inside it. The server runs the
/// directory; the app only has to resolve whatever the user opened down to that
/// directory and hand it to `start --machine`.
enum MachineDocument {

    /// The filename extension shared by the machine directory and its inner
    /// document file (both named `<name>.beebium-machine`). The server never
    /// relies on it -- it identifies a machine by its `machine.json` -- so it is
    /// only how the app and Finder recognise one.
    static let fileExtension = "beebium-machine"

    /// Resolve a path the app was handed -- the Open panel, a drop, a recents
    /// entry, a double-clicked document file -- to the machine DIRECTORY to
    /// launch.
    ///
    /// The directory and its inner document file share a name, so they are told
    /// apart by `isDirectory`: a directory is the machine itself (the macOS
    /// package, or a plain folder on Windows and Linux); a regular file is the
    /// inner document, whose parent directory is the machine. Pure, so the
    /// resolution is testable without touching the filesystem.
    static func machineDirectory(forOpenedPath url: URL, isDirectory: Bool) -> URL {
        isDirectory ? url : url.deletingLastPathComponent()
    }

    /// `machineDirectory(forOpenedPath:isDirectory:)` with the directory flag
    /// read from the filesystem, for the live Open/drop paths.
    static func machineDirectory(forOpenedPath url: URL) -> URL {
        let isDirectory = (try? url.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory ?? false
        return machineDirectory(forOpenedPath: url, isDirectory: isDirectory)
    }
}

extension UTType {
    /// The saved-machine document package: a `.beebium-machine` directory that
    /// Finder shows as a single item and opens in Beebium. Declared as an
    /// exported type in the app's Info.plist (see project.yml), conforming to
    /// `com.apple.package`, so this resolves to that declaration.
    static let beebiumMachine = UTType(exportedAs: "com.beebium.machine")
}
