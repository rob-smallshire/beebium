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

import SwiftUI

/// Emulation-speed control: a log2 slider for the configured multiplier with a
/// read-only achieved-speed marker sharing the same axis, an Unlimited toggle,
/// and a 1x reset.
struct SpeedControlView: View {
    @ObservedObject var model: SpeedControlModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Emulation Speed").font(.headline)

            SpeedScale(model: model)
                .frame(height: 72)

            Toggle("Unlimited", isOn: Binding(
                get: { model.isUnlimited },
                set: { model.setUnlimited($0) }
            ))
            .toggleStyle(.switch)
            .help("Run the emulator as fast as the host allows")
        }
    }
}

/// The shared-scale axis: a draggable thumb for the configured multiplier and a
/// downward caret for the achieved multiplier, both positioned by one log2
/// mapping over one track width so the two scales coincide exactly.
private struct SpeedScale: View {
    @ObservedObject var model: SpeedControlModel

    private let thumbRadius: CGFloat = 9

    var body: some View {
        GeometryReader { geo in
            axis(in: geo.size)
        }
    }

    private func xFor(_ m: Double, inset: CGFloat, usable: CGFloat, n: Double) -> CGFloat {
        let clamped = min(max(m, model.scaleMin), model.scaleMax)
        let frac = (log2(clamped) + n) / (2 * n)
        return inset + CGFloat(frac) * usable
    }

    private func multiplier(atX px: CGFloat, inset: CGFloat, usable: CGFloat, n: Double) -> Double {
        let frac = min(max(Double((px - inset) / usable), 0), 1)
        return pow(2.0, frac * 2 * n - n)
    }

    private func axis(in size: CGSize) -> some View {
        let n = Double(model.scaleExponent)
        let inset = thumbRadius
        let usable = max(size.width - 2 * inset, 1)
        let axisY = size.height * 0.6
        // Seven labels: three tick steps each side of 1x. scaleExponent is
        // 3 * tickStep, so stepping by tickStep yields -3k,-2k,-k,0,k,2k,3k.
        let exponents = Array(stride(from: -model.scaleExponent,
                                     through: model.scaleExponent,
                                     by: model.tickStep))

        return ZStack(alignment: .topLeading) {
            // Track
            Capsule()
                .fill(Color.secondary.opacity(0.25))
                .frame(width: usable, height: 4)
                .position(x: inset + usable / 2, y: axisY)

            // Power-of-two tick marks
            ForEach(exponents, id: \.self) { e in
                Rectangle()
                    .fill(Color.secondary.opacity(0.5))
                    .frame(width: 1, height: 8)
                    .position(x: xFor(pow(2.0, Double(e)), inset: inset, usable: usable, n: n),
                              y: axisY)
            }

            // Achieved-speed marker (read-only caret above the axis), a linear
            // glide matched to the server's publish cadence (no ease pulsing).
            DownCaret()
                .fill(Color.accentColor)
                .frame(width: 11, height: 8)
                .position(x: xFor(model.achievedMultiplier, inset: inset, usable: usable, n: n),
                          y: axisY - 16)
                .animation(.linear(duration: 0.5), value: model.achievedMultiplier)
                .animation(.easeInOut(duration: 0.25), value: model.scaleExponent)

            // Configured-speed thumb. Dimmed while Unlimited overrides it; the
            // scale stays live, so dragging it (or tapping a label) leaves
            // Unlimited and sets that finite speed.
            Circle()
                .fill(Color.accentColor)
                .frame(width: thumbRadius * 2, height: thumbRadius * 2)
                .shadow(radius: 1)
                .opacity(model.isUnlimited ? 0.4 : 1.0)
                .position(x: xFor(model.configuredMultiplier, inset: inset, usable: usable, n: n),
                          y: axisY)

            // Continuous drag/click over the slot, confined to the upper band so
            // it doesn't steal taps from the labels below.
            Rectangle()
                .fill(Color.clear)
                .frame(width: size.width, height: axisY + 6)
                .contentShape(Rectangle())
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            model.configuredMultiplier = multiplier(
                                atX: value.location.x, inset: inset, usable: usable, n: n)
                        }
                        .onEnded { value in
                            model.apply(multiplier: multiplier(
                                atX: value.location.x, inset: inset, usable: usable, n: n))
                        }
                )

            // Exact snap-to-power-of-two labels. Clicking one (e.g. "1×") sets
            // that speed precisely -- so the labels double as the reset.
            ForEach(exponents, id: \.self) { e in
                Button {
                    model.apply(multiplier: pow(2.0, Double(e)))
                } label: {
                    Text(Self.label(forExponent: e))
                        .font(.system(size: 9))
                        .foregroundColor(.secondary)
                        .fixedSize()
                        .padding(.vertical, 3)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .position(x: xFor(pow(2.0, Double(e)), inset: inset, usable: usable, n: n),
                          y: axisY + 16)
            }
        }
    }

    private static func label(forExponent e: Int) -> String {
        e >= 0 ? "\(Int(pow(2.0, Double(e))))×" : "1/\(Int(pow(2.0, Double(-e))))"
    }
}

private struct DownCaret: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.closeSubpath()
        return path
    }
}
