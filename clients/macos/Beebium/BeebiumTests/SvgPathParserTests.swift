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

/// The SVG path parser behind the Touch Bar's function-key glyphs.
///
/// It is exercised only on a Mac with a Touch Bar, so a silent regression here
/// would go unnoticed for a long time -- which is why it is pinned down now
/// that the scanning has moved off Scanner's deprecated out-parameter API onto
/// the returning one. The two differ in how they report "scanned nothing", and
/// this parser's loop depends on that to make progress.
final class SvgPathParserTests: XCTestCase {

    private func parse(_ pathData: String) -> NSBezierPath? {
        BeebiumSvgGlyphRenderer.parseSVGPath(pathData)
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

    // MARK: - Commands

    func testMoveAndLine() {
        guard let path = parse("M 10 20 L 30 40") else { return XCTFail("no path") }
        let parsed = elements(of: path)
        XCTAssertEqual(parsed.map { $0.0 }, [.moveTo, .lineTo])
        XCTAssertEqual(parsed[0].1, [NSPoint(x: 10, y: 20)])
        XCTAssertEqual(parsed[1].1, [NSPoint(x: 30, y: 40)])
    }

    func testCubicCurveTakesThreePoints() {
        guard let path = parse("M 0 0 C 1 2 3 4 5 6") else { return XCTFail("no path") }
        let parsed = elements(of: path)
        XCTAssertEqual(parsed.map { $0.0 }, [.moveTo, .curveTo])
        // AppKit stores curveTo as (control1, control2, endpoint).
        XCTAssertEqual(parsed[1].1,
                       [NSPoint(x: 1, y: 2), NSPoint(x: 3, y: 4), NSPoint(x: 5, y: 6)])
    }

    func testClosePath() {
        guard let path = parse("M 0 0 L 10 0 Z") else { return XCTFail("no path") }
        // NSBezierPath.close() appends an implicit moveTo back to the start of
        // the subpath as well as the closePath, so this asserts the commands
        // were recognised rather than a literal element count.
        XCTAssertEqual(types(of: path).prefix(3), [.moveTo, .lineTo, .closePath])
    }

    func testLowercaseCommandsAreAccepted() {
        guard let lower = parse("m 1 1 l 2 2 z"),
              let upper = parse("M 1 1 L 2 2 Z") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: lower), types(of: upper))
        XCTAssertEqual(elements(of: lower).map { $0.1 }, elements(of: upper).map { $0.1 })
    }

    func testNegativeAndFractionalCoordinates() {
        guard let path = parse("M -1.5 2.25 L 3 -4.75") else { return XCTFail("no path") }
        let parsed = elements(of: path)
        XCTAssertEqual(parsed[0].1, [NSPoint(x: -1.5, y: 2.25)])
        XCTAssertEqual(parsed[1].1, [NSPoint(x: 3, y: -4.75)])
    }

    func testCommaSeparatedCoordinates() {
        // Commas are not in charactersToBeSkipped, so they stop a number scan
        // and the coordinate pair after them is dropped. Pinning the behaviour
        // as it is: the bundled glyphs use spaces, and widening the parser is
        // not what this change is for.
        XCTAssertNil(parse("M 10,20"), "a comma-separated pair yields no usable path")
    }

    // MARK: - Progress and recovery

    func testUnknownCommandIsSkippedWithoutLosingWhatFollows() {
        guard let path = parse("M 0 0 Q 9 9 9 9 L 5 5") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo, .lineTo],
                       "the unrecognised Q is dropped, the L after it is not")
    }

    func testAdjacentCommandLettersDoNotStall() {
        // Z followed immediately by another letter is the case where scanning
        // "up to a letter" consumes nothing. The loop must still terminate --
        // reaching the assertions at all is most of what this test checks --
        // and must not lose the second subpath on the way.
        guard let path = parse("M 0 0 Z M 1 1 Z") else { return XCTFail("no path") }
        let moves = elements(of: path).filter { $0.0 == .moveTo }.flatMap { $0.1 }
        XCTAssertTrue(moves.contains(NSPoint(x: 0, y: 0)))
        XCTAssertTrue(moves.contains(NSPoint(x: 1, y: 1)))
        XCTAssertEqual(types(of: path).filter { $0 == .closePath }.count, 2)
    }

    func testLeadingJunkBeforeTheFirstCommandIsSkipped() {
        guard let path = parse("  42 M 1 2 L 3 4") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo, .lineTo])
    }

    func testTruncatedCoordinatesAreDroppedNotGuessed() {
        // "L 7" has no y. The line is not emitted with a made-up coordinate.
        guard let path = parse("M 0 0 L 7") else { return XCTFail("no path") }
        XCTAssertEqual(types(of: path), [.moveTo])
    }

    // MARK: - Nothing to draw

    func testEmptyStringYieldsNoPath() {
        XCTAssertNil(parse(""))
    }

    func testTextWithNoCommandsYieldsNoPath() {
        XCTAssertNil(parse("1 2 3 4"))
    }

    func testOnlyUnknownCommandsYieldsNoPath() {
        XCTAssertNil(parse("Q 1 2 3 4"))
    }
}
