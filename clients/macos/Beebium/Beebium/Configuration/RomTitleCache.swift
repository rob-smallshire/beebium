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

/// Transient cache of parsed ROM headers, keyed by image reference, for the
/// Memory section to show a ROM's title/version rather than its filename.
///
/// This is pure UI adornment over the draft (the draft itself stores only the
/// image reference), so it lives outside the draft and is rebuilt on demand. A
/// `describe-rom` is run at most once per distinct image.
@MainActor
final class RomTitleCache: ObservableObject {
    @Published private(set) var byImage: [String: RomHeaderInfo] = [:]

    /// Runs `describe-rom` for an image, supplied by the host. Nil disables
    /// resolution (e.g. before any preset executable is known).
    var resolver: (@MainActor (String) async -> RomHeaderInfo?)?

    /// Resolve an image's header if not already cached. Safe to call repeatedly.
    func resolve(_ image: String) async {
        guard !image.isEmpty, byImage[image] == nil, let resolver else { return }
        if let info = await resolver(image), info.recognised, !info.title.isEmpty {
            byImage[image] = info
        }
    }

    /// The display text for a socket's contents: the resolved ROM title (and
    /// version) when known, otherwise the image's filename, otherwise the
    /// content's own label (e.g. "Empty", "Sideways RAM").
    func displayText(for content: SidewaysSocketEditor.Content) -> String {
        guard content.kind != .empty, let image = content.image, !image.isEmpty else {
            return content.displayLabel
        }
        if let info = byImage[image], !info.title.isEmpty {
            if !info.version.isEmpty { return "\(info.title) \(info.version)" }
            return info.title
        }
        return URL(fileURLWithPath: image).lastPathComponent
    }

    /// The resolved ROM kinds for a socket's image (language/service/romfs),
    /// empty when unknown.
    func kinds(for content: SidewaysSocketEditor.Content) -> [String] {
        guard let image = content.image, let info = byImage[image] else { return [] }
        return info.kinds
    }
}
