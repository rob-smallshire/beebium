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

/// Which page the Welcome window shows: the saved machines a user can reopen,
/// or the presets and the custom builder.
enum WelcomePage: Hashable, CaseIterable, Identifiable {
    case machines
    case presets

    var id: Self { self }

    /// The page to open on at launch: a returning user with saved machines sees
    /// them first; a new user sees the templates (section 4.8). Pure.
    static func initial(hasRecentMachines: Bool) -> WelcomePage {
        hasRecentMachines ? .machines : .presets
    }

    /// The page a File-menu command opens the Welcome window on (section 4.8):
    /// New... is always the presets (and the custom builder); Open Recent... is
    /// the saved machines. This is fixed, unlike `initial`, which chooses the
    /// launch page from whether there are recents. Pure.
    static func page(for command: WelcomeCommand) -> WelcomePage {
        switch command {
        case .new:        return .presets
        case .openRecent: return .machines
        }
    }
}

/// A File-menu command that opens the Welcome window, to map to the page it
/// should show.
enum WelcomeCommand {
    case new
    case openRecent
}
