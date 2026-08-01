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

import XCTest
import AppKit
@testable import Beebium

/// The SVG path grammar, as implemented by SvgPathParser for the Touch Bar's
/// function-key glyphs.
///
/// The glyphs are drawing-tool output, and the parser only runs on a Mac with
/// a Touch Bar, so a spelling it cannot read means a glyph silently missing on
/// hardware the developer may not have. These tests are the standard: each one
/// is a form the specification says is legal.
final class SvgPathParserTests: XCTestCase {

    private func parse(_ pathData: String) -> NSBezierPath? {
        SvgPathParser.path(from: pathData)
    }

    /// The path's elements, as (type, points-that-matter) pairs.
    private func elements(of path: NSBezierPath) -> [(NSBezierPath.ElementType, [NSPoint])] {
        var result: [(NSBezierPath.ElementType, [NSPoint])] = []
        var points = [NSPoint](repeating: .zero, count: 3)
        for index in 0..<path.elementCount {
            let type = path.element(at: index, associatedPoints: &points)
            switch type {
            case .moveTo, .lineTo: result.append((type, [points[0]]))
            case .curveTo: result.append((type, [points[0], points[1], points[2]]))
            case .closePath: result.append((type, []))
            @unknown default: result.append((type, []))
            }
        }
        return result
    }

    private func types(of path: NSBezierPath) -> [NSBezierPath.ElementType] {
        elements(of: path).map { $0.0 }
    }

    /// The points of every element, flattened -- enough to compare two
    /// spellings of the same shape without depending on element bookkeeping.
    private func points(of path: NSBezierPath) -> [NSPoint] {
        elements(of: path).flatMap { $0.1 }
    }

    /// Assert two path-data strings describe the same path.
    private func assertSameShape(_ lhs: String, _ rhs: String,
                                 accuracy: CGFloat = 1e-9,
                                 file: StaticString = #filePath, line: UInt = #line) {
        guard let left = parse(lhs), let right = parse(rhs) else {
            return XCTFail("one of the paths did not parse", file: file, line: line)
        }
        XCTAssertEqual(types(of: left), types(of: right), file: file, line: line)
        let leftPoints = points(of: left), rightPoints = points(of: right)
        guard leftPoints.count == rightPoints.count else {
            return XCTFail("different point counts", file: file, line: line)
        }
        for (a, b) in zip(leftPoints, rightPoints) {
            XCTAssertEqual(a.x, b.x, accuracy: accuracy, file: file, line: line)
            XCTAssertEqual(a.y, b.y, accuracy: accuracy, file: file, line: line)
        }
    }

    // MARK: - Separators

    func testCommaSeparatedCoordinates() {
        assertSameShape("M 10,20 L 30,40", "M 10 20 L 30 40")
    }

    func testCommaWithSurroundingWhitespace() {
        assertSameShape("M 10 , 20 L 30\t,\n40", "M 10 20 L 30 40")
    }

    func testNoSeparatorAtAll() {
        // A sign or a decimal point begins a number on its own, so a separator
        // is never required between numbers that cannot run together.
        assertSameShape("M10-20L-30.5.5", "M 10 -20 L -30.5 0.5")
    }

    func testCommandLetterNeedsNoSeparator() {
        assertSameShape("M10 20L30 40Z", "M 10 20 L 30 40 Z")
    }

    func testLeadingAndTrailingWhitespace() {
        assertSameShape("  \n M 1 2 L 3 4 \t ", "M 1 2 L 3 4")
    }

    // MARK: - Numbers

    func testNegativeAndFractionalCoordinates() {
        guard let path = parse("M -1.5 2.25 L 3 -4.75") else { return XCTFail("no path") }
        XCTAssertEqual(points(of: path), [NSPoint(x: -1.5, y: 2.25), NSPoint(x: 3, y: -4.75)])
    }

    func testExplicitPlusSign() {
        assertSameShape("M +1 +2 L +3 +4", "M 1 2 L 3 4")
    }

    func testLeadingDecimalPoint() {
        guard let path = parse("M .5 .25") else { return XCTFail("no path") }
        XCTAssertEqual(points(of: path), [NSPoint(x: 0.5, y: 0.25)])
    }

    func testTrailingDecimalPoint() {
        assertSameShape("M 1. 2.", "M 1 2")
    }

    func testExponentNotation() {
        guard let path = parse("M 1e2 1.5E-1 L -2e+1 3e0") else { return XCTFail("no path") }
        XCTAssertEqual(points(of: path), [NSPoint(x: 100, y: 0.15), NSPoint(x: -20, y: 3)])
    }

    func testBareEIsNotAnExponent() {
        // "4e" with no exponent digits is the number 4 followed by a stray
        // letter. The e must not be swallowed into the number, or the line
        // would land somewhere between 4 and infinity; it is left to be read
        // as a command, where being unrecognised ends the path.
        guard let path = parse("M 1 2 L 3 4e 9 9") else { return XCTFail("no path") }
        XCTAssertEqual(points(of: path), [NSPoint(x: 1, y: 2), NSPoint(x: 3, y: 4)],
                       "the completed line stands; the stray e ends the path there")
    }

    // MARK: - Commands

    func testMoveAndLine() {
        guard let path = parse("M 10 20 L 30 40") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo, .lineTo])
        XCTAssertEqual(points(of: path), [NSPoint(x: 10, y: 20), NSPoint(x: 30, y: 40)])
    }

    func testCubicCurveTakesThreePoints() {
        guard let path = parse("M 0 0 C 1 2 3 4 5 6") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo, .curveTo])
        // AppKit stores curveTo as (control1, control2, endpoint).
        XCTAssertEqual(Array(points(of: path).dropFirst()),
                       [NSPoint(x: 1, y: 2), NSPoint(x: 3, y: 4), NSPoint(x: 5, y: 6)])
    }

    func testClosePath() {
        guard let path = parse("M 0 0 L 10 0 Z") else { return XCTFail("no path") }
        // NSBezierPath.close() appends an implicit moveTo back to the start of
        // the subpath as well as the closePath, so this asserts the commands
        // were recognised rather than a literal element count.
        XCTAssertEqual(types(of: path).prefix(3), [.moveTo, .lineTo, .closePath])
    }

    func testHorizontalAndVerticalLines() {
        assertSameShape("M 1 2 H 10 V 20", "M 1 2 L 10 2 L 10 20")
    }

    func testQuadraticBecomesTheEquivalentCubic() {
        // Control points two thirds of the way from each endpoint towards the
        // quadratic's control point describe exactly the same curve.
        assertSameShape("M 0 0 Q 3 9 6 0",
                        "M 0 0 C 2 6 4 6 6 0")
    }

    func testSmoothCubicReflectsThePreviousControlPoint() {
        assertSameShape("M 0 0 C 1 2 3 4 5 6 S 7 8 9 10",
                        "M 0 0 C 1 2 3 4 5 6 C 7 8 7 8 9 10")
    }

    func testSmoothCubicWithoutAPrecedingCubicUsesTheCurrentPoint() {
        assertSameShape("M 5 5 S 7 8 9 10",
                        "M 5 5 C 5 5 7 8 9 10")
    }

    func testSmoothQuadraticReflectsThePreviousControlPoint() {
        assertSameShape("M 0 0 Q 2 4 4 0 T 8 0",
                        "M 0 0 Q 2 4 4 0 Q 6 -4 8 0")
    }

    func testSmoothQuadraticWithoutAPrecedingQuadraticUsesTheCurrentPoint() {
        assertSameShape("M 3 3 T 9 3", "M 3 3 Q 3 3 9 3")
    }

    // MARK: - Relative commands

    func testLowercaseIsRelativeToTheCurrentPoint() {
        assertSameShape("m 10 10 l 5 0 l 0 5 z",
                        "M 10 10 L 15 10 L 15 15 Z")
    }

    func testRelativeMoveAfterCloseIsRelativeToTheSubpathStart() {
        // Z returns the current point to where the subpath began, so the
        // following relative moveto starts from there, not from the last
        // coordinate drawn.
        assertSameShape("M 10 10 L 20 20 Z m 5 5",
                        "M 10 10 L 20 20 Z M 15 15")
    }

    func testRelativeCubicAndSmooth() {
        assertSameShape("M 10 10 c 1 1 2 2 3 3 s 1 1 2 2",
                        "M 10 10 C 11 11 12 12 13 13 S 14 14 15 15")
    }

    func testRelativeHorizontalAndVertical() {
        assertSameShape("M 10 10 h 5 v -5 h -5", "M 10 10 L 15 10 L 15 5 L 10 5")
    }

    // MARK: - Implicit repetition

    func testRepeatedLineCoordinates() {
        assertSameShape("M 0 0 L 1 1 2 2 3 3", "M 0 0 L 1 1 L 2 2 L 3 3")
    }

    func testRepeatedMoveCoordinatesDrawLines() {
        // The specification is explicit: extra coordinate pairs after a moveto
        // are linetos, not more movetos.
        assertSameShape("M 0 0 1 1 2 2", "M 0 0 L 1 1 L 2 2")
    }

    func testRepeatedRelativeMoveCoordinatesDrawRelativeLines() {
        assertSameShape("m 0 0 1 1 2 2", "M 0 0 l 1 1 l 2 2")
    }

    func testRepeatedCubicCoordinateSets() {
        assertSameShape("M 0 0 C 1 1 2 2 3 3 4 4 5 5 6 6",
                        "M 0 0 C 1 1 2 2 3 3 C 4 4 5 5 6 6")
    }

    // MARK: - Arcs

    func testZeroRadiusArcIsAStraightLine() {
        assertSameShape("M 0 0 A 0 0 0 0 0 10 10", "M 0 0 L 10 10")
    }

    func testArcWithCoincidentEndpointsIsOmitted() {
        guard let path = parse("M 5 5 A 10 10 0 1 1 5 5 L 9 9") else {
            return XCTFail("no path")
        }
        XCTAssertEqual(types(of: path), [.moveTo, .lineTo],
                       "the arc is dropped; the line after it is not")
    }

    func testArcFlagsMayRunTogetherWithoutSeparators() {
        // The flags are single digits and are the one place the grammar lets
        // numbers abut: "a1 1 0 011 1" is large-arc=0, sweep=1, then (1,1).
        assertSameShape("M 0 0 a 5 5 0 011 1", "M 0 0 a 5 5 0 0 1 1 1")
    }

    func testQuarterCircleArcEndsWhereAsked() {
        guard let path = parse("M 10 0 A 10 10 0 0 1 0 10") else {
            return XCTFail("no path")
        }
        let last = points(of: path).last!
        XCTAssertEqual(last.x, 0, accuracy: 1e-9)
        XCTAssertEqual(last.y, 10, accuracy: 1e-9)
    }

    func testArcIsSplitSoNoSegmentExceedsAQuarterTurn() {
        // A full-circle-ish sweep needs several cubics; one would be wildly off.
        guard let path = parse("M 10 0 A 10 10 0 1 1 -10 0") else {
            return XCTFail("no path")
        }
        XCTAssertGreaterThanOrEqual(types(of: path).filter { $0 == .curveTo }.count, 2)
    }

    func testArcApproximationStaysOnTheCircle() {
        // Sample the rendered path: every point of a circular arc's cubics
        // should sit within a whisker of the circle it approximates.
        guard let path = parse("M 20 0 A 20 20 0 1 1 -20 0") else {
            return XCTFail("no path")
        }
        let flattened = path.flattened
        var samples = [NSPoint](repeating: .zero, count: 3)
        for index in 0..<flattened.elementCount {
            let type = flattened.element(at: index, associatedPoints: &samples)
            guard type == .moveTo || type == .lineTo else { continue }
            let radius = (samples[0].x * samples[0].x + samples[0].y * samples[0].y).squareRoot()
            XCTAssertEqual(radius, 20, accuracy: 0.05)
        }
    }

    func testLargeArcAndSweepFlagsPickDifferentArcs() {
        // The same endpoints and radii describe four arcs; the two flags choose
        // between them, so the four must not come out identical.
        let variants = ["M 0 0 A 10 10 0 0 0 10 10",
                        "M 0 0 A 10 10 0 0 1 10 10",
                        "M 0 0 A 10 10 0 1 0 10 10",
                        "M 0 0 A 10 10 0 1 1 10 10"]
        let midpoints: [NSPoint] = variants.compactMap {
            guard let path = parse($0) else { return nil }
            let all = points(of: path)
            return all[all.count / 2]
        }
        XCTAssertEqual(midpoints.count, 4)
        for i in 0..<4 {
            for j in (i + 1)..<4 {
                XCTAssertFalse(abs(midpoints[i].x - midpoints[j].x) < 1e-6
                               && abs(midpoints[i].y - midpoints[j].y) < 1e-6,
                               "arc variants \(i) and \(j) came out the same")
            }
        }
    }

    func testOutOfRangeRadiiAreScaledUpToReachTheEndpoint() {
        // Radii too small to span the endpoints are enlarged until they fit,
        // rather than the arc being dropped.
        guard let path = parse("M 0 0 A 1 1 0 0 1 20 0") else { return XCTFail("no path") }
        let last = points(of: path).last!
        XCTAssertEqual(last.x, 20, accuracy: 1e-9)
        XCTAssertEqual(last.y, 0, accuracy: 1e-9)
    }

    func testRotatedEllipticalArcEndsWhereAsked() {
        guard let path = parse("M 0 0 A 30 10 45 1 0 20 5") else { return XCTFail("no path") }
        let last = points(of: path).last!
        XCTAssertEqual(last.x, 20, accuracy: 1e-9)
        XCTAssertEqual(last.y, 5, accuracy: 1e-9)
    }

    // MARK: - Error handling

    func testUnknownCommandEndsThePathButKeepsWhatCameBefore() {
        // The specification's rule for malformed path data: render up to the
        // error, discard the rest.
        guard let path = parse("M 0 0 L 5 5 K 9 9 L 7 7") else { return XCTFail("no path") }
        XCTAssertEqual(points(of: path), [NSPoint(x: 0, y: 0), NSPoint(x: 5, y: 5)])
    }

    func testTruncatedCoordinatesAreDroppedNotGuessed() {
        guard let path = parse("M 0 0 L 7") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo])
    }

    func testPathNotBeginningWithAMovetoDrawsNothing() {
        XCTAssertNil(parse("L 10 10 L 20 20"))
        XCTAssertNil(parse("Z"))
    }

    func testAdjacentCommandLettersDoNotStall() {
        guard let path = parse("M 0 0 Z M 1 1 Z") else { return XCTFail("no path") }
        let moves = elements(of: path).filter { $0.0 == .moveTo }.flatMap { $0.1 }
        XCTAssertTrue(moves.contains(NSPoint(x: 0, y: 0)))
        XCTAssertTrue(moves.contains(NSPoint(x: 1, y: 1)))
        XCTAssertEqual(types(of: path).filter { $0 == .closePath }.count, 2)
    }

    // MARK: - Nothing to draw

    func testEmptyStringYieldsNoPath() {
        XCTAssertNil(parse(""))
        XCTAssertNil(parse("   \n\t "))
    }

    func testTextWithNoCommandsYieldsNoPath() {
        XCTAssertNil(parse("1 2 3 4"))
    }

    func testAMovetoAloneYieldsAPathThatDrawsNothing() {
        // A lone moveto is legal and puts no ink on the page. It comes back as
        // a path rather than nil -- NSBezierPath counts the moveto as an
        // element -- which is harmless: stroking or filling it draws nothing.
        guard let path = parse("M 10 10") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo])
    }

    // MARK: - The bundled glyphs

    func testEveryBundledGlyphPathParses() throws {
        // The resource the Touch Bar actually draws. If a re-export ever
        // introduces a spelling the parser cannot read, this fails on any
        // machine rather than showing a blank key on a Touch Bar one.
        //
        // Bundle.main is the app that hosts these tests, which is where the
        // resource is embedded. Not finding it is a failure rather than a
        // skip: a silently skipped test here proves nothing, and a glyph
        // resource missing from the bundle is itself worth knowing about.
        let url = try XCTUnwrap(
            Bundle.main.url(forResource: "TouchBarGlyphs", withExtension: "svg"),
            "TouchBarGlyphs.svg is not in the application bundle")
        let svg = try String(contentsOf: url, encoding: .utf8)

        let pattern = try NSRegularExpression(pattern: "\\sd=\"([^\"]*)\"")
        let matches = pattern.matches(in: svg, range: NSRange(svg.startIndex..., in: svg))
        XCTAssertFalse(matches.isEmpty, "no path data found in the glyph resource")

        for match in matches {
            guard let range = Range(match.range(at: 1), in: svg) else { continue }
            let pathData = String(svg[range])
            XCTAssertNotNil(parse(pathData), "did not parse: \(pathData.prefix(60))")
        }
    }
}
