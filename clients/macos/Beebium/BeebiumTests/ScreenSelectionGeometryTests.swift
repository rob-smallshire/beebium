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

import XCTest
@testable import Beebium

/// Tests for the view-to-frame-pixel mapping, the inverse of the render
/// pipeline's geometry. This is the crux of the selection stream: everything
/// else depends on a mouse point being turned into the frame pixel the server
/// reasons about, accounting for aspect fit, pixel shape, borders and the edge
/// margin the active style produced.
final class ScreenSelectionGeometryTests: XCTestCase {

    /// A geometry whose content aspect matches the drawable, so the picture
    /// fills the whole view with no letterbox -- the arithmetic is then a clean
    /// square map and the expected pixels are obvious. MODE 0: 640x256 texture,
    /// displayed 640x256, progressive (line-doubled to 512 for aspect).
    /// parScale 1.0 and drawable 1250x1000 give contentAspect == drawableAspect.
    private func fillingGeometry(edgeMargin: Double = 0,
                                 regions: [DisplayRegion] = [
                                    DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 640)
                                 ]) -> ScreenRenderGeometry {
        ScreenRenderGeometry(
            drawableSize: CGSize(width: 1250, height: 1000),
            textureSize: CGSize(width: 640, height: 256),
            displaySize: CGSize(width: 640, height: 256),
            totalSize: CGSize(width: 640, height: 256),
            borderOffset: .zero,
            parScale: 1.0,
            interlaced: false,
            edgeMargin: edgeMargin,
            regions: regions
        )
    }

    private func assertClose(_ a: CGPoint, _ b: CGPoint,
                             accuracy: CGFloat = 0.5, file: StaticString = #filePath,
                             line: UInt = #line) {
        XCTAssertEqual(a.x, b.x, accuracy: accuracy, file: file, line: line)
        XCTAssertEqual(a.y, b.y, accuracy: accuracy, file: file, line: line)
    }

    // MARK: - Known points (no letterbox, no margin)

    func testCentreMapsToCentreFramePixel() {
        let g = fillingGeometry()
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.5, y: 0.5), geometry: g)
        XCTAssertNotNil(hit)
        assertClose(hit!.pixel, CGPoint(x: 320, y: 128))
        XCTAssertTrue(hit!.insideActiveArea)
    }

    func testTopLeftMapsToOrigin() {
        let g = fillingGeometry()
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0, y: 0), geometry: g)
        assertClose(hit!.pixel, CGPoint(x: 0, y: 0))
    }

    func testBottomRightMapsToFarCorner() {
        let g = fillingGeometry()
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 1, y: 1), geometry: g)
        assertClose(hit!.pixel, CGPoint(x: 640, y: 256))
    }

    // MARK: - Round trip

    func testForwardThenInverseIsIdentity() {
        let g = fillingGeometry()
        for pixel in [CGPoint(x: 100, y: 50), CGPoint(x: 0, y: 0),
                      CGPoint(x: 639, y: 255), CGPoint(x: 313, y: 201)] {
            let normalized = ScreenCoordinateMapper.normalizedPoint(
                framePixel: pixel, geometry: g)
            let back = ScreenCoordinateMapper.framePixel(
                normalizedPoint: normalized, geometry: g)
            XCTAssertNotNil(back)
            assertClose(back!.pixel, pixel)
        }
    }

    func testRoundTripUnderEdgeMargin() {
        let g = fillingGeometry(edgeMargin: 0.02)
        for pixel in [CGPoint(x: 100, y: 50), CGPoint(x: 320, y: 128)] {
            let normalized = ScreenCoordinateMapper.normalizedPoint(
                framePixel: pixel, geometry: g)
            let back = ScreenCoordinateMapper.framePixel(
                normalizedPoint: normalized, geometry: g)
            assertClose(back!.pixel, pixel)
        }
    }

    // MARK: - Edge margin (Standard style)

    func testEdgeMarginTopLeftIsTheActiveOrigin() {
        // With a 2% inset, the active area's top-left sits at texcoord (0.02,
        // 0.02) of the picture, which is the same normalised point here.
        let g = fillingGeometry(edgeMargin: 0.02)
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.02, y: 0.02), geometry: g)
        assertClose(hit!.pixel, CGPoint(x: 0, y: 0))
    }

    func testEdgeMarginCentreStillMapsToCentre() {
        let g = fillingGeometry(edgeMargin: 0.02)
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.5, y: 0.5), geometry: g)
        assertClose(hit!.pixel, CGPoint(x: 320, y: 128))
    }

    // MARK: - Letterbox / pillarbox

    func testPointOutsideThePictureReturnsNil() {
        // A drawable far wider than the content pillarboxes the picture; a point
        // in the side bar is not over any frame pixel.
        var g = fillingGeometry()
        g.drawableSize = CGSize(width: 2000, height: 1000)  // aspect 2.0 > 1.25
        XCTAssertNil(ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.02, y: 0.5), geometry: g))
        // The centre is still over the picture.
        let centre = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.5, y: 0.5), geometry: g)
        assertClose(centre!.pixel, CGPoint(x: 320, y: 128))
    }

    func testBorderPointClampsToEdgeAndReportsOutside() {
        // Debug-style geometry: a coloured border surrounds the active area.
        // A point in the border is still within the picture, so it clamps to the
        // active edge rather than returning nil, and is reported as outside.
        let g = ScreenRenderGeometry(
            drawableSize: CGSize(width: 704 * 1.0, height: 288 * 2.0),
            textureSize: CGSize(width: 640, height: 256),
            displaySize: CGSize(width: 640, height: 256),
            totalSize: CGSize(width: 704, height: 288),
            borderOffset: CGPoint(x: 32, y: 16),
            parScale: 1.0,
            interlaced: false,
            edgeMargin: 0,
            regions: [DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 640)]
        )
        // Top-left of the whole picture is in the top-left border.
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0, y: 0), geometry: g)
        XCTAssertNotNil(hit)
        XCTAssertFalse(hit!.insideActiveArea)
        assertClose(hit!.pixel, CGPoint(x: 0, y: 0))
    }

    // MARK: - Split screen (per-band horizontal scale)

    func testSplitScreenLowerBandUsesItsOwnPixelWidth() {
        // Upper band 640 wide over scanlines 0..128, lower band 320 wide over
        // 128..256. A point at the horizontal centre of the lower band must map
        // to x = 160 (half of 320), not 320.
        let g = fillingGeometry(regions: [
            DisplayRegion(startLine: 0, endLine: 128, pixelWidth: 640),
            DisplayRegion(startLine: 128, endLine: 256, pixelWidth: 320),
        ])
        let lower = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.5, y: 0.75), geometry: g)
        assertClose(lower!.pixel, CGPoint(x: 160, y: 192))

        let upper = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.5, y: 0.25), geometry: g)
        assertClose(upper!.pixel, CGPoint(x: 320, y: 64))
    }

    // MARK: - Horizontal scaling (MODE 1: 320 texture stretched to 640)

    func testHalfWidthTextureStretchedToDisplay() {
        // MODE 1: 320 logical pixels shown at 640. A point at the horizontal
        // centre maps to frame x 160 (the middle of the 320-wide texture).
        var g = fillingGeometry(regions: [
            DisplayRegion(startLine: 0, endLine: 256, pixelWidth: 320)
        ])
        g.textureSize = CGSize(width: 320, height: 256)
        let hit = ScreenCoordinateMapper.framePixel(
            normalizedPoint: CGPoint(x: 0.5, y: 0.5), geometry: g)
        assertClose(hit!.pixel, CGPoint(x: 160, y: 128))
    }

    // MARK: - View-rect mapping for highlights

    func testViewRectSpansTheFrameRectInViewSpace() {
        let g = fillingGeometry()
        let rect = FramePixelRect(x: 0, y: 0, width: 320, height: 128)
        let viewRect = ScreenCoordinateMapper.viewRect(
            frameRect: rect, geometry: g, viewSize: CGSize(width: 1250, height: 1000))
        // The top-left quarter of the frame occupies the top-left quarter of the
        // (letterbox-free) view.
        XCTAssertEqual(viewRect.minX, 0, accuracy: 1)
        XCTAssertEqual(viewRect.minY, 0, accuracy: 1)
        XCTAssertEqual(viewRect.width, 625, accuracy: 1)
        XCTAssertEqual(viewRect.height, 500, accuracy: 1)
    }
}
