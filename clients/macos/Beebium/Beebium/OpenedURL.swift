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

/// What a URL handed to the app means. The app is opened with a URL from two
/// entry points -- the main scene's `.onOpenURL` and the delegate's
/// `application(_:open:)` -- and both must read a given URL the same way. This
/// pure classifier is the single place that decides, so the two cannot diverge.
enum OpenedURL: Equatable {
    /// A `beebium://connect?...` deep link: attach a window to a server.
    case connect(DeepLinkConnectRequest)
    /// A saved machine to open: a file URL whose extension is
    /// `beebium-machine`, either the package directory or its inner document
    /// file. The URL is passed on untouched; the open flow resolves it to the
    /// machine directory from the filesystem.
    case savedMachine(URL)
    /// Anything else -- a URL the app has no action for.
    case ignored

    /// Classify a URL the app was opened with. Connect links are recognised
    /// first; then a file URL carrying the saved-machine extension; everything
    /// else is ignored.
    static func classify(_ url: URL) -> OpenedURL {
        if let request = DeepLink.parseConnect(url) {
            return .connect(request)
        }
        if url.isFileURL {
            // A package directory can arrive with a trailing slash (Finder and
            // LaunchServices add one); resolving through the path reads the
            // extension the same for the package directory and its inner file.
            let fileExtension = URL(fileURLWithPath: url.path).pathExtension.lowercased()
            if fileExtension == MachinePackage.fileExtension {
                return .savedMachine(url)
            }
        }
        return .ignored
    }
}
