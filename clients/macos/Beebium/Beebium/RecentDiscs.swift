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

/// One recently used disc image, identified by its path on the host the
/// server shares. The path is the identity: the list dedupes by it.
struct RecentDisc: Codable, Identifiable, Hashable {
    let filepath: String
    let lastUsed: Date

    var id: String { filepath }
}

/// The shared list of recently inserted disc images, newest first, capped,
/// persisted in UserDefaults. Both floppy drives draw on this one list, and
/// only inserts the user makes through the app (Browse, drag and drop, the
/// Recent menu) are recorded -- discs a preset mounts at launch are not, since
/// they are already repeatable from the preset. Modelled on
/// RecentConnectionsManager.
@MainActor
final class RecentDiscsManager: ObservableObject {
    static let shared = RecentDiscsManager()

    @Published private(set) var discs: [RecentDisc] = []

    private let key = "recentDiscs"
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
            discs = []
            return
        }
        do {
            discs = try JSONDecoder().decode([RecentDisc].self, from: data)
        } catch {
            NSLog("[RecentDiscs] Failed to decode: \(error)")
            discs = []
        }
    }

    /// Record a successful insert of the image at `filepath`: move it to the
    /// front, collapse any earlier entry for the same path, and cap the list.
    func record(filepath: String) {
        discs.removeAll { $0.filepath == filepath }
        discs.insert(RecentDisc(filepath: filepath, lastUsed: Date()), at: 0)
        if discs.count > maxEntries {
            discs = Array(discs.prefix(maxEntries))
        }
        save()
    }

    /// Forget every recent disc.
    func clear() {
        discs = []
        defaults.removeObject(forKey: key)
    }

    private func save() {
        do {
            let data = try JSONEncoder().encode(discs)
            defaults.set(data, forKey: key)
        } catch {
            NSLog("[RecentDiscs] Failed to encode: \(error)")
        }
    }

    /// Menu labels for `discs`, in the same order: each image's file name,
    /// with its immediate parent folder in parentheses only where two entries
    /// would otherwise read the same name. Pure, so the disambiguation rule is
    /// testable.
    static func menuLabels(for discs: [RecentDisc]) -> [String] {
        let names = discs.map { URL(fileURLWithPath: $0.filepath).lastPathComponent }
        var counts: [String: Int] = [:]
        for name in names {
            counts[name, default: 0] += 1
        }
        return zip(discs, names).map { disc, name in
            guard (counts[name] ?? 0) > 1 else { return name }
            let parent = URL(fileURLWithPath: disc.filepath)
                .deletingLastPathComponent().lastPathComponent
            return parent.isEmpty ? name : "\(name) (\(parent))"
        }
    }
}
