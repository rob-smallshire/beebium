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

import XCTest
import AppKit
@testable import Beebium

/// The thumbnail cache's bookkeeping, tested with an injected loader so no
/// server runs: a preset with no thumbnail is answered without a fetch, an
/// image is fetched once and kept, and a miss is remembered rather than
/// retried.
@MainActor
final class PresetThumbnailCacheTests: XCTestCase {

    /// Counts how often the injected loader runs (a reference type so the
    /// loader closure can tally calls).
    private final class Counter {
        var count = 0
    }

    private func preset(id: String, hasThumbnail: Bool) -> MachinePreset {
        MachinePreset(
            id: UUID(), presetId: id, name: id,
            coreExecutablePath: "/srv/beebium-model-b",
            presetFilepath: "/presets/\(id).beebium-preset",
            source: .systemPreset, machineDescription: nil, modelName: "BBC Model B",
            modelDescription: nil, releaseDate: nil, hasThumbnail: hasThumbnail)
    }

    /// A small, real PNG the cache can decode into an NSImage.
    private func samplePNGData() -> Data {
        let image = NSImage(size: NSSize(width: 2, height: 2))
        image.lockFocus()
        NSColor.red.drawSwatch(in: NSRect(x: 0, y: 0, width: 2, height: 2))
        image.unlockFocus()
        let tiff = image.tiffRepresentation!
        return NSBitmapImageRep(data: tiff)!.representation(using: .png, properties: [:])!
    }

    func testDoesNotFetchWhenPresetHasNoThumbnail() async {
        let counter = Counter()
        let cache = PresetThumbnailCache { _ in counter.count += 1; return nil }
        let p = preset(id: "a", hasThumbnail: false)

        let first = await cache.thumbnail(for: p)
        let second = await cache.thumbnail(for: p)

        XCTAssertNil(first)
        XCTAssertNil(second)
        XCTAssertEqual(counter.count, 0, "a preset with no thumbnail must not ask the server")
    }

    func testFetchesOnceAndCachesTheImage() async {
        let counter = Counter()
        let data = samplePNGData()
        let cache = PresetThumbnailCache { _ in counter.count += 1; return data }
        let p = preset(id: "a", hasThumbnail: true)

        let first = await cache.thumbnail(for: p)
        let second = await cache.thumbnail(for: p)

        XCTAssertNotNil(first)
        XCTAssertTrue(first === second, "the cached image is reused, not re-decoded")
        XCTAssertEqual(counter.count, 1, "the server is asked at most once per preset")
    }

    func testRemembersAMissWithoutRetrying() async {
        let counter = Counter()
        let cache = PresetThumbnailCache { _ in counter.count += 1; return nil }
        let p = preset(id: "a", hasThumbnail: true)

        let first = await cache.thumbnail(for: p)
        let second = await cache.thumbnail(for: p)
        XCTAssertNil(first)
        XCTAssertNil(second)
        XCTAssertEqual(counter.count, 1, "a failed fetch is remembered, not retried")
    }
}
