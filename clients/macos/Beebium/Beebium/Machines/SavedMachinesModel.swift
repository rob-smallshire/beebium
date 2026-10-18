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
import Combine

/// The saved machines behind the Welcome window's Machines page: the recent
/// machine directories, each described with describe-machine, in recents order.
/// A recent directory that no longer describes -- deleted, moved away or
/// unreadable -- is dropped rather than shown as a broken row.
///
/// The Running badge on each card is the machine's lock (describe-machine's
/// locked_by_pid), so it is only as fresh as the last reload. The page reloads
/// on several triggers -- a machine window opening or closing (observed here,
/// off the view), the page being shown or the window becoming key, and a modest
/// timer while the page is visible -- all funnelled through one coalesced
/// `reload` so a burst of triggers costs one describe pass, not many. No
/// client-side record of what is running is kept; the lock stays the truth.
///
/// `describe` is injectable so the model is exercised without running a server;
/// in the app it is PresetManager's describe-machine. `machineChanges` is the
/// off-view signal that a machine session started or ended.
@MainActor
final class SavedMachinesModel: ObservableObject {
    @Published private(set) var machines: [MachineDescription] = []
    @Published private(set) var isLoading = false

    private let recents: MachineRecents
    private let describe: (URL) async -> MachineDescription?
    private var cancellables = Set<AnyCancellable>()

    // Coalescing: at most one describe pass runs at a time; triggers that arrive
    // during a pass collapse into a single follow-up pass.
    private var isReloading = false
    private var reloadQueued = false

    init(recents: MachineRecents? = nil,
         describe: ((URL) async -> MachineDescription?)? = nil,
         machineChanges: AnyPublisher<Void, Never>? = nil) {
        self.recents = recents ?? MachineRecents()
        self.describe = describe ?? { await PresetManager.shared.describeMachine(path: $0.path) }

        // A machine window opening or closing changes MachineManager's set, which
        // flips a saved machine's lock; reload so the badge follows. This is a
        // model-level observation, not a SwiftUI callback, so it fires even while
        // the Machines page is not the one on screen.
        let changes = machineChanges
            ?? MachineManager.shared.$machines.dropFirst().map { _ in () }.eraseToAnyPublisher()
        changes
            .sink { [weak self] _ in Task { @MainActor in await self?.reload() } }
            .store(in: &cancellables)
    }

    /// Whether there are any recent machines at all, without describing them --
    /// used to choose the Welcome window's initial page.
    var hasRecentMachines: Bool { !recents.machineDirpaths.isEmpty }

    /// Describe every recent machine, dropping those that no longer describe,
    /// and publish the list in recents order. Coalesced: a call made while a pass
    /// is already running schedules one more pass and returns, so overlapping
    /// triggers do not start parallel describe storms.
    func reload() async {
        if isReloading { reloadQueued = true; return }
        isReloading = true
        defer { isReloading = false }
        repeat {
            reloadQueued = false
            isLoading = true
            var resolved: [MachineDescription] = []
            for dirpath in recents.machineDirpaths {
                if let description = await describe(dirpath) {
                    resolved.append(description)
                }
            }
            machines = resolved
            isLoading = false
        } while reloadQueued
    }

    /// Drop one machine from the page (a card's Remove from List): forget it from
    /// recents and take it out of the shown list at once. The machine on disk is
    /// untouched -- opening it again restores it -- and a running machine can be
    /// dropped too, since this is only a list entry. No full re-describe: the
    /// other cards are unchanged.
    func forget(_ machine: MachineDescription) {
        recents.forget(machineDirpath: URL(fileURLWithPath: machine.machineDirpath))
        machines.removeAll { $0.machineDirpath == machine.machineDirpath }
    }

    /// Forget every recent machine (the Clear button on the page). The machines
    /// on disk are untouched; only the recents list is emptied.
    func clear() {
        recents.clear()
        machines = []
    }
}
