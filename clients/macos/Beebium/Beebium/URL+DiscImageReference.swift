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

extension URL {
    /// Resolve a Finder alias file to the real file it points at, so a dropped
    /// or picked alias reaches the server as its target rather than the small
    /// alias file itself (which the server would read as an unrecognised image).
    ///
    /// A Finder alias is a macOS-only construct, so it is resolved here in the
    /// front-end at the seam where a user-supplied URL enters, not in the
    /// portable server. Anything that is not an alias is returned unchanged and
    /// this never throws: a plain file, a symlink (POSIX open follows symlinks
    /// anyway), or a path that does not exist all pass straight through.
    func resolvingDiscImageReference() -> URL {
        guard isFileURL else { return self }

        // The path doesn't exist / can't be inspected -> as-is, no throw.
        guard let values = try? resourceValues(forKeys: [.isAliasFileKey,
                                                         .isSymbolicLinkKey]) else {
            return self
        }
        // isAliasFileKey is true for BOTH Finder aliases and POSIX symlinks;
        // only Finder aliases need resolving here. A symlink is followed by
        // POSIX open, so pass it (and any plain file) through unchanged.
        if values.isSymbolicLink == true { return self }
        guard values.isAliasFile == true else { return self }

        // Resolve without blocking on UI or mounting a volume just to look.
        guard let target = try? URL(resolvingAliasFileAt: self,
                                    options: [.withoutUI, .withoutMounting]) else {
            return self
        }
        return target
    }
}
