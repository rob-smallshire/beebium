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

/// The saved machines behind the Welcome window's Machines page: the recent
/// machine directories, each described with describe-machine, in recents order.
/// A recent directory that no longer describes -- deleted, moved away or
/// unreadable -- is dropped rather than shown as a broken row.
///
/// `describe` is injectable so the model is exercised without running a server;
/// in the app it is PresetManager's describe-machine.
@MainActor
final class SavedMachinesModel: ObservableObject {
    @Published private(set) var machines: [MachineDescription] = []
    @Published private(set) var isLoading = false

    private let recents: MachineRecents
    private let describe: (URL) async -> MachineDescription?

    init(recents: MachineRecents? = nil,
         describe: ((URL) async -> MachineDescription?)? = nil) {
        self.recents = recents ?? MachineRecents()
        self.describe = describe ?? { await PresetManager.shared.describeMachine(path: $0.path) }
    }

    /// Whether there are any recent machines at all, without describing them --
    /// used to choose the Welcome window's initial page.
    var hasRecentMachines: Bool { !recents.machineDirpaths.isEmpty }

    /// Describe every recent machine, dropping those that no longer describe,
    /// and publish the list in recents order.
    func reload() async {
        isLoading = true
        defer { isLoading = false }
        var resolved: [MachineDescription] = []
        for dirpath in recents.machineDirpaths {
            if let description = await describe(dirpath) {
                resolved.append(description)
            }
        }
        machines = resolved
    }

    /// Forget every recent machine (the Clear button on the page). The machines
    /// on disk are untouched; only the recents list is emptied.
    func clear() {
        recents.clear()
        machines = []
    }
}
