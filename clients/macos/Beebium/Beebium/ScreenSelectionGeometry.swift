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

import CoreGraphics
import Foundation

// MARK: - Frame pixel coordinates

/// A rectangle in frame pixel coordinates: the origin is the top-left of the
/// active pixel area, not of the bordered display. This is the same coordinate
/// system the server's `PixelRegion` uses, so a rect built here can be sent to
/// `GetScreenText` unchanged and a run's `bounds` can be drawn back without
/// translation.
struct FramePixelRect: Equatable {
    var x: Int
    var y: Int
    var width: Int
    var height: Int

    var maxX: Int { x + width }
    var maxY: Int { y + height }
    var isEmpty: Bool { width <= 0 || height <= 0 }
}

// MARK: - Cell grid geometry (from GetScreenGeometry)

/// The character grid one band of scanlines implies, in frame pixels. Mirrors
/// the server's `ScreenBandGeometry`. A whole-screen bitmap or MODE 7 display
/// has a single band; a split screen (Elite, Revs) has one per geometry.
struct ScreenBandGeometry: Equatable {
    var top: Int       // First scanline, inclusive
    var bottom: Int    // One past the last

    var cellWidth: Int
    var cellHeight: Int

    /// Cell-to-cell step, equal to the cell size except where a mode leaves
    /// blank scanlines between rows (MODE 3, MODE 6: an eight-scanline glyph on
    /// a ten-scanline pitch).
    var columnPitch: Int
    var rowPitch: Int

    var originX: Int   // Where the grid starts within the band
    var originY: Int

    /// True when this band advertises a usable character grid. A band with a
    /// zero pitch cannot be snapped to and is treated as free pixels only.
    var isGridded: Bool { columnPitch > 0 && rowPitch > 0 }

    /// The column whose cell contains frame pixel `x`, clamped to be
    /// non-negative. Uses the cell the pointer is inside (floor), which is what
    /// a text selection wants: the character under the cursor is included.
    func column(atFrameX x: Int) -> Int {
        guard columnPitch > 0 else { return 0 }
        return max(0, (x - originX) / columnPitch)
    }

    /// The row whose cell contains frame pixel `y`, clamped to be non-negative.
    func row(atFrameY y: Int) -> Int {
        guard rowPitch > 0 else { return 0 }
        return max(0, (y - originY) / rowPitch)
    }

    /// The left frame-pixel edge of column `c`.
    func frameX(ofColumn c: Int) -> Int { originX + c * columnPitch }

    /// The top frame-pixel edge of row `r`.
    func frameY(ofRow r: Int) -> Int { originY + r * rowPitch }
}

// MARK: - Rendering geometry (how the active style laid the frame out)

/// Everything the view-to-frame mapping needs about how the active Display
/// Style placed the frame in the drawable. It is a plain, Metal-free value so
/// the mapping can be unit-tested without a device, but its fields are exactly
/// those the shader reads from `Uniforms`, so the mapping stays the true inverse
/// of `vertexShader` + the active fragment shader. Build it from `Uniforms`
/// (see `init(uniforms:)`) so it tracks whatever the style produced -- Standard
/// insets by an edge margin with no borders; Debug surrounds the picture with
/// coloured borders and no margin.
///
/// `ScreenSelectionGeometryTests` pins the round trip; if the shader math
/// changes, that math and this inverse must change together.
struct ScreenRenderGeometry: Equatable {
    /// Drawable size, used only for its aspect ratio.
    var drawableSize: CGSize
    /// Logical framebuffer size -- the frame pixel coordinate space.
    var textureSize: CGSize
    /// Scaled display size (e.g. 640x256; MODE 1's 320 stretched to 640).
    var displaySize: CGSize
    /// Total size including CRTC borders. Equal to `displaySize` for Standard.
    var totalSize: CGSize
    /// Offset of the active area within the total (left, top border widths).
    /// Zero for Standard.
    var borderOffset: CGPoint
    /// Pixel aspect ratio: BBC pixels are `parScale` as wide as they are tall.
    var parScale: Double
    /// True for interlaced MODE 7; false for line-doubled bitmap modes.
    var interlaced: Bool
    /// Per-edge inset the Standard style draws as an inner frame, as a fraction
    /// of the picture rectangle. Zero for styles that do not inset (Debug).
    var edgeMargin: Double
    /// Per-band logical pixel widths for split-screen modes.
    var regions: [DisplayRegion]

    /// The logical pixel width in effect at frame scanline `scanlineY`. Equal to
    /// the whole texture width when there is a single region; per-band otherwise.
    func regionPixelWidth(atScanline scanlineY: Double) -> Double {
        guard regions.count > 1 else { return Double(textureSize.width) }
        let maxScan = max(0, Int(textureSize.height) - 1)
        let scan = min(max(Int(scanlineY), 0), maxScan)
        for region in regions where scan >= region.startLine && scan < region.endLine {
            return Double(region.pixelWidth)
        }
        return Double(textureSize.width)
    }
}

// MARK: - The mapping

/// The inverse of the render pipeline's geometry: a mouse point in the view back
/// to a frame pixel, and a frame rectangle forward to the view for drawing
/// highlights. Points are in a normalised, top-left-origin space (0,0 at the
/// top-left of the view, 1,1 at the bottom-right) so the mapping is independent
/// of backing scale and of whichever coordinate flip the caller's view uses.
enum ScreenCoordinateMapper {

    /// The picture rectangle the aspect-fit places the frame into, expressed as
    /// a fraction of the view: origin and size in normalised [0,1] coordinates.
    /// This is the pillarboxed/letterboxed region; outside it is background.
    static func pictureRect(_ g: ScreenRenderGeometry) -> CGRect {
        let contentWidth = Double(g.totalSize.width) * g.parScale
        // Bitmap modes render 256 lines that a CRT doubles to ~512; MODE 7's
        // ~500 interlaced lines are shown as-is. The vertex shader doubles the
        // progressive height for the aspect calculation, and so must this.
        let contentHeight = Double(g.totalSize.height) * (g.interlaced ? 1.0 : 2.0)
        guard contentHeight > 0, g.drawableSize.height > 0 else {
            return CGRect(x: 0, y: 0, width: 1, height: 1)
        }
        let contentAspect = contentWidth / contentHeight
        let drawableAspect = Double(g.drawableSize.width) / Double(g.drawableSize.height)

        var scaleX = 1.0
        var scaleY = 1.0
        if drawableAspect > contentAspect {
            scaleX = contentAspect / drawableAspect  // pillarbox
        } else {
            scaleY = drawableAspect / contentAspect  // letterbox
        }
        return CGRect(x: (1.0 - scaleX) / 2.0, y: (1.0 - scaleY) / 2.0,
                      width: scaleX, height: scaleY)
    }

    /// A normalised view point mapped to a frame pixel.
    ///
    /// Returns nil when the point lies outside the picture rectangle -- in the
    /// letterbox or pillarbox, where there is no frame to hit. Within the
    /// picture but inside a CRTC border or the edge margin, the pixel is clamped
    /// to the nearest active-area edge and `insideActiveArea` is false: a drag
    /// that runs past the picture still selects as far as the edge, which is
    /// what the user meant.
    static func framePixel(normalizedPoint n: CGPoint,
                           geometry g: ScreenRenderGeometry)
        -> (pixel: CGPoint, insideActiveArea: Bool)? {
        let picture = pictureRect(g)
        guard picture.width > 0, picture.height > 0 else { return nil }

        let texX = (Double(n.x) - picture.minX) / picture.width
        let texY = (Double(n.y) - picture.minY) / picture.height
        if texX < 0 || texX > 1 || texY < 0 || texY > 1 { return nil }

        // Picture-rectangle texcoord -> active-area content coordinate, in the
        // scaled display space. Standard insets by an edge margin; Debug offsets
        // by the border widths. Exactly one is in play (the other's field is 0).
        var contentX: Double
        var contentY: Double
        if g.edgeMargin > 0 {
            let margin = min(max(g.edgeMargin, 0), 0.45)
            let span = 1.0 - 2.0 * margin
            contentX = (texX - margin) / span * Double(g.displaySize.width)
            contentY = (texY - margin) / span * Double(g.displaySize.height)
        } else {
            contentX = texX * Double(g.totalSize.width) - Double(g.borderOffset.x)
            contentY = texY * Double(g.totalSize.height) - Double(g.borderOffset.y)
        }

        let inside = contentX >= 0 && contentX <= Double(g.displaySize.width)
                  && contentY >= 0 && contentY <= Double(g.displaySize.height)
        contentX = min(max(contentX, 0), Double(g.displaySize.width))
        contentY = min(max(contentY, 0), Double(g.displaySize.height))

        let pixel = framePixel(contentX: contentX, contentY: contentY, geometry: g)
        return (pixel, inside)
    }

    /// A frame pixel mapped forward to a normalised view point, for drawing a
    /// highlight over exactly the pixels a run occupies.
    static func normalizedPoint(framePixel p: CGPoint,
                                geometry g: ScreenRenderGeometry) -> CGPoint {
        guard g.textureSize.width > 0, g.textureSize.height > 0 else { return .zero }
        let texV = Double(p.y) / Double(g.textureSize.height)
        let contentY = texV * Double(g.displaySize.height)
        let pixelWidth = g.regionPixelWidth(atScanline: Double(p.y))
        let contentX = (pixelWidth > 0 ? Double(p.x) / pixelWidth : 0)
            * Double(g.displaySize.width)

        var texX: Double
        var texY: Double
        if g.edgeMargin > 0 {
            let margin = min(max(g.edgeMargin, 0), 0.45)
            let span = 1.0 - 2.0 * margin
            texX = margin + contentX / Double(g.displaySize.width) * span
            texY = margin + contentY / Double(g.displaySize.height) * span
        } else {
            texX = (contentX + Double(g.borderOffset.x)) / Double(g.totalSize.width)
            texY = (contentY + Double(g.borderOffset.y)) / Double(g.totalSize.height)
        }

        let picture = pictureRect(g)
        return CGPoint(x: picture.minX + texX * picture.width,
                       y: picture.minY + texY * picture.height)
    }

    /// A frame pixel rectangle mapped to a view CGRect (in a top-left-origin
    /// coordinate space of `viewSize`), for drawing a highlight overlay.
    static func viewRect(frameRect r: FramePixelRect,
                         geometry g: ScreenRenderGeometry,
                         viewSize: CGSize) -> CGRect {
        let topLeft = normalizedPoint(framePixel: CGPoint(x: r.x, y: r.y), geometry: g)
        let bottomRight = normalizedPoint(
            framePixel: CGPoint(x: r.maxX, y: r.maxY), geometry: g)
        let x0 = topLeft.x * viewSize.width
        let y0 = topLeft.y * viewSize.height
        let x1 = bottomRight.x * viewSize.width
        let y1 = bottomRight.y * viewSize.height
        return CGRect(x: min(x0, x1), y: min(y0, y1),
                      width: abs(x1 - x0), height: abs(y1 - y0))
    }

    /// Content (scaled display) coordinate to frame pixel. The horizontal scale
    /// is per-band, so a point in Elite's 160-pixel lower band lands in [0,160)
    /// while one in its 320-pixel upper band lands in [0,320) -- exactly the
    /// frame-pixel x GetScreenText expects for that band.
    private static func framePixel(contentX: Double, contentY: Double,
                                   geometry g: ScreenRenderGeometry) -> CGPoint {
        let texV = contentY / Double(g.displaySize.height)
        let frameY = texV * Double(g.textureSize.height)
        let pixelWidth = g.regionPixelWidth(atScanline: frameY)
        let frameX = (g.displaySize.width > 0
                      ? contentX / Double(g.displaySize.width) : 0) * pixelWidth
        return CGPoint(x: frameX, y: frameY)
    }
}
