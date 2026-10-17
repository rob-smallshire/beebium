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

/// The backing store for the recent-machines list. In the app this is the
/// standard macOS recent-documents list (NSDocumentController); in tests it is
/// an in-memory double. Kept behind a protocol so `MachineRecents` is exercised
/// without touching the system list.
@MainActor
protocol RecentDocumentsStore {
    func noteRecent(_ url: URL)
    func removeRecent(_ url: URL)
    var recentURLs: [URL] { get }
    func clearRecents()
}

/// The system recent-documents list. The app need not be NSDocument-based to
/// use it: `noteNewRecentDocumentURL`, `recentDocumentURLs` and
/// `clearRecentDocuments` work on the shared controller regardless, so the
/// recents list costs no store of our own and survives moves the way Finder
/// aliases do (section 4.8).
@MainActor
struct SystemRecentDocumentsStore: RecentDocumentsStore {
    func noteRecent(_ url: URL) {
        NSDocumentController.shared.noteNewRecentDocumentURL(url)
    }
    /// Drop one entry. NSDocumentController offers no single-remove, so the list
    /// is rebuilt without the target -- cleared and the survivors re-noted
    /// oldest-first, which keeps their order (noteNew puts each at the top). We
    /// only ever note machine directories, so nothing else is lost.
    func removeRecent(_ url: URL) {
        let target = url.standardizedFileURL.path
        let survivors = NSDocumentController.shared.recentDocumentURLs
            .filter { $0.standardizedFileURL.path != target }
        guard survivors.count != NSDocumentController.shared.recentDocumentURLs.count else { return }
        NSDocumentController.shared.clearRecentDocuments(nil)
        for survivor in survivors.reversed() {
            NSDocumentController.shared.noteNewRecentDocumentURL(survivor)
        }
    }
    var recentURLs: [URL] { NSDocumentController.shared.recentDocumentURLs }
    func clearRecents() {
        NSDocumentController.shared.clearRecentDocuments(nil)
    }
}

/// The app's list of recently opened or saved machines. Every Save and Open
/// records the machine's directory; the Welcome window's Machines page and
/// File > Open Recent read it back.
@MainActor
final class MachineRecents {
    private let store: RecentDocumentsStore

    init(store: RecentDocumentsStore? = nil) {
        self.store = store ?? SystemRecentDocumentsStore()
    }

    /// Record a machine directory as the most recent, standardised so the same
    /// machine opened by slightly different paths stays one entry.
    func remember(machineDirpath url: URL) {
        store.noteRecent(url.standardizedFileURL)
    }

    /// The recent machine directories, most recent first, with duplicates that
    /// differ only by path form collapsed. The system list already orders and
    /// dedups, so the extra dedup only guards the residual `/x` vs `/x/` pair a
    /// move can leave behind.
    var machineDirpaths: [URL] {
        Self.deduplicatedByPath(store.recentURLs)
    }

    /// Move a renamed machine's recents entry from its old directory to its new
    /// one: the old path no longer exists on disk, so drop it and record the
    /// new. Order is otherwise preserved, with the renamed machine now most
    /// recent.
    func rename(from oldURL: URL, to newURL: URL) {
        store.removeRecent(oldURL)
        store.noteRecent(newURL.standardizedFileURL)
    }

    func clear() {
        store.clearRecents()
    }

    /// Collapse URLs that resolve to the same file path, keeping the first
    /// occurrence so the most-recent-first order is preserved. Pure.
    static func deduplicatedByPath(_ urls: [URL]) -> [URL] {
        var seen = Set<String>()
        var result: [URL] = []
        for url in urls where seen.insert(url.standardizedFileURL.path).inserted {
            result.append(url)
        }
        return result
    }
}
