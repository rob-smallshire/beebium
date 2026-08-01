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

import AppKit
import Foundation

/// Parses the `d` attribute of an SVG `<path>` into an `NSBezierPath`.
///
/// The whole path grammar of SVG 1.1 section 8.3 is accepted, because the
/// glyphs it reads are drawing-tool output and every tool spells paths
/// differently: commas or spaces between coordinates, relative or absolute
/// commands, one coordinate set per command letter or twenty. A parser that
/// takes only the dialect the currently-bundled file happens to use would
/// break silently the next time that file is re-exported.
///
/// What that means concretely:
///
///  * All eleven commands -- `M L H V C S Q T A Z` -- in both cases, lowercase
///    being relative to the current point and uppercase absolute.
///  * `comma-wsp` separators: whitespace, a single comma, or both, anywhere a
///    separator is allowed, and nowhere required (`M10-20` is two numbers, and
///    `10.5.5` is `10.5` then `0.5`).
///  * Numbers with a leading sign, no integer part, or an exponent.
///  * Implicit repetition: further coordinate sets after a command repeat it,
///    with `M`/`m` repeating as `L`/`l` per the specification.
///
/// `NSBezierPath` draws only lines and cubics, so quadratics are converted
/// exactly and elliptical arcs are approximated by cubics over segments of at
/// most a quarter turn -- well under a tenth of a pixel at glyph sizes.
///
/// Error handling follows the specification too: a path that does not begin
/// with a moveto renders nothing, and anything malformed ends the path there
/// while keeping what was already drawn.
enum SvgPathParser {

    /// Parse SVG path data. Returns nil when there is nothing to draw.
    static func path(from pathData: String) -> NSBezierPath? {
        var parser = Parser(pathData)
        let path = parser.parse()
        return path.isEmpty ? nil : path
    }

    // MARK: - Parser

    private struct Parser {
        private let characters: [Character]
        private var index: Int = 0

        private let path = NSBezierPath()
        private var current = CGPoint.zero
        private var subpathStart = CGPoint.zero

        /// The previous cubic's second control point and the previous
        /// quadratic's control point, for the smooth commands to reflect.
        /// Each is cleared by any command that is not of its own family.
        private var previousCubicControl: CGPoint?
        private var previousQuadraticControl: CGPoint?

        init(_ pathData: String) {
            characters = Array(pathData)
        }

        mutating func parse() -> NSBezierPath {
            skipSeparators()
            // A path must open with a moveto. Without one there is no current
            // point, so nothing that follows has a defined start.
            guard let first = peekCommand(), first == "M" || first == "m" else {
                return path
            }

            var command: Character = "M"
            while true {
                skipSeparators()
                if atEnd { break }

                if let next = peekCommand() {
                    index += 1
                    command = next
                } else if command == "M" {
                    command = "L"    // implicit repetition of a moveto draws lines
                } else if command == "m" {
                    command = "l"
                }

                guard run(command) else { break }    // malformed: stop, keep what we have
            }
            return path
        }

        // MARK: One command

        /// Consume one coordinate set for `command` and apply it. Returns false
        /// if the data was malformed, which ends the path.
        private mutating func run(_ command: Character) -> Bool {
            let relative = command.isLowercase
            switch command.uppercased() {
            case "M":
                guard let point = coordinatePair(relative: relative) else { return false }
                move(to: point)
            case "L":
                guard let point = coordinatePair(relative: relative) else { return false }
                line(to: point)
            case "H":
                guard let x = number() else { return false }
                line(to: CGPoint(x: relative ? current.x + x : x, y: current.y))
            case "V":
                guard let y = number() else { return false }
                line(to: CGPoint(x: current.x, y: relative ? current.y + y : y))
            case "C":
                guard let c1 = coordinatePair(relative: relative),
                      let c2 = coordinatePair(relative: relative),
                      let end = coordinatePair(relative: relative) else { return false }
                curve(to: end, control1: c1, control2: c2)
            case "S":
                guard let c2 = coordinatePair(relative: relative),
                      let end = coordinatePair(relative: relative) else { return false }
                curve(to: end, control1: reflectedCubicControl(), control2: c2)
            case "Q":
                guard let control = coordinatePair(relative: relative),
                      let end = coordinatePair(relative: relative) else { return false }
                quadratic(to: end, control: control)
            case "T":
                guard let end = coordinatePair(relative: relative) else { return false }
                quadratic(to: end, control: reflectedQuadraticControl())
            case "A":
                guard let rx = number(), let ry = number(), let rotation = number(),
                      let largeArc = flag(), let sweep = flag(),
                      let end = coordinatePair(relative: relative) else { return false }
                arc(to: end, rx: rx, ry: ry,
                    rotationDegrees: rotation, largeArc: largeArc, sweep: sweep)
            case "Z":
                close()
            default:
                return false     // an unrecognised command ends the path
            }
            return true
        }

        // MARK: Path building

        private mutating func move(to point: CGPoint) {
            path.move(to: point)
            current = point
            subpathStart = point
            previousCubicControl = nil
            previousQuadraticControl = nil
        }

        private mutating func line(to point: CGPoint) {
            path.line(to: point)
            current = point
            previousCubicControl = nil
            previousQuadraticControl = nil
        }

        private mutating func curve(to end: CGPoint, control1: CGPoint, control2: CGPoint) {
            path.curve(to: end, controlPoint1: control1, controlPoint2: control2)
            current = end
            previousCubicControl = control2
            previousQuadraticControl = nil
        }

        /// A quadratic as the cubic with the same shape: both control points
        /// sit two thirds of the way from an endpoint towards the quadratic's
        /// single control point. This is exact, not an approximation.
        private mutating func quadratic(to end: CGPoint, control: CGPoint) {
            let start = current
            let c1 = CGPoint(x: start.x + 2.0 / 3.0 * (control.x - start.x),
                             y: start.y + 2.0 / 3.0 * (control.y - start.y))
            let c2 = CGPoint(x: end.x + 2.0 / 3.0 * (control.x - end.x),
                             y: end.y + 2.0 / 3.0 * (control.y - end.y))
            path.curve(to: end, controlPoint1: c1, controlPoint2: c2)
            current = end
            previousCubicControl = nil
            previousQuadraticControl = control
        }

        private mutating func close() {
            path.close()
            current = subpathStart
            previousCubicControl = nil
            previousQuadraticControl = nil
        }

        /// The control point a smooth cubic starts from: the previous cubic's
        /// second control reflected about the current point, or the current
        /// point itself when the previous command was not a cubic.
        private func reflectedCubicControl() -> CGPoint {
            guard let previous = previousCubicControl else { return current }
            return CGPoint(x: 2 * current.x - previous.x, y: 2 * current.y - previous.y)
        }

        private func reflectedQuadraticControl() -> CGPoint {
            guard let previous = previousQuadraticControl else { return current }
            return CGPoint(x: 2 * current.x - previous.x, y: 2 * current.y - previous.y)
        }

        // MARK: Elliptical arc

        /// Append an endpoint-parameterised elliptical arc, following the
        /// out-of-range and degenerate-case rules of SVG 1.1 appendix F.6.
        private mutating func arc(to end: CGPoint, rx: Double, ry: Double,
                                  rotationDegrees: Double, largeArc: Bool, sweep: Bool) {
            defer {
                previousCubicControl = nil
                previousQuadraticControl = nil
            }

            let start = current
            // Coincident endpoints mean the arc is omitted entirely; a zero
            // radius degenerates it to a straight line. Both are specified.
            if start == end { return }
            var rx = abs(rx), ry = abs(ry)
            if rx == 0 || ry == 0 {
                path.line(to: end)
                current = end
                return
            }

            let phi = rotationDegrees * .pi / 180
            let cosPhi = cos(phi), sinPhi = sin(phi)

            let dx = (start.x - end.x) / 2, dy = (start.y - end.y) / 2
            let x1 = cosPhi * dx + sinPhi * dy
            let y1 = -sinPhi * dx + cosPhi * dy

            // Radii too small to join the endpoints are scaled up until they fit.
            let lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
            if lambda > 1 {
                let scale = lambda.squareRoot()
                rx *= scale
                ry *= scale
            }

            let numerator = max(0, rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1)
            let denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1
            var factor = denominator == 0 ? 0 : (numerator / denominator).squareRoot()
            if largeArc == sweep { factor = -factor }

            let cx1 = factor * rx * y1 / ry
            let cy1 = -factor * ry * x1 / rx
            let centre = CGPoint(
                x: cosPhi * cx1 - sinPhi * cy1 + (start.x + end.x) / 2,
                y: sinPhi * cx1 + cosPhi * cy1 + (start.y + end.y) / 2)

            let startAngle = angle(from: CGPoint(x: 1, y: 0),
                                   to: CGPoint(x: (x1 - cx1) / rx, y: (y1 - cy1) / ry))
            var sweepAngle = angle(from: CGPoint(x: (x1 - cx1) / rx, y: (y1 - cy1) / ry),
                                   to: CGPoint(x: (-x1 - cx1) / rx, y: (-y1 - cy1) / ry))
            sweepAngle = sweepAngle.truncatingRemainder(dividingBy: 2 * .pi)
            if !sweep && sweepAngle > 0 { sweepAngle -= 2 * .pi }
            if sweep && sweepAngle < 0 { sweepAngle += 2 * .pi }

            // A cubic tracks a circular arc closely up to about a quarter turn;
            // beyond that the error grows fast, so split.
            let segments = max(1, Int(ceil(abs(sweepAngle) / (.pi / 2))))
            let delta = sweepAngle / Double(segments)
            let alpha = 4.0 / 3.0 * tan(delta / 4)

            var theta = startAngle
            for _ in 0..<segments {
                let next = theta + delta
                // Control points on the unit circle, then carried through the
                // ellipse's affine map -- which preserves cubics exactly.
                let p1 = ellipse(centre: centre, rx: rx, ry: ry, cosPhi: cosPhi, sinPhi: sinPhi,
                                 x: cos(theta) - alpha * sin(theta),
                                 y: sin(theta) + alpha * cos(theta))
                let p2 = ellipse(centre: centre, rx: rx, ry: ry, cosPhi: cosPhi, sinPhi: sinPhi,
                                 x: cos(next) + alpha * sin(next),
                                 y: sin(next) - alpha * cos(next))
                let p3 = ellipse(centre: centre, rx: rx, ry: ry, cosPhi: cosPhi, sinPhi: sinPhi,
                                 x: cos(next), y: sin(next))
                path.curve(to: p3, controlPoint1: p1, controlPoint2: p2)
                theta = next
            }

            // Land exactly on the requested endpoint rather than on the
            // accumulated trigonometry.
            current = end
        }

        private func ellipse(centre: CGPoint, rx: Double, ry: Double,
                             cosPhi: Double, sinPhi: Double,
                             x: Double, y: Double) -> CGPoint {
            CGPoint(x: centre.x + cosPhi * rx * x - sinPhi * ry * y,
                    y: centre.y + sinPhi * rx * x + cosPhi * ry * y)
        }

        /// The signed angle from one vector to another, in radians.
        private func angle(from u: CGPoint, to v: CGPoint) -> Double {
            let ux = Double(u.x), uy = Double(u.y)
            let vx = Double(v.x), vy = Double(v.y)
            let lengths = (ux * ux + uy * uy).squareRoot() * (vx * vx + vy * vy).squareRoot()
            guard lengths > 0 else { return 0 }
            let cosine = min(1, max(-1, (ux * vx + uy * vy) / lengths))
            let sign: Double = (ux * vy - uy * vx) < 0 ? -1 : 1
            return sign * Foundation.acos(cosine)
        }

        // MARK: Tokens

        private var atEnd: Bool { index >= characters.count }

        private func character(at offset: Int) -> Character? {
            let position = index + offset
            return position < characters.count ? characters[position] : nil
        }

        /// The command letter at the cursor, without consuming it.
        private func peekCommand() -> Character? {
            guard let c = character(at: 0), "MmLlHhVvCcSsQqTtAaZz".contains(c) else { return nil }
            return c
        }

        /// Consume `comma-wsp`: any run of whitespace, with at most one comma
        /// somewhere inside it. Separators are optional everywhere, so this
        /// never fails -- a missing separator is legal when the next token can
        /// begin on its own (a sign, a decimal point, a command letter).
        private mutating func skipSeparators() {
            var sawComma = false
            while let c = character(at: 0) {
                if c == "," && !sawComma {
                    sawComma = true
                    index += 1
                } else if c.isWhitespace {
                    index += 1
                } else {
                    break
                }
            }
        }

        private mutating func coordinatePair(relative: Bool) -> CGPoint? {
            guard let x = number(), let y = number() else { return nil }
            return relative
                ? CGPoint(x: current.x + x, y: current.y + y)
                : CGPoint(x: x, y: y)
        }

        /// An arc's large-arc and sweep parameters are single digits, and are
        /// the one place the grammar allows a number to run straight into the
        /// next without a separator ("a1 1 0 011 1" is well-formed).
        private mutating func flag() -> Bool? {
            skipSeparators()
            switch character(at: 0) {
            case "0": index += 1; return false
            case "1": index += 1; return true
            default:  return nil
            }
        }

        /// Scan one number: optional sign, digits with an optional fractional
        /// part or a fractional part alone, and an optional exponent.
        private mutating func number() -> Double? {
            skipSeparators()
            let start = index

            if let c = character(at: 0), c == "+" || c == "-" { index += 1 }

            var sawDigits = false
            while let c = character(at: 0), c.isNumber { index += 1; sawDigits = true }
            if let c = character(at: 0), c == "." {
                index += 1
                while let d = character(at: 0), d.isNumber { index += 1; sawDigits = true }
            }
            guard sawDigits else {
                index = start
                return nil
            }

            // An "e" only belongs to the number if a valid exponent follows it;
            // otherwise it is the next command letter.
            if let c = character(at: 0), c == "e" || c == "E" {
                let beforeExponent = index
                index += 1
                if let s = character(at: 0), s == "+" || s == "-" { index += 1 }
                var sawExponentDigits = false
                while let d = character(at: 0), d.isNumber { index += 1; sawExponentDigits = true }
                if !sawExponentDigits { index = beforeExponent }
            }

            return Double(String(characters[start..<index]))
        }
    }
}
