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

/// Supplies preset thumbnails to the UI, fetched lazily from the server with
/// `show-preset --thumbnail` and kept in memory. The server owns the image, so
/// the client never reads a thumbnail file itself; it asks here, by preset, and
/// gets the image or nil.
///
/// Each preset's image is fetched at most once: a hit is kept, and a miss (the
/// preset has no thumbnail, or the fetch failed) is remembered too, so a card
/// that scrolls in and out does not re-ask. A preset the listing says has no
/// thumbnail is answered nil without asking the server at all.
@MainActor
final class PresetThumbnailCache: ObservableObject {
    /// Loads the PNG bytes of a preset's thumbnail, or nil if there are none.
    /// Injectable so the cache's bookkeeping is testable without a server.
    typealias Loader = (MachinePreset) async -> Data?

    private let load: Loader
    private var images: [String: NSImage] = [:]
    /// Preset ids already resolved, hit or miss, so a miss is not retried.
    private var resolved: Set<String> = []

    init(load: @escaping Loader = PresetThumbnailCache.cliLoad) {
        self.load = load
    }

    /// The preset's thumbnail, or nil when it has none or the fetch fails. The
    /// server is asked at most once per preset id; the result is memoised.
    func thumbnail(for preset: MachinePreset) async -> NSImage? {
        if let image = images[preset.presetId] { return image }
        if resolved.contains(preset.presetId) { return nil }
        guard preset.hasThumbnail else {
            resolved.insert(preset.presetId)
            return nil
        }
        let data = await load(preset)
        // Another caller for the same id may have resolved it while this fetch
        // was in flight; its image, if any, is already stored, so only fill a
        // gap rather than overwrite.
        resolved.insert(preset.presetId)
        if images[preset.presetId] == nil, let data, let image = NSImage(data: data) {
            images[preset.presetId] = image
        }
        return images[preset.presetId]
    }

    /// The default loader: run `show-preset --thumbnail` into a temporary file
    /// and read the PNG bytes back. The preset's id resolves across the system
    /// and user stores server-side.
    static func cliLoad(_ preset: MachinePreset) async -> Data? {
        let dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("Beebium", isDirectory: true)
            .appendingPathComponent("thumbnails", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let output = dir.appendingPathComponent("\(UUID().uuidString).png")
        defer { try? FileManager.default.removeItem(at: output) }

        let error = await CoreCLI().showPresetThumbnail(
            idOrPath: preset.presetId,
            outputPath: output.path,
            executablePath: preset.coreExecutablePath)
        if let error {
            // The listing said this preset has a thumbnail, so a failure to
            // write it is a real fault, not the expected "no thumbnail" case
            // (which the caller avoids via hasThumbnail). Say why before
            // falling back to the placeholder.
            NSLog("[PresetThumbnailCache] show-preset --thumbnail failed for \(preset.presetId): \(error)")
            return nil
        }
        return try? Data(contentsOf: output)
    }
}
