// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

/// The pull-down menu a FileReference offers: the server's own actions first,
/// then the renderer's client-side ones -- Reveal in Finder, only when the
/// server shares this host's filesystem, and Copy Path always (#144). Pure so
/// the composition is testable without a view; the renderer never invents a
/// server action the view did not list.
enum FileReferenceMenu {

    /// One server-supplied action (dispatched by its id as file_action_id).
    struct ServerAction: Equatable {
        let id: String
        let title: String
    }

    /// A menu entry: a server action, or one of the two client-side commands.
    enum Item: Equatable {
        case serverAction(id: String, title: String)
        case reveal
        case copyPath
    }

    /// Server actions in their given order, then Reveal in Finder when the
    /// server is on this host, then Copy Path.
    static func items(serverActions: [ServerAction], isServerLocal: Bool) -> [Item] {
        var items = serverActions.map { Item.serverAction(id: $0.id, title: $0.title) }
        if isServerLocal {
            items.append(.reveal)
        }
        items.append(.copyPath)
        return items
    }
}
