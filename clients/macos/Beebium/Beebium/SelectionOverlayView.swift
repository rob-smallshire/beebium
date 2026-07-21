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

    /// The selection range, in this view's coordinates: the reading-order flow
    /// (rows), the snapped block (rectangle), or empty (anywhere). Drawn as the
    /// lighter fill beneath the runs, so "what is selected" reads distinctly
    /// from "what will copy".
    var rangeRects: [CGRect] = [] {
        didSet { needsDisplay = true }
    }

    /// The recognised runs, in this view's coordinates. These are what a copy
    /// would take, drawn as the stronger fill on top of the range.
    var highlightRects: [CGRect] = [] {
        didSet { needsDisplay = true }
    }

    /// The raw dragged rectangle, in this view's coordinates. Outlined faintly so
    /// the user still sees the literal gesture beneath the snapped range.
    var marqueeRect: CGRect? {
        didSet { needsDisplay = true }
    }

    override var isFlipped: Bool { true }

    /// The overlay is a passive decoration; clicks and drags must reach the
    /// Metal view beneath it.
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }

        // One tint that reads on both the bright and dark screens a BBC
        // produces, in two intensities: the range (what is selected) beneath the
        // runs (what will copy).
        let tint = NSColor.selectedContentBackgroundColor

        context.setFillColor(tint.withAlphaComponent(0.25).cgColor)
        for rect in rangeRects {
            context.fill(rect.integral)
        }

        context.setFillColor(tint.withAlphaComponent(0.50).cgColor)
        for rect in highlightRects {
            context.fill(rect.integral)
        }

        if let marquee = marqueeRect {
            context.setStrokeColor(tint.withAlphaComponent(0.5).cgColor)
            context.setLineWidth(1.0)
            // Inset by half a line so the 1px stroke sits inside the rectangle.
            context.stroke(marquee.insetBy(dx: 0.5, dy: 0.5))
        }
    }
}
