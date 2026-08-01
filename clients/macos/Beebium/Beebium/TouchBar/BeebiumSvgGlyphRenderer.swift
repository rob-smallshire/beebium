// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

//
//  BeebiumSvgGlyphRenderer.swift
//  Beebium
//
//  Standalone SVG glyph renderer for TouchBar function keys
//
//  DESIGN RATIONALE:
//
//  This renderer implements a minimal, lightweight SVG parser specifically for rendering
//  simple glyph graphics on the TouchBar. It is intentionally simplistic to avoid introducing
//  large external dependencies (such as full SVG rendering frameworks like SVGKit or WebKit).
//
//  WHY NOT USE A FULL SVG LIBRARY?
//  - Dependency avoidance: Full SVG libraries are heavyweight (100KB+ of code)
//  - Simple requirements: We only need basic path rendering for function key glyphs
//  - Performance: Custom parser is optimized for our specific use case
//  - Control: Direct access to path data allows precise rendering control
//  - Maintainability: Simple code is easier to understand and debug
//
//  WHAT THIS PARSER SUPPORTS:
//  - Basic path commands: M (moveto), L (lineto), C (cubic bezier), Z (closepath)
//  - Very basic CSS styles via style="..." attributes with stroke-width and fill properties
//  - Groups with ID attributes for glyph identification
//
//  WHAT THIS PARSER DOES NOT SUPPORT:
//  - Advanced SVG features (gradients, filters, masks, patterns)
//  - Relative path commands (m, l, c, etc.)
//  - Arc commands (A, a)
//  - Quadratic bezier curves (Q, q)
//  - Transformations in path data
//  - Text elements
//  - External references
//
//  ARCHITECTURE:
//  - Uses Foundation's XMLParser for robust XML parsing (event-based SAX parser)
//  - Caches parsed glyph data to avoid repeated I/O and parsing
//  - Converts SVG path data to NSBezierPath for native Cocoa rendering
//
//  For SVG structure requirements, see comments in TouchBarGlyphs.svg
//

import Cocoa
import Foundation

// MARK: - SVG Glyph Renderer

@available(macOS 10.12.2, *)
class BeebiumSvgGlyphRenderer {

    // MARK: - Types

    typealias GlyphData = (paths: [String], strokeWidths: [CGFloat], fillModes: [Bool])

    // MARK: - Properties

    // Injected SVG resource configuration
    private let svgResourceName: String
    private let svgResourceType: String

    // Instance cache for this renderer's glyphs
    private var glyphCache: [String: GlyphData] = [:]

    // MARK: - Initialization

    init(svgResourceName: String, svgResourceType: String = "svg") {
        self.svgResourceName = svgResourceName
        self.svgResourceType = svgResourceType
    }

    // MARK: - Public Interface

    /// Render an SVG glyph using instance renderer with injected resource
    func renderGlyph(named title: String, in rect: NSRect) {
        // Load glyph data from SVG file or cache
        guard let glyphData = self.loadGlyphData(for: title) else {
            // Fallback to text if glyph data not found
            Self.drawFunctionKeyText(title, in: rect)
            return
        }

        // Calculate scaling to fit 100x100 SVG into TouchBar key, 50% larger than letterboxing
        let svgSize: CGFloat = 100.0
        let targetRect = rect.insetBy(dx: 4, dy: 4) // Add some padding
        let letterboxScale = min(targetRect.width / svgSize, targetRect.height / svgSize)
        let scale = letterboxScale * 1.6  // 50% larger than letterbox fit

        // Center the scaled SVG in the target area
        let scaledSize = svgSize * scale
        let xOffset = targetRect.minX + (targetRect.width - scaledSize) / 2
        let yOffset = targetRect.minY + (targetRect.height - scaledSize) / 2

        // Set up coordinate system transformation
        NSGraphicsContext.current?.saveGraphicsState()

        // Translate and scale to fit SVG coordinate system with Y-axis flip
        let transform = NSAffineTransform()
        transform.translateX(by: xOffset, yBy: yOffset + scaledSize) // Move to bottom-left of target area
        transform.scaleX(by: scale, yBy: -scale) // Flip Y-axis to match SVG coordinate system
        transform.concat()

        // Parse and render each path
        for (index, pathString) in glyphData.paths.enumerated() {
            if index < glyphData.strokeWidths.count && index < glyphData.fillModes.count {
                let originalStrokeWidth = glyphData.strokeWidths[index]
                let isFill = glyphData.fillModes[index]

                // Calculate stroke width for non-fill paths
                var strokeWidth = originalStrokeWidth
                if !isFill && originalStrokeWidth > 0 {
                    // Calculate proportional stroke width: if 5px was 5% of 100px SVG,
                    // it should remain 5% of the final rendered size
                    let targetWidth = targetRect.width - 8 // Account for padding
                    let proportionalStroke = (originalStrokeWidth / svgSize) * targetWidth
                    // Use minimum of 4.0px for TouchBar visibility
                    strokeWidth = max(4.0, proportionalStroke)
                }

                if let bezierPath = Self.parseSVGPath(pathString.trimmingCharacters(in: .whitespaces)) {
                    Self.renderPath(bezierPath, strokeWidth: strokeWidth, isFill: isFill)
                }
            }
        }

        NSGraphicsContext.current?.restoreGraphicsState()
    }

    // MARK: - XML Parser Delegate

    private class SVGPathParserDelegate: NSObject, XMLParserDelegate {
        private var currentGroupId: String?
        private var currentPaths: [String] = []
        private var currentStrokeWidths: [CGFloat] = []
        private var currentFillModes: [Bool] = []

        // Collected results for all groups
        var allGroups: [String: GlyphData] = [:]

        func parser(_ parser: XMLParser, didStartElement elementName: String,
                   namespaceURI: String?, qualifiedName qName: String?,
                   attributes attributeDict: [String : String] = [:]) {

            // Check if we're entering a group with an ID
            if elementName == "g", let groupId = attributeDict["id"] {
                currentGroupId = groupId
                currentPaths = []
                currentStrokeWidths = []
                currentFillModes = []
                return
            }

            // Parse path elements inside any group
            if currentGroupId != nil && elementName == "path" {
                // Extract path data from 'd' attribute
                guard let pathData = attributeDict["d"] else {
                    return
                }

                currentPaths.append(pathData)

                // Parse style attribute if present
                var strokeWidth: CGFloat = 0
                var isFill = true

                if let styleString = attributeDict["style"] {
                    // Parse CSS-style properties from style attribute
                    let styleProperties = parseStyleString(styleString)

                    // Extract stroke-width
                    if let strokeWidthStr = styleProperties["stroke-width"] {
                        // Remove 'px' suffix if present
                        let numericStr = strokeWidthStr.replacingOccurrences(of: "px", with: "").trimmingCharacters(in: .whitespaces)
                        strokeWidth = CGFloat(Double(numericStr) ?? 0)
                    }

                    // Extract fill mode
                    if let fillValue = styleProperties["fill"] {
                        isFill = fillValue.trimmingCharacters(in: .whitespaces).lowercased() != "none"
                    }
                }

                currentStrokeWidths.append(strokeWidth)
                currentFillModes.append(isFill)
            }
        }

        func parser(_ parser: XMLParser, didEndElement elementName: String,
                   namespaceURI: String?, qualifiedName qName: String?) {
            // Exit group and save collected data
            if elementName == "g", let groupId = currentGroupId {
                if !currentPaths.isEmpty {
                    allGroups[groupId] = (paths: currentPaths, strokeWidths: currentStrokeWidths, fillModes: currentFillModes)
                }
                currentGroupId = nil
            }
        }

        // Parse CSS-style property string into dictionary
        private func parseStyleString(_ styleString: String) -> [String: String] {
            var properties: [String: String] = [:]

            // Split by semicolons to get individual property declarations
            let declarations = styleString.components(separatedBy: ";")

            for declaration in declarations {
                // Split by colon to get property name and value
                let parts = declaration.components(separatedBy: ":")
                guard parts.count == 2 else { continue }

                let key = parts[0].trimmingCharacters(in: .whitespaces).lowercased()
                let value = parts[1].trimmingCharacters(in: .whitespaces)

                properties[key] = value
            }

            return properties
        }
    }

    // MARK: - SVG Processing

    private func loadGlyphData(for title: String) -> GlyphData? {
        // Check instance cache first
        if let cachedData = glyphCache[title] {
            return cachedData
        }

        // If cache is empty, we need to parse the SVG file once for all glyphs
        if glyphCache.isEmpty {
            parseAllGlyphs()
        }

        // Return cached data for the requested glyph
        return glyphCache[title]
    }

    private func parseAllGlyphs() {
        // Load SVG file once
        guard let bundle = Bundle.main.path(forResource: svgResourceName, ofType: svgResourceType),
              let svgData = NSData(contentsOfFile: bundle),
              let svgContent = String(data: svgData as Data, encoding: .utf8) else {
            print("[BeebiumSvgGlyphRenderer] Failed to load \(svgResourceName).\(svgResourceType)")
            return
        }

        // Parse all groups in single pass
        guard let data = svgContent.data(using: .utf8) else {
            return
        }

        let parser = XMLParser(data: data)
        let delegate = SVGPathParserDelegate()
        parser.delegate = delegate

        // Parse the XML
        guard parser.parse() else {
            if let error = parser.parserError {
                print("[BeebiumSvgGlyphRenderer] XML parsing error: \(error.localizedDescription)")
            }
            return
        }

        // Cache all parsed groups
        glyphCache = delegate.allGroups

        print("[BeebiumSvgGlyphRenderer] Cached \(glyphCache.count) glyph(s) from \(svgResourceName).\(svgResourceType)")
    }

    static func parseSVGPath(_ pathData: String) -> NSBezierPath? {
        let path = NSBezierPath()
        let scanner = Scanner(string: pathData)
        scanner.charactersToBeSkipped = CharacterSet.whitespacesAndNewlines

        while !scanner.isAtEnd {
            if let command = scanner.scanCharacters(from: CharacterSet.letters) {
                switch command.uppercased() {
                case "M": // Move to
                    if let point = scanPoint(scanner) {
                        path.move(to: point)
                    }

                case "L": // Line to
                    if let point = scanPoint(scanner) {
                        path.line(to: point)
                    }

                case "C": // Cubic bezier curve
                    if let cp1 = scanPoint(scanner),
                       let cp2 = scanPoint(scanner),
                       let end = scanPoint(scanner) {
                        path.curve(to: end, controlPoint1: cp1, controlPoint2: cp2)
                    }

                case "Z": // Close path
                    path.close()

                default:
                    // Skip unknown commands by scanning to next letter
                    _ = scanner.scanUpToCharacters(from: CharacterSet.letters)
                }
            } else {
                // Skip non-command content
                _ = scanner.scanUpToCharacters(from: CharacterSet.letters)
            }
        }

        return path.isEmpty ? nil : path
    }

    private static func scanPoint(_ scanner: Scanner) -> NSPoint? {
        guard let x = scanner.scanDouble(), let y = scanner.scanDouble() else {
            return nil
        }
        return NSPoint(x: x, y: y)
    }

    private static func drawFunctionKeyText(_ title: String, in rect: NSRect) {
        // Extract "f" and digit from title (e.g., "f0" -> "f" + "0")
        let fCharacter = String(title.prefix(1))  // "f"
        let digitCharacter = String(title.suffix(1))  // "0", "1", etc.

        // Base font size for digit
        let digitSize: CGFloat = 12
        let fSize: CGFloat = digitSize * 1.5  // 50% larger

        // Create fonts
        let digitFont = NSFont.systemFont(ofSize: digitSize, weight: .medium)

        // Create italic font using font descriptor
        let systemFont = NSFont.systemFont(ofSize: fSize, weight: .medium)
        let fontDescriptor = systemFont.fontDescriptor.withSymbolicTraits(.italic)
        let fFont = NSFont(descriptor: fontDescriptor, size: fSize) ?? systemFont

        // Text attributes
        let digitAttributes: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.white,
            .font: digitFont
        ]

        let fAttributes: [NSAttributedString.Key: Any] = [
            .foregroundColor: NSColor.white,
            .font: fFont
        ]

        // Measure text sizes
        let fSizeMeasured = fCharacter.size(withAttributes: fAttributes)
        let digitSizeMeasured = digitCharacter.size(withAttributes: digitAttributes)

        // Calculate total width and positioning
        let totalWidth = fSizeMeasured.width + digitSizeMeasured.width
        let startX = rect.midX - totalWidth / 2

        // Calculate baseline alignment - align based on digit baseline
        let digitBaseline = rect.midY - digitSizeMeasured.height / 2
        let fBaseline = digitBaseline + (digitSizeMeasured.height - fSizeMeasured.height) / 2

        // Draw "f" (italic)
        let fRect = NSRect(
            x: startX,
            y: fBaseline,
            width: fSizeMeasured.width,
            height: fSizeMeasured.height
        )
        fCharacter.draw(in: fRect, withAttributes: fAttributes)

        // Draw digit (upright)
        let digitRect = NSRect(
            x: startX + fSizeMeasured.width,
            y: digitBaseline,
            width: digitSizeMeasured.width,
            height: digitSizeMeasured.height
        )
        digitCharacter.draw(in: digitRect, withAttributes: digitAttributes)
    }

    // MARK: - Rendering

    private static func renderPath(_ path: NSBezierPath, strokeWidth: CGFloat, isFill: Bool) {
        // Set stroke properties for high-quality rendering
        path.lineCapStyle = .round
        path.lineJoinStyle = .round

        // Fill and stroke with white color
        NSColor.white.setFill()
        NSColor.white.setStroke()

        if isFill {
            // This is a filled path
            path.fill()
        } else if strokeWidth > 0 {
            // This is a stroked path - set width and stroke
            path.lineWidth = strokeWidth
            path.stroke()
        }
    }
}
