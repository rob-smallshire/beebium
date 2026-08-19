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

/// The disc image formats a floppy drive will accept.
///
/// One definition serves the file pickers, the drag-and-drop validation and
/// the preset editor, so that a format added here reaches all of them and
/// none of them can drift into accepting a different set from the others.
///
/// TODO: Query server for supported extensions via gRPC API (Phase 9a)
enum DiscImageTypes {
    /// Filename extensions, lowercased, without the leading dot.
    static let extensions: Set<String> = [
        "ssd",  // DFS single-sided disc
        "dsd",  // DFS double-sided disc
        "adf",  // ADFS disc (auto-detect geometry)
        "adl",  // ADFS large
        "adm",  // ADFS medium
        "ads",  // ADFS small
        "hfe",  // HFE flux-level disc image
        "img",  // Raw disc image
    ]

    /// The same formats as content types, for file pickers and for asking a
    /// drag what it is carrying.
    ///
    /// An extension macOS has no declaration for resolves to a dynamic type
    /// derived from the extension itself, which is still what Finder puts on
    /// the dragging pasteboard, so the comparison holds. An extension that
    /// resolves to nothing at all is dropped: substituting a permissive
    /// stand-in would widen a file picker to every file on the disc.
    static let contentTypes: [UTType] = extensions.sorted().compactMap {
        UTType(filenameExtension: $0)
    }
}
