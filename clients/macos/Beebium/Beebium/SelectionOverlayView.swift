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

import AppKit

/// Draws the selection over the frozen frame: a marquee outline for the dragged
/// rectangle and translucent fills over the runs GetScreenText recognised. It is
/// a dumb view -- it is handed rectangles already in its own coordinate space
/// and does not know about frame pixels, geometry, or the server. The owning
/// view (KeyboardMTKView) does the mapping and hands the results down.
///
/// Flipped so its coordinate space is top-left origin, matching the normalised
/// top-left space the coordinate mapper works in; that keeps the mapping and the
/// drawing agreeing on which way is up.
final class SelectionOverlayView: NSView {

    /// The recognised runs, in this view's coordinates. These are what a copy
    /// would take, so highlighting them is the version-one feedback.
    var highlightRects: [CGRect] = [] {
        didSet { needsDisplay = true }
    }

    /// The raw dragged rectangle, in this view's coordinates. Outlined so the
    /// user sees the gesture even where no text was found.
    var marqueeRect: CGRect? {
        didSet { needsDisplay = true }
    }

    override var isFlipped: Bool { true }

    /// The overlay is a passive decoration; clicks and drags must reach the
    /// Metal view beneath it.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }

        // A tint that reads on both the bright and dark screens a BBC produces.
        let fill = NSColor.selectedContentBackgroundColor.withAlphaComponent(0.35)
        context.setFillColor(fill.cgColor)
        for rect in highlightRects {
            context.fill(rect.integral)
        }

        if let marquee = marqueeRect {
            context.setStrokeColor(
                NSColor.selectedContentBackgroundColor.withAlphaComponent(0.9).cgColor)
            context.setLineWidth(1.0)
            // Inset by half a line so the 1px stroke sits inside the rectangle.
            context.stroke(marquee.insetBy(dx: 0.5, dy: 0.5))
        }
    }
}
