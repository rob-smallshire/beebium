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

import AppKit
import Foundation
import GRPC
import SwiftUI

/// Metadata for a single indicator, parsed from server response
struct IndicatorMetadata {
    let name: String
    let label: String
    let color: Color
    let relatedKey: String?  // BBC key name this indicator relates to (e.g., "Caps Lock", "Shift Lock")
}

/// Client for streaming indicator states from beebium-server via gRPC
@MainActor
final class IndicatorClient: ObservableObject, Disconnectable {
    /// Current indicator values (name -> 0-255)
    @Published private(set) var values: [String: UInt32] = [:]

    /// Indicator metadata keyed by name
    @Published private(set) var metadata: [String: IndicatorMetadata] = [:]

    /// Whether metadata has been loaded
    @Published private(set) var isLoaded: Bool = false

    /// Error message if connection or streaming failed
    @Published private(set) var errorMessage: String?

    /// Bumped whenever a lock LED (`caps-lock-led` / `shift-lock-led`) value
    /// changes, including the first time each arrives. It is the race-free push
    /// signal for the app's lock mirror (issue #73): the LEDs change exactly when
    /// the MOS re-inits the locks on reset, so an observer re-reads the exact
    /// latch (`GetLockState`) and adopts it. Observable so ContentView can react.
    @Published private(set) var lockLedChangeToken: Int = 0

    /// Last-seen lock-LED brightnesses, to detect a change. The LED is a
    /// duty-cycle-filtered projection of the latch, so we use it only as a
    /// "something changed" trigger and read the exact value separately.
    private var lastCapsLockLed: UInt32?
    private var lastShiftLockLed: UInt32?

    private var client: Beebium_IndicatorServiceNIOClient?
    private var subscriptionTask: Task<Void, Never>?

    /// Connect to the server using an existing gRPC channel
    func connect(channel: GRPCChannel) {
        client = Beebium_IndicatorServiceNIOClient(channel: channel)

        subscriptionTask = Task { [weak self] in
            await self?.fetchMetadataAndSubscribe()
        }
    }

    /// Disconnect from the server
    func disconnect() {
        subscriptionTask?.cancel()
        subscriptionTask = nil
        client = nil
        isLoaded = false
        values = [:]
        metadata = [:]
        errorMessage = nil
        // Re-arm the lock-LED change detection so a reconnect's first values are
        // seen as changes and re-adopted. The token stays monotonic.
        lastCapsLockLed = nil
        lastShiftLockLed = nil
    }

    private func fetchMetadataAndSubscribe() async {
        guard let client = client else { return }

        // First, fetch indicator metadata
        do {
            let request = Beebium_ListIndicatorsRequest()
            let response = try await client.listIndicators(request).response.get()

            var newMetadata: [String: IndicatorMetadata] = [:]
            for info in response.indicators {
                let label = info.metadata["label"] ?? info.name
                let colorString = info.metadata["color"] ?? ""
                let color = colorFromMetadata(colorString)
                let relatedKeyString = info.metadata["related_key"] ?? ""
                let relatedKey = relatedKeyString.isEmpty ? nil : relatedKeyString

                newMetadata[info.name] = IndicatorMetadata(
                    name: info.name,
                    label: label,
                    color: color,
                    relatedKey: relatedKey
                )
            }

            await MainActor.run {
                self.metadata = newMetadata
                self.isLoaded = true
                self.errorMessage = nil
            }
        } catch {
            await MainActor.run {
                self.errorMessage = "Failed to list indicators: \(error.localizedDescription)"
                self.isLoaded = false
            }
            return
        }

        // Then subscribe to streaming updates
        var request = Beebium_SubscribeIndicatorsRequest()
        request.minIntervalMs = 50  // ~20Hz update rate

        let call = client.subscribe(request) { [weak self] update in
            Task { @MainActor [weak self] in
                self?.handleUpdate(update)
            }
        }

        // Wait for stream to complete or be cancelled
        do {
            _ = try await call.status.get()
        } catch {
            await MainActor.run {
                if self.isLoaded {
                    self.errorMessage = "Indicator stream ended: \(error.localizedDescription)"
                }
            }
        }
    }

    private func handleUpdate(_ update: Beebium_IndicatorUpdate) {
        // Merge updated values into our state
        for (name, value) in update.values {
            values[name] = value
        }

        // Signal a lock-LED change so the app re-reads the exact latch and
        // updates its authoritative mirror (issue #73). This fires the first
        // time each LED arrives (machine booted far enough to own the locks) and
        // whenever the MOS re-inits them on reset -- a race-free trigger, since
        // the LEDs move exactly when the latch does.
        var lockChanged = false
        if let caps = update.values["caps-lock-led"], caps != lastCapsLockLed {
            lastCapsLockLed = caps
            lockChanged = true
        }
        if let shift = update.values["shift-lock-led"], shift != lastShiftLockLed {
            lastShiftLockLed = shift
            lockChanged = true
        }
        if lockChanged {
            lockLedChangeToken &+= 1
        }
    }

    // MARK: - Color Parsing

    /// Parse color from various formats supported in indicator metadata
    /// Formats: "#rrggbb", "rgb(r g b)", "hsl(h s% l%)", "NNNnm" (wavelength)
    private func colorFromMetadata(_ colorString: String) -> Color {
        let trimmed = colorString.trimmingCharacters(in: .whitespaces)

        // CSS hex format: "#00a400" or "#0a0"
        if trimmed.hasPrefix("#") {
            return parseHexColor(trimmed)
        }

        // CSS rgb format: "rgb(214 122 127)" or "rgb(214, 122, 127)"
        if trimmed.hasPrefix("rgb(") {
            return parseRGBColor(trimmed)
        }

        // CSS hsl format: "hsl(50 80% 40%)" or "hsl(50, 80%, 40%)"
        if trimmed.hasPrefix("hsl(") {
            return parseHSLColor(trimmed)
        }

        // Wavelength format: "625nm"
        if trimmed.hasSuffix("nm") {
            return parseWavelengthColor(trimmed)
        }

        return .gray
    }

    /// Parse CSS hex color: "#rrggbb" or "#rgb"
    private func parseHexColor(_ hex: String) -> Color {
        var hexString = hex.trimmingCharacters(in: .whitespaces)
        if hexString.hasPrefix("#") {
            hexString.removeFirst()
        }

        // Expand short form (#rgb -> #rrggbb)
        if hexString.count == 3 {
            hexString = hexString.map { "\($0)\($0)" }.joined()
        }

        guard hexString.count == 6,
              let value = UInt64(hexString, radix: 16) else {
            return .gray
        }

        let r = Double((value >> 16) & 0xFF) / 255.0
        let g = Double((value >> 8) & 0xFF) / 255.0
        let b = Double(value & 0xFF) / 255.0

        return Color(red: r, green: g, blue: b)
    }

    /// Parse CSS rgb color: "rgb(214 122 127)" or "rgb(214, 122, 127)"
    private func parseRGBColor(_ rgb: String) -> Color {
        // Extract content between parentheses
        guard let start = rgb.firstIndex(of: "("),
              let end = rgb.lastIndex(of: ")") else {
            return .gray
        }

        let content = rgb[rgb.index(after: start)..<end]
        // Split by comma or whitespace
        let components = content
            .replacingOccurrences(of: ",", with: " ")
            .split(separator: " ")
            .compactMap { Double($0.trimmingCharacters(in: .whitespaces)) }

        guard components.count >= 3 else { return .gray }

        return Color(
            red: components[0] / 255.0,
            green: components[1] / 255.0,
            blue: components[2] / 255.0
        )
    }

    /// Parse CSS hsl color: "hsl(50 80% 40%)" or "hsl(50, 80%, 40%)"
    private func parseHSLColor(_ hsl: String) -> Color {
        // Extract content between parentheses
        guard let start = hsl.firstIndex(of: "("),
              let end = hsl.lastIndex(of: ")") else {
            return .gray
        }

        let content = hsl[hsl.index(after: start)..<end]
        // Split by comma or whitespace, remove % signs
        let components = content
            .replacingOccurrences(of: ",", with: " ")
            .replacingOccurrences(of: "%", with: "")
            .split(separator: " ")
            .compactMap { Double($0.trimmingCharacters(in: .whitespaces)) }

        guard components.count >= 3 else { return .gray }

        let h = components[0] / 360.0  // Hue: 0-360 -> 0-1
        let s = components[1] / 100.0  // Saturation: 0-100% -> 0-1
        let l = components[2] / 100.0  // Lightness: 0-100% -> 0-1

        return Color(hue: h, saturation: s, brightness: hslLightnessToBrightness(s: s, l: l))
    }

    /// Convert HSL lightness to HSB brightness
    /// HSL and HSB use different models; this approximates the conversion
    private func hslLightnessToBrightness(s: Double, l: Double) -> Double {
        // When lightness is 0.5, brightness equals saturation relationship
        // For simplicity, use an approximation: B = L + S * min(L, 1-L)
        let b = l + s * min(l, 1 - l)
        return b
    }

    /// Parse wavelength color: "625nm"
    /// Converts wavelength to RGB, then normalizes perceptual brightness using LAB color space
    /// so all indicator colors appear equally bright regardless of hue.
    private func parseWavelengthColor(_ wavelength: String) -> Color {
        guard let value = Double(wavelength.replacingOccurrences(of: "nm", with: "")) else {
            return .gray
        }

        let (r, g, b) = wavelengthToRGB(value)
        let targetL = targetLightnessForWavelength(value)
        return normalizePerceptualBrightness(r: r, g: g, b: b, targetLightness: targetL)
    }

    /// Compute target LAB lightness for a wavelength, compensating for Helmholtz-Kohlrausch effect.
    ///
    /// The Helmholtz-Kohlrausch (H-K) effect causes saturated colors to appear brighter than
    /// their measured luminance would suggest. This effect varies by hue: red and blue show
    /// a strong H-K effect (appearing brighter), while green/yellow show a weaker effect.
    ///
    /// To achieve perceptually uniform brightness across different LED colors, we apply a
    /// compensation curve that adjusts the target LAB lightness based on wavelength:
    /// - Colors with strong H-K effect (red, blue) use lower L values
    /// - Colors with weak H-K effect (green, yellow, amber) use higher L values
    ///
    /// The control points were tuned by visual comparison of indicator LEDs at different
    /// wavelengths displayed simultaneously on screen.
    private func targetLightnessForWavelength(_ lambda: Double) -> CGFloat {
        // Control points tuned by visual inspection to achieve perceptually equal brightness.
        // Linear interpolation between points for intermediate wavelengths.
        struct ControlPoint {
            let wavelength: Double
            let lightness: CGFloat
        }

        let controlPoints: [ControlPoint] = [
            ControlPoint(wavelength: 380, lightness: 60),  // violet (strong H-K effect)
            ControlPoint(wavelength: 480, lightness: 65),  // blue (strong H-K effect)
            ControlPoint(wavelength: 520, lightness: 75),  // cyan-green (weak H-K effect)
            ControlPoint(wavelength: 568, lightness: 75),  // green (weak H-K effect)
            ControlPoint(wavelength: 590, lightness: 80),  // amber (weak H-K effect)
            ControlPoint(wavelength: 625, lightness: 65),  // red (strong H-K effect)
            ControlPoint(wavelength: 780, lightness: 60),  // deep red (strong H-K effect)
        ]

        // Find surrounding control points and interpolate
        for i in 0..<(controlPoints.count - 1) {
            let p1 = controlPoints[i]
            let p2 = controlPoints[i + 1]
            if lambda >= p1.wavelength && lambda <= p2.wavelength {
                let t = (lambda - p1.wavelength) / (p2.wavelength - p1.wavelength)
                return p1.lightness + CGFloat(t) * (p2.lightness - p1.lightness)
            }
        }

        // Outside range - use nearest endpoint
        if lambda < controlPoints.first!.wavelength {
            return controlPoints.first!.lightness
        }
        return controlPoints.last!.lightness
    }

    /// Normalize perceptual brightness using CIE LAB color space.
    ///
    /// LAB separates lightness (L) from chromaticity (a, b), allowing us to adjust
    /// perceived brightness while preserving the hue and saturation of the color.
    /// This is used to apply Helmholtz-Kohlrausch compensation: we set L to the
    /// target value from the H-K compensation curve while keeping a and b unchanged.
    ///
    /// - Parameters:
    ///   - r, g, b: RGB components (0.0-1.0)
    ///   - targetLightness: Target L value in LAB space (0-100), from H-K compensation curve
    /// - Returns: Color with adjusted perceptual brightness
    private func normalizePerceptualBrightness(r: Double, g: Double, b: Double, targetLightness: CGFloat) -> Color {
        // Access LAB color space via Core Graphics (NSColorSpace doesn't expose it directly)
        guard let cgLabSpace = CGColorSpace(name: CGColorSpace.genericLab),
              let nsLabSpace = NSColorSpace(cgColorSpace: cgLabSpace) else {
            return Color(red: r, green: g, blue: b)
        }

        // Create NSColor in sRGB space
        let nsColor = NSColor(srgbRed: r, green: g, blue: b, alpha: 1.0)

        // Convert to LAB color space
        guard let labColor = nsColor.usingColorSpace(nsLabSpace) else {
            return Color(red: r, green: g, blue: b)
        }

        // Get LAB components (preserving a and b chromaticity)
        var components = [CGFloat](repeating: 0, count: 4)
        labColor.getComponents(&components)
        let a = components[1]
        let bComponent = components[2]

        // Create new color with target lightness, preserving a and b (chromaticity)
        let normalizedLab = NSColor(
            colorSpace: nsLabSpace,
            components: [targetLightness, a, bComponent, 1.0],
            count: 4
        )
        guard let normalizedRGB = normalizedLab.usingColorSpace(NSColorSpace.sRGB) else {
            return Color(red: r, green: g, blue: b)
        }

        return Color(nsColor: normalizedRGB)
    }

    /// Approximate visible spectrum wavelength to RGB
    /// - Parameter lambda: Wavelength in nanometers (380-780)
    /// - Returns: RGB tuple with values 0.0-1.0
    private func wavelengthToRGB(_ lambda: Double) -> (Double, Double, Double) {
        var r: Double = 0
        var g: Double = 0
        var b: Double = 0

        if lambda >= 380 && lambda < 440 {
            r = -(lambda - 440) / (440 - 380)
            g = 0
            b = 1
        } else if lambda >= 440 && lambda < 490 {
            r = 0
            g = (lambda - 440) / (490 - 440)
            b = 1
        } else if lambda >= 490 && lambda < 510 {
            r = 0
            g = 1
            b = -(lambda - 510) / (510 - 490)
        } else if lambda >= 510 && lambda < 580 {
            r = (lambda - 510) / (580 - 510)
            g = 1
            b = 0
        } else if lambda >= 580 && lambda < 645 {
            r = 1
            g = -(lambda - 645) / (645 - 580)
            b = 0
        } else if lambda >= 645 && lambda <= 780 {
            r = 1
            g = 0
            b = 0
        }
        // Outside visible range: returns (0, 0, 0) which will appear black

        return (r, g, b)
    }
}
