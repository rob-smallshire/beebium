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
//  BeebiumTouchBarLEDs.swift
//  Beebium
//
//  LED area functionality for TouchBar status indicators
//

import Cocoa
import Foundation

// MARK: - LED Data Models

struct BeebiumTouchBarLED {
    let label: String           // "CAPS LOCK", "SHIFT LOCK"
    let indicatorName: String   // "caps-lock-led", "shift-lock-led"
    let labelLEDSpacing: CGFloat  // signed spacing between label and LED

    // Derived property - splits label on spaces
    var labelWords: [String] {
        return label.split(separator: " ").map(String.init)
    }

    // Behavioral methods
    func measureLabelWidth(font: NSFont) -> CGFloat {
        let textAttributes: [NSAttributedString.Key: Any] = [.font: font]
        let words = labelWords
        let wordWidths = words.map { ($0 as NSString).size(withAttributes: textAttributes).width }
        let maxWidth = wordWidths.max() ?? 0
        return maxWidth + 8
    }

    func wordWidths(font: NSFont) -> [CGFloat] {
        let textAttributes: [NSAttributedString.Key: Any] = [.font: font]
        let words = labelWords
        return words.map { ($0 as NSString).size(withAttributes: textAttributes).width }
    }

    func wordOffsets(font: NSFont) -> [CGFloat] {
        let labelWidth = measureLabelWidth(font: font)
        let wWidths = wordWidths(font: font)
        return wWidths.map { wordWidth in
            (labelWidth - wordWidth) / 2
        }
    }

    @MainActor
    func brightness(indicatorClient: IndicatorClient) -> UInt32 {
        return indicatorClient.values[indicatorName] ?? 0
    }
}

/// Layout geometry for a single LED unit (label + LED image)
struct BeebiumLEDLayout {
    let led: BeebiumTouchBarLED  // Reference to the associated LED object
    let font: NSFont            // Font used for text measurement and rendering
    let labelWidth: CGFloat     // The calculated width of the label area in logical pixels
    let labelX: CGFloat         // The horizontal position where the label should be placed
    let ledX: CGFloat          // The horizontal position where the LED image should be placed
    let wordOffsets: [CGFloat] // Array of centering offsets for each word within the label area

    // TouchBar layout constants
    static let containerHeight: CGFloat = 30  // Logical TouchBar height for controls

    init(led: BeebiumTouchBarLED, font: NSFont, labelX: CGFloat, ledX: CGFloat) {
        self.led = led
        self.font = font
        self.labelWidth = led.measureLabelWidth(font: font)
        self.labelX = labelX
        self.ledX = ledX
        self.wordOffsets = led.wordOffsets(font: font)
    }

    // MARK: - View Creation Methods

    func createLEDImageView(ledWidth: CGFloat, onImage: NSImage, offImage: NSImage, brightness: UInt32) -> NSImageView {
        let ledImageView = NSImageView()
        ledImageView.frame = NSRect(
            x: ledX,
            y: (Self.containerHeight - ledWidth) / 2,
            width: ledWidth,
            height: ledWidth
        )
        ledImageView.imageScaling = .scaleProportionallyUpOrDown

        // Create blended image based on brightness
        ledImageView.image = BeebiumIndicatorPanelView.createBlendedLEDImage(
            brightness: brightness,
            onImage: onImage,
            offImage: offImage
        )

        return ledImageView
    }

    func createLabelView() -> NSView {
        let labelView = NSView(frame: NSRect(
            x: labelX,
            y: 0,
            width: labelWidth,
            height: Self.containerHeight
        ))

        // Dynamic multi-line label rendering - handles variable word counts
        let lineHeight: CGFloat = 10
        let lineSpacing: CGFloat = 0  // No gap between LED label lines
        let totalTextHeight = CGFloat(led.labelWords.count) * lineHeight + CGFloat(led.labelWords.count - 1) * lineSpacing

        // Start position for first line (top of centered text block within containerHeight)
        let startY = (Self.containerHeight + totalTextHeight) / 2 - lineHeight

        // Create a label for each word
        for (index, word) in led.labelWords.enumerated() {
            let wordLabel = NSTextField(frame: NSRect(
                x: wordOffsets[index],
                y: startY - CGFloat(index) * (lineHeight + lineSpacing),
                width: labelWidth,
                height: lineHeight
            ))
            wordLabel.stringValue = word
            wordLabel.font = font
            wordLabel.textColor = NSColor.white
            wordLabel.isBezeled = false
            wordLabel.drawsBackground = false
            wordLabel.isEditable = false
            wordLabel.isSelectable = false
            wordLabel.alignment = .left
            labelView.addSubview(wordLabel)
        }

        return labelView
    }
}

// MARK: - LED Area View

// BeebiumIndicatorPanelView: Specialized view for LED area with encapsulated LED setup
@MainActor
class BeebiumIndicatorPanelView: BeebiumDiagnosticView {

    // Store references to LED views for state updates
    private var ledViews: [NSImageView] = []

    // LED images loaded once at initialization
    private let onImage: NSImage
    private let offImage: NSImage

    // LED definitions for updating state
    private let leds: [BeebiumTouchBarLED]

    // Indicator client for getting LED states
    weak var indicatorClient: IndicatorClient?

    // Timer for periodic LED state updates
    private var updateTimer: Timer?

    // LED-specific initializer that encapsulates all setup
    init(leds: [BeebiumTouchBarLED], indicatorClient: IndicatorClient?) {
        self.leds = leds
        self.indicatorClient = indicatorClient

        // Load LED images once at initialization
        self.onImage = NSImage(named: "LedOn") ?? NSImage()
        self.offImage = NSImage(named: "LedOff") ?? NSImage()

        super.init(
            name: "LEDArea",
            minimumWidth: 0,
            contentHuggingPriority: 1000,
            compressionResistancePriority: 500
        )

        // Layout constants (local scope)
        let ledWidth: CGFloat = 28                  // The logical width at which the LED images will be rendered
        let nominalPadding: CGFloat = 2             // Padding between adjacent LED/label groups
        let font: NSFont = NSFont.systemFont(ofSize: 8) // The label font

        // First pass: measure all label widths. This is necessary so we can space the LEDs
        // equally, but as close together as possible; we need to know the widest label between the LEDs
        var allLabelWidths: [CGFloat] = []
        for led in leds {
            let width = led.measureLabelWidth(font: font)
            allLabelWidths.append(width)
        }

        // Calculate max label width of the labels which lie _between_ the LEDs
        // which excludes the first label
        let maxLabelWidth = allLabelWidths.dropFirst().max() ?? 0

        // Second pass: calculate all positioning information and create LED layout arrays
        var currentX: CGFloat = 0
        var ledLayouts: [BeebiumLEDLayout] = []

        for (index, led) in leds.enumerated() {
            // Layout algorithm: position accumulation
            let labelWidth = led.measureLabelWidth(font: font)
            let ledPadding = index == 0 ? 0 : maxLabelWidth + nominalPadding - labelWidth
            let ledUnitWidth = ledPadding + labelWidth + led.labelLEDSpacing + ledWidth

            let labelX = currentX + ledPadding
            let ledX = labelX + labelWidth + led.labelLEDSpacing

            // LEDLayout constructor: LED-specific computation + storage
            let layout = BeebiumLEDLayout(led: led, font: font, labelX: labelX, ledX: ledX)
            ledLayouts.append(layout)

            currentX += ledUnitWidth
        }

        let fixedWidth = ceil(currentX)

        translatesAutoresizingMaskIntoConstraints = false
        widthAnchor.constraint(equalToConstant: fixedWidth).isActive = true
        frame = NSRect(x: 0, y: 0, width: fixedWidth, height: 30)
        wantsLayer = true

        // LED panel content creation
        // Initialize LED views array with correct size
        ledViews = Array(repeating: NSImageView(), count: leds.count)

        for (index, ledLayout) in ledLayouts.enumerated() {
            // Get initial brightness
            let brightness = indicatorClient?.values[leds[index].indicatorName] ?? 0

            // Create LED image view using LEDLayout method
            let ledImageView = ledLayout.createLEDImageView(
                ledWidth: ledWidth,
                onImage: onImage,
                offImage: offImage,
                brightness: brightness
            )
            ledViews[index] = ledImageView
            addSubview(ledImageView)

            // Create label view using LEDLayout method
            let labelView = ledLayout.createLabelView()
            addSubview(labelView)
        }

        // Start update timer for LED state changes
        startUpdates()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        // The timer was scheduled on the main run loop and must be invalidated
        // there, but deinit can run on whichever thread drops the last
        // reference, so the timer has to cross a queue boundary to get home.
        // Timer is not Sendable and never will be; nonisolated(unsafe) states
        // that this particular crossing is safe, and it is: we are the last
        // owner (nothing else can touch this timer once we are deallocating)
        // and the receiving block does nothing but invalidate it.
        nonisolated(unsafe) let timer = updateTimer
        DispatchQueue.main.async {
            timer?.invalidate()
        }
    }

    // MARK: - LED State Management

    /// Start periodic LED state updates
    func startUpdates() {
        updateTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            // The timer is scheduled on the main run loop, so this already
            // fires on the main thread. Hopping to it again only delayed each
            // LED update by a turn -- and re-captured the weak self inside a
            // second escaping closure, which is what the compiler objected to.
            MainActor.assumeIsolated {
                self?.updateLEDStates()
            }
        }
    }

    /// Stop periodic LED state updates
    func stopUpdates() {
        updateTimer?.invalidate()
        updateTimer = nil
    }

    /// Update all LED states based on current indicator values
    func updateLEDStates() {
        guard let indicatorClient = indicatorClient else { return }

        // Update each TouchBar LED based on current state
        for (index, led) in leds.enumerated() {
            guard index < ledViews.count else { continue }
            let brightness = led.brightness(indicatorClient: indicatorClient)

            // Create blended image for current brightness level
            ledViews[index].image = Self.createBlendedLEDImage(
                brightness: brightness,
                onImage: onImage,
                offImage: offImage
            )
        }
    }

    /// Create a blended LED image based on brightness level
    /// - Parameters:
    ///   - brightness: LED brightness (0-255)
    ///   - onImage: The "on" LED image
    ///   - offImage: The "off" LED image
    /// - Returns: A blended image representing the LED state
    nonisolated static func createBlendedLEDImage(brightness: UInt32, onImage: NSImage, offImage: NSImage) -> NSImage {
        let size = offImage.size
        return NSImage(size: size, flipped: false) { rect in
            // Base layer: "off" image at full opacity
            offImage.draw(in: rect)

            // Top layer: "on" image with alpha proportional to brightness
            let alpha = CGFloat(brightness) / 255.0
            onImage.draw(in: rect, from: .zero, operation: .sourceOver, fraction: alpha)

            return true
        }
    }
}

// MARK: - BeebiumTouchBarLEDs Class

@available(macOS 10.12.2, *)
@MainActor
class BeebiumTouchBarLEDs: BeebiumTouchBarComponent {

    // MARK: - Properties

    // TouchBar identifier for this component
    private let identifier: NSTouchBarItem.Identifier

    // Indicator client for LED states
    private weak var indicatorClient: IndicatorClient?

    // Reference to the LED panel view for state updates
    private weak var indicatorPanelView: BeebiumIndicatorPanelView?

    // TouchBar LED definitions - CAPS LOCK and SHIFT LOCK only (no Cassette Motor)
    internal static let touchBarLEDs: [BeebiumTouchBarLED] = [
        BeebiumTouchBarLED(
            label: "CAPS LOCK",
            indicatorName: "caps-lock-led",
            labelLEDSpacing: -8
        ),
        BeebiumTouchBarLED(
            label: "SHIFT LOCK",
            indicatorName: "shift-lock-led",
            labelLEDSpacing: -8
        )
    ]

    // MARK: - Initialization

    init(identifier: NSTouchBarItem.Identifier, indicatorClient: IndicatorClient?) {
        self.identifier = identifier
        self.indicatorClient = indicatorClient
    }

    // MARK: - Public Interface

    /// Update all LED states based on current BBC Micro state
    func updateLEDStates() {
        indicatorPanelView?.updateLEDStates()
    }

    /// Create the LED group TouchBar item
    func createLEDGroupItem() -> NSCustomTouchBarItem {
        // LED Area Requirements (Optional Group):
        // - Dynamic width based on layout algorithm result
        // - Low visibility priority (dropped when Control Strip enabled)
        // - High content hugging to prevent expansion
        let item = NSCustomTouchBarItem(identifier: identifier)
        item.visibilityPriority = .low  // Optional group - dropped when Control Strip enabled

        // Create and store reference to indicator panel view
        let panelView = BeebiumIndicatorPanelView(leds: Self.touchBarLEDs, indicatorClient: indicatorClient)
        indicatorPanelView = panelView

        item.view = panelView
        item.customizationLabel = "BBC Micro Status LEDs"
        return item
    }

    // MARK: - BeebiumTouchBarComponent Protocol Implementation

    var componentName: String { "LEDs" }

    func createTouchBarItem() -> NSCustomTouchBarItem {
        return createLEDGroupItem()
    }

    func updateState() {
        updateLEDStates()
    }
}
