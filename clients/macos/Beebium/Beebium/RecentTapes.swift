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

/// One recently used tape image, identified by its path on the host the server
/// shares. The path is the identity: the list dedupes by it.
struct RecentTape: Codable, Identifiable, Hashable {
    let filepath: String
    let lastUsed: Date

    var id: String { filepath }
}

/// The list of recently inserted tape images, newest first, capped, persisted
/// in UserDefaults. Tapes keep their own list, separate from recent discs, so
/// the deck's Recent menu only ever offers tapes and a floppy drive's only ever
/// offers discs. As for discs (section 9.2), only inserts the user makes through
/// the app (Browse, drag and drop, the Recent menu, a blank tape just created)
/// are recorded -- a tape a preset mounts at launch is not, since it is already
/// repeatable from the preset. Modelled on RecentDiscsManager.
@MainActor
final class RecentTapesManager: ObservableObject {
    static let shared = RecentTapesManager()

    @Published private(set) var tapes: [RecentTape] = []

    private let key = "recentTapes"
    private let maxEntries = 10
    private let defaults: UserDefaults

    /// `defaults` is injectable so tests can run against an isolated suite
    /// without touching the host's preferences.
    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        load()
    }

    /// Load the list from UserDefaults, falling back to empty on absence or a
    /// decode failure.
    func load() {
        guard let data = defaults.data(forKey: key) else {
            tapes = []
            return
        }
        do {
            tapes = try JSONDecoder().decode([RecentTape].self, from: data)
        } catch {
            NSLog("[RecentTapes] Failed to decode: \(error)")
            tapes = []
        }
    }

    /// Record a successful insert of the tape at `filepath`: move it to the
    /// front, collapse any earlier entry for the same path, and cap the list.
    func record(filepath: String) {
        tapes.removeAll { $0.filepath == filepath }
        tapes.insert(RecentTape(filepath: filepath, lastUsed: Date()), at: 0)
        if tapes.count > maxEntries {
            tapes = Array(tapes.prefix(maxEntries))
        }
        save()
    }

    /// Forget every recent tape.
    func clear() {
        tapes = []
        defaults.removeObject(forKey: key)
    }

    private func save() {
        do {
            let data = try JSONEncoder().encode(tapes)
            defaults.set(data, forKey: key)
        } catch {
            NSLog("[RecentTapes] Failed to encode: \(error)")
        }
    }

    /// Menu labels for `tapes`, in the same order: each image's file name, with
    /// its immediate parent folder in parentheses only where two entries would
    /// otherwise read the same name. Pure, so the disambiguation rule is
    /// testable.
    static func menuLabels(for tapes: [RecentTape]) -> [String] {
        let names = tapes.map { URL(fileURLWithPath: $0.filepath).lastPathComponent }
        var counts: [String: Int] = [:]
        for name in names {
            counts[name, default: 0] += 1
        }
        return zip(tapes, names).map { tape, name in
            guard (counts[name] ?? 0) > 1 else { return name }
            let parent = URL(fileURLWithPath: tape.filepath)
                .deletingLastPathComponent().lastPathComponent
            return parent.isEmpty ? name : "\(name) (\(parent))"
        }
    }
}
