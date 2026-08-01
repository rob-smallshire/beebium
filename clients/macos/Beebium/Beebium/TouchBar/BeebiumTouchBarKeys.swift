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
//  BeebiumTouchBarKeys.swift
//  Beebium
//
//  Key area functionality for TouchBar function keys, COPY, BREAK, etc.
//

import Cocoa
import Foundation

// MARK: - Key Data Models

struct BeebiumTouchBarKey: BeebiumTouchBarLayoutElement {
    let identifier: NSTouchBarItem.Identifier
    let backgroundColor: NSColor
    let bbcKeyName: String        // e.g., "f0", "Shift Lock", "Copy"
    let displayName: String       // e.g., "f0", "SHIFT LOCK", "COPY"
    let svgGlyphName: String?     // e.g., "f0" or nil for text keys
    let customizationLabel: String
    let isBreakKey: Bool          // Special handling flag

    // Keys are always 1 key unit wide
    var widthInKeyUnits: CGFloat { 1.0 }
}

// Gap element for spacing between keys, measured in key units
struct BeebiumTouchBarKeyGap: BeebiumTouchBarLayoutElement {
    let widthInKeyUnits: CGFloat

    init(width: CGFloat) {
        self.widthInKeyUnits = width
    }
}

// MARK: - Key Area View

// ButtonRowView: Specialized view for button area with key unit visualization
class BeebiumButtonRowView: BeebiumDiagnosticView {

    // Injected dependencies
    weak var keyboardClient: KeyboardClient?
    weak var bbcKeyCache: BBCKeyCache?
    weak var svgRenderer: BeebiumSvgGlyphRenderer?

    override init(name: String, minimumWidth: CGFloat = 0, contentHuggingPriority: Float? = nil, compressionResistancePriority: Float? = nil) {
        super.init(name: name, minimumWidth: minimumWidth, contentHuggingPriority: contentHuggingPriority, compressionResistancePriority: compressionResistancePriority)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    // MARK: - Touch Event Configuration

    override var acceptsFirstResponder: Bool {
        return true
    }

    // MARK: - Touch Tracking for Event Symmetry

    // Track active touches to ensure symmetric key events
    // Maps each touch's identity (stable across touch lifetime) to the key where it originally began
    // This ensures UP events are sent to the same key as DOWN events,
    // even if the user drags their finger to a different key
    //
    // IMPORTANT: Must use NSMutableDictionary instead of Swift Dictionary because:
    // - touch.identity returns 'NSCopying & NSObjectProtocol' which conforms to NSCopying but NOT Hashable
    // - Swift Dictionary requires Hashable keys, but NSCopying types aren't automatically Hashable
    // - NSDictionary supports any NSCopying-conforming object as a key (per Apple's documentation)
    // - The NSTouch object reference itself is NOT stable across events, but touch.identity IS stable
    private var activeTouches = NSMutableDictionary()

    /// Find which key (if any) is at the given point
    /// Returns the key at that point, or nil if no key is there
    private func findKeyAtPoint(_ point: NSPoint) -> BeebiumTouchBarKey? {
        guard actualWidth > 0 else { return nil }

        let layouts = BeebiumTouchBarKeys.keyboardLayout.calculateLayout(
            widthInLogicalPixels: actualWidth
        )

        for layout in layouts where layout.isKey {
            let keyRect = NSRect(
                x: layout.rect.minX,
                y: 0,
                width: layout.rect.width,
                height: bounds.height
            )

            if keyRect.contains(point) {
                return layout.key
            }
        }

        return nil
    }

    /// Send a key event to the keyboard client.
    ///
    /// Synchronously, from the touch callback: the client's own bookkeeping is
    /// what orders the wire sends, and a Task per touch would let a fast tap's
    /// down and up race each other to the BBC.
    private func sendKeyEvent(key: BeebiumTouchBarKey, isDown: Bool) {
        guard let keyboardClient = keyboardClient else { return }

        if !isDown {
            keyboardClient.touchBarKeyUp(bbcKeyName: key.bbcKeyName)
            return
        }

        // BREAK is a reset line rather than a matrix position, so it has no
        // cache entry and needs no ikNumber.
        var ikNumber: UInt8 = 0
        if !key.isBreakKey {
            guard let entry = bbcKeyCache?.lookup(name: key.bbcKeyName) else {
                print("[TouchBar] Key not found in cache: \(key.bbcKeyName)")
                return
            }
            ikNumber = entry.ikNumber
        }

        keyboardClient.touchBarKeyDown(bbcKeyName: key.bbcKeyName, ikNumber: ikNumber)
    }

    // MARK: - Direct TouchBar Touch Event Handling

    // TouchBar touch event handling
    override func touchesBegan(with event: NSEvent) {
        let touches = event.touches(matching: .began, in: self)

        for touch in touches {
            let localPoint = touch.location(in: self)
            if let key = findKeyAtPoint(localPoint) {
                // Record which key this touch started in - critical for event symmetry
                activeTouches[touch.identity] = key.bbcKeyName as NSString
                sendKeyEvent(key: key, isDown: true)
            }
        }
    }

    override func touchesEnded(with event: NSEvent) {
        let touches = event.touches(matching: .ended, in: self)

        for touch in touches {
            // Use ORIGINAL key from when touch began, not current position
            // This ensures symmetric DOWN/UP events even if user drags between keys
            if let keyName = activeTouches[touch.identity] as? String {
                // Find the key definition by name to send the up event
                if let key = BeebiumTouchBarKeys.keyboardLayout.keys.first(where: { $0.bbcKeyName == keyName }) {
                    sendKeyEvent(key: key, isDown: false)
                }
                activeTouches.removeObject(forKey: touch.identity)
            }
        }
    }

    override func touchesCancelled(with event: NSEvent) {
        let touches = event.touches(matching: .cancelled, in: self)

        for touch in touches {
            if let keyName = activeTouches[touch.identity] as? String {
                // Send UP event to release key
                // Touch cancellation occurs when system interrupts the touch sequence
                if let key = BeebiumTouchBarKeys.keyboardLayout.keys.first(where: { $0.bbcKeyName == keyName }) {
                    sendKeyEvent(key: key, isDown: false)
                }
                activeTouches.removeObject(forKey: touch.identity)
            }
        }
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        // Only draw keys if we have valid measured width
        guard actualWidth > 0, let svgRenderer = svgRenderer else { return }

        // Use cached layout calculation instead of inline algorithm
        let layouts = BeebiumTouchBarKeys.keyboardLayout.calculateLayout(widthInLogicalPixels: actualWidth)

        for layout in layouts where layout.isKey {
            // Draw key using pre-calculated layout
            let keyRect = NSRect(
                x: layout.rect.minX,
                y: 0,
                width: layout.rect.width,
                height: bounds.height
            )

            // Create key image with dynamic size
            let key = layout.key! // Safe because layout.isKey is true
            let keyImage = BeebiumTouchBarKeys.createKeyCapImage(for: key, size: keyRect.size, svgRenderer: svgRenderer)
            keyImage.draw(in: keyRect)
        }
    }
}

// MARK: - Keyboard Layout Architecture

/// Layout geometry for keyboard elements - eliminates algorithm duplication between touch and draw
struct BeebiumKeyLayout {
    let element: BeebiumTouchBarLayoutElement
    let rect: NSRect              // Pre-calculated position and size
    let isKey: Bool              // Cached type check
    let key: BeebiumTouchBarKey?    // Cached key reference if applicable

    init(element: BeebiumTouchBarLayoutElement, rect: NSRect) {
        self.element = element
        self.rect = rect
        self.isKey = element is BeebiumTouchBarKey
        self.key = element as? BeebiumTouchBarKey
    }
}

/// Unified keyboard layout management
class BeebiumKeyboardLayout {
    // Core data storage - preserves gaps and keys as distinct layout elements
    private let elements: [BeebiumTouchBarLayoutElement]
    private let _keys: [BeebiumTouchBarKey]           // Cached extraction - no more repeated compactMap
    private let _totalUnits: CGFloat               // Cached calculation - no more repeated reduce

    // Layout caching for performance optimization
    private var _cachedWidth: CGFloat?             // Last width used for layout calculation
    private var _cachedLayouts: [BeebiumKeyLayout]?       // Cached layout result

    init(elements: [BeebiumTouchBarLayoutElement]) {
        self.elements = elements
        self._keys = elements.compactMap { $0 as? BeebiumTouchBarKey }
        self._totalUnits = elements.reduce(0) { $0 + $1.widthInKeyUnits }
    }

    // Clean access methods - layout semantics preserved, performance improved
    var layoutElements: [BeebiumTouchBarLayoutElement] { elements }
    var keys: [BeebiumTouchBarKey] { _keys }                // Cached, not computed
    var totalKeyUnits: CGFloat { _totalUnits }           // Cached, not computed

    /// Calculate layout positions for all elements at given width (with caching)
    func calculateLayout(widthInLogicalPixels width: CGFloat) -> [BeebiumKeyLayout] {
        // Return cached result if width hasn't changed
        if let cachedWidth = _cachedWidth,
           let cachedLayouts = _cachedLayouts,
           abs(cachedWidth - width) < 0.01 { // Use small epsilon for float comparison
            return cachedLayouts
        }

        // Calculate new layout
        let keyUnitWidth = width / totalKeyUnits
        var currentX: CGFloat = 0
        var layouts: [BeebiumKeyLayout] = []

        for element in elements {
            let elementWidth = element.widthInKeyUnits * keyUnitWidth
            let rect = NSRect(
                x: currentX,
                y: 0,
                width: elementWidth,
                height: BeebiumAppearance.Geometry.touchBarHeight
            )

            layouts.append(BeebiumKeyLayout(element: element, rect: rect))
            currentX += elementWidth
        }

        // Cache the results for future calls
        _cachedWidth = width
        _cachedLayouts = layouts
        return layouts
    }
}

/// Centralized appearance constants for TouchBar key visual styling
struct BeebiumAppearance {
    // MARK: - Geometry
    struct Geometry {
        static let cornerRadius: CGFloat = 6
        static let borderWidth: CGFloat = 0.5
        static let horizontalMargin: CGFloat = 2
        static let verticalMargin: CGFloat = 0
        static let touchBarHeight: CGFloat = 30
    }

    // MARK: - Typography
    struct Typography {
        static let keyTextSize: CGFloat = 10
        static let functionKeyDigitSize: CGFloat = 12
        static let functionKeyFSize: CGFloat = 18  // 1.5x digit size
        static let keyTextWeight: NSFont.Weight = .medium
        static let lineSpacing: CGFloat = 2
    }

    // MARK: - Colors
    struct Colors {
        static let borderColor = NSColor.black.withAlphaComponent(0.2)
        static let textColor = NSColor.white

        // Key background colors
        static let functionKeyColor = NSColor(red: 0.83, green: 0.29, blue: 0.24, alpha: 1.0)    // #d34b3d
        static let copyKeyColor = NSColor(red: 0.325, green: 0.267, blue: 0.141, alpha: 1.0)     // #534424
        static let regularKeyColor = NSColor(red: 0.125, green: 0.114, blue: 0.118, alpha: 1.0)  // #201D1E
    }

    // MARK: - Effects
    struct Effects {
        static let borderAlpha: CGFloat = 0.2
    }
}


// MARK: - BeebiumTouchBarKeys Component Class

@available(macOS 10.12.2, *)
@MainActor
class BeebiumTouchBarKeys: BeebiumTouchBarComponent {

    // MARK: - Properties

    // Single source of truth for keyboard layout
    internal static let keyboardLayout = BeebiumKeyboardLayout(elements: {
        var layout: [BeebiumTouchBarLayoutElement] = []

        // SHIFT LOCK key (1 key unit)
        layout.append(BeebiumTouchBarKey(
            identifier: NSTouchBarItem.Identifier("com.beebium.touchbar.shiftlock"),
            backgroundColor: BeebiumAppearance.Colors.regularKeyColor,
            bbcKeyName: "Shift Lock",
            displayName: "SHIFT LOCK",
            svgGlyphName: nil,
            customizationLabel: "BBC Micro SHIFT LOCK Key",
            isBreakKey: false
        ))

        // Add gap before f-keys for visual separation
        layout.append(BeebiumTouchBarKeyGap(width: 0.1))

        // Function keys f0-f9 (each 1 key unit)
        for i in 0...9 {
            let keyName = "f\(i)"
            layout.append(BeebiumTouchBarKey(
                identifier: NSTouchBarItem.Identifier("com.beebium.touchbar.\(keyName)"),
                backgroundColor: BeebiumAppearance.Colors.functionKeyColor,
                bbcKeyName: keyName,
                displayName: keyName,
                svgGlyphName: keyName,
                customizationLabel: "BBC Micro \(keyName) Key",
                isBreakKey: false
            ))
        }

        // Add gap before COPY key for visual separation
        layout.append(BeebiumTouchBarKeyGap(width: 0.1))

        // COPY key (1 key unit)
        layout.append(BeebiumTouchBarKey(
            identifier: NSTouchBarItem.Identifier("com.beebium.touchbar.copy"),
            backgroundColor: BeebiumAppearance.Colors.copyKeyColor,
            bbcKeyName: "Copy",
            displayName: "COPY",
            svgGlyphName: nil,
            customizationLabel: "BBC Micro COPY Key",
            isBreakKey: false
        ))

        // Add gap before BREAK key
        layout.append(BeebiumTouchBarKeyGap(width: 0.3))

        // BREAK key (1 key unit)
        layout.append(BeebiumTouchBarKey(
            identifier: NSTouchBarItem.Identifier("com.beebium.touchbar.break"),
            backgroundColor: BeebiumAppearance.Colors.regularKeyColor,
            bbcKeyName: "Break",
            displayName: "BREAK",
            svgGlyphName: nil,
            customizationLabel: "BBC Micro BREAK Key",
            isBreakKey: true
        ))

        return layout
    }())

    // Extract just the keys from the layout for compatibility - now cached instead of computed
    internal static var touchBarKeys: [BeebiumTouchBarKey] {
        return keyboardLayout.keys
    }

    // Computed properties based on layout array - now cached instead of computed
    internal static var totalKeyUnits: CGFloat { keyboardLayout.totalKeyUnits }

    // Injected dependencies
    private let svgRenderer: BeebiumSvgGlyphRenderer
    private weak var keyboardClient: KeyboardClient?
    private weak var bbcKeyCache: BBCKeyCache?

    // TouchBar identifier for this component
    private let identifier: NSTouchBarItem.Identifier

    // MARK: - Initialization

    init(svgRenderer: BeebiumSvgGlyphRenderer,
         keyboardClient: KeyboardClient?,
         bbcKeyCache: BBCKeyCache?,
         identifier: NSTouchBarItem.Identifier) {
        self.svgRenderer = svgRenderer
        self.keyboardClient = keyboardClient
        self.bbcKeyCache = bbcKeyCache
        self.identifier = identifier
    }

    // MARK: - BeebiumTouchBarComponent Protocol Implementation

    var componentName: String { "Keys" }

    func createTouchBarItem() -> NSCustomTouchBarItem {
        let item = NSCustomTouchBarItem(identifier: identifier)

        // Create button area container with responsive sizing:
        // - Minimum width: 685 lpx (fits all keys at Control Strip enabled width)
        // - Expands to fill available space when Control Strip disabled
        // - Low content hugging allows expansion, high compression resistance protects minimum
        let minWidth: CGFloat = 685
        let containerView = BeebiumButtonRowView(
            name: "ButtonArea",
            minimumWidth: minWidth,
            contentHuggingPriority: 200,       // Low - allows expansion
            compressionResistancePriority: 1000 // High - protects minimum
        )

        // Inject dependencies
        containerView.keyboardClient = keyboardClient
        containerView.bbcKeyCache = bbcKeyCache
        containerView.svgRenderer = svgRenderer

        // Preferred width constraint encourages expansion to ~1000 lpx when Control Strip disabled
        let preferredWidth: CGFloat = 1000
        containerView.translatesAutoresizingMaskIntoConstraints = false
        let preferredWidthConstraint = containerView.widthAnchor.constraint(equalToConstant: preferredWidth)
        preferredWidthConstraint.priority = NSLayoutConstraint.Priority(250) // Low priority - yields to system constraints
        preferredWidthConstraint.isActive = true

        containerView.wantsLayer = true

        // Hidden label with constraints provides "intrinsic content size" for responsive layout.
        let label = NSTextField()
        label.stringValue = "BUTTON (\(Int(minWidth))px)"
        label.isBezeled = false
        label.isEditable = false
        label.isSelectable = false
        label.translatesAutoresizingMaskIntoConstraints = false
        containerView.addSubview(label)

        containerView.widthLabel = label  // Store reference for DiagnosticView to update with actual width

        NSLayoutConstraint.activate([
            label.leadingAnchor.constraint(equalTo: containerView.leadingAnchor, constant: 5),
            label.trailingAnchor.constraint(equalTo: containerView.trailingAnchor, constant: -5),
            label.topAnchor.constraint(equalTo: containerView.topAnchor, constant: 5),
            label.heightAnchor.constraint(equalToConstant: 20)
        ])

        label.isHidden = true     // Hidden but drives responsive layout via constraints
        label.isEnabled = false   // Disabled to prevent intercepting TouchBar touch events

        item.view = containerView
        item.customizationLabel = "BBC Micro Function Keys"
        item.visibilityPriority = .high  // Mandatory group - always try to keep visible

        return item
    }

    func updateState() {
        // Future: Update key states based on BBC Micro state
    }

    // MARK: - Key Rendering Methods

    /// Create standardized text attributes using centralized styling constants
    private static func createTextAttributes() -> [NSAttributedString.Key: Any] {
        let style = NSMutableParagraphStyle()
        style.alignment = .center

        return [
            .foregroundColor: BeebiumAppearance.Colors.textColor,
            .font: NSFont.systemFont(ofSize: BeebiumAppearance.Typography.keyTextSize,
                                   weight: BeebiumAppearance.Typography.keyTextWeight),
            .paragraphStyle: style
        ]
    }

    /// Enhanced multi-line text drawing with centralized styling
    private static func drawMultiLineText(_ text: String, in rect: NSRect) {
        let attributes = createTextAttributes()
        drawMultiLineText(text, in: rect, withAttributes: attributes)
    }

    // Helper method for drawing multi-line text centered within a rectangle
    private static func drawMultiLineText(_ text: String, in rect: NSRect, withAttributes attributes: [NSAttributedString.Key: Any]) {
        let words = text.split(separator: " ").map(String.init)

        // Unified algorithm handles both single and multi-line text
        let lineHeight = "M".size(withAttributes: attributes).height
        let lineSpacing = BeebiumAppearance.Typography.lineSpacing
        let totalTextHeight = CGFloat(words.count) * lineHeight + CGFloat(words.count - 1) * lineSpacing

        // Start position for first line (top of centered text block)
        let startY = rect.midY + totalTextHeight / 2 - lineHeight

        // Draw each word as a separate line
        for (index, word) in words.enumerated() {
            let wordSize = word.size(withAttributes: attributes)
            let wordRect = NSRect(
                x: rect.midX - wordSize.width / 2,  // Center horizontally
                y: startY - CGFloat(index) * (lineHeight + lineSpacing),  // Stack vertically
                width: wordSize.width,
                height: lineHeight
            )
            word.draw(in: wordRect, withAttributes: attributes)
        }
    }

    /// Draw key background with rounded rectangle and border using centralized styling
    private static func drawKeyBackground(for keyDef: BeebiumTouchBarKey, in rect: NSRect) {
        // Calculate button rectangle with margins from constants
        let buttonRect = rect.insetBy(
            dx: BeebiumAppearance.Geometry.horizontalMargin,
            dy: BeebiumAppearance.Geometry.verticalMargin
        )

        // Create rounded rectangle path using centralized radius
        let path = NSBezierPath(
            roundedRect: buttonRect,
            xRadius: BeebiumAppearance.Geometry.cornerRadius,
            yRadius: BeebiumAppearance.Geometry.cornerRadius
        )

        // Fill with background color
        keyDef.backgroundColor.setFill()
        path.fill()

        // Add border with centralized styling
        BeebiumAppearance.Colors.borderColor.setStroke()
        path.lineWidth = BeebiumAppearance.Geometry.borderWidth
        path.stroke()
    }

    // Static version for use by other components (avoids needing component instance)
    static func createKeyCapImage(for keyDef: BeebiumTouchBarKey, size: NSSize, svgRenderer: BeebiumSvgGlyphRenderer) -> NSImage {
        return NSImage(size: size, flipped: false) { rect in
            // Orchestrated rendering pipeline using extracted methods
            Self.drawKeyBackground(for: keyDef, in: rect)

            // Draw content with provided SVG renderer
            let buttonRect = rect.insetBy(
                dx: BeebiumAppearance.Geometry.horizontalMargin,
                dy: BeebiumAppearance.Geometry.verticalMargin
            )

            if let svgGlyphName = keyDef.svgGlyphName {
                // Use SVG glyph for function keys
                svgRenderer.renderGlyph(named: svgGlyphName, in: buttonRect)
            } else {
                // Standard text for COPY/BREAK/SHIFT LOCK keys using centralized styling
                Self.drawMultiLineText(keyDef.displayName, in: buttonRect)
            }

            return true
        }
    }
}
