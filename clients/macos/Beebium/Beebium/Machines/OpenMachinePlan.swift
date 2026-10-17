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

/// What opening a saved machine should do, decided from its describe-machine
/// record before any launch.
///
/// A machine already running -- its directory locked (section 4.4) -- is never
/// launched a second time (section 4.8): if this app owns the locking process,
/// its window is brought forward; otherwise the user is told which process
/// holds it. An unlocked machine is launched. Pure, so the decision is testable
/// without a server.
enum OpenMachinePlan: Equatable {
    /// A window this app owns is showing the machine; bring it forward.
    case bringForward(pid: Int)
    /// The directory is locked by a process this app did not launch.
    case reportInUse(pid: Int)
    /// Not running; launch it.
    case launch

    static func plan(for description: MachineDescription, appLaunchedPids: Set<Int>) -> OpenMachinePlan {
        guard let pid = description.lockedByPid else { return .launch }
        return appLaunchedPids.contains(pid) ? .bringForward(pid: pid) : .reportInUse(pid: pid)
    }
}
