// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

import Foundation
import Metal
import MetalKit
import simd

/// Renders emulator video frames using Metal. Delegates per-style rendering
/// decisions (which shader, which uniforms) to the active `DisplayStyle`.
final class MetalRenderer: NSObject {
    private let device: MTLDevice
    private let commandQueue: MTLCommandQueue
    private let pixelFormat: MTLPixelFormat = .bgra8Unorm

    /// Active display style. Use `setActiveStyle(_:)` to swap; pipelines are cached by id.
    private var activeStyle: any DisplayStyle
    private var pipelineCache: [String: MTLRenderPipelineState] = [:]

    private var frameTexture: MTLTexture?
    private var textureWidth: Int = 0
    private var textureHeight: Int = 0
    private var drawableSize: CGSize = .zero

    /// The view presenting this renderer's output, held weakly so a frame
    /// arrival can mark it for display. The view is paused (no free-running
    /// display link), so presentation is driven by the ~50 Hz frame stream
    /// rather than a CVDisplayLink -- which stalls when the display sleeps and
    /// does not restart, freezing the window while frames keep arriving.
    weak var mtkView: MTKView?

    // Display dimensions (target after scaling). BBC displays all modes at the
    // same physical size (640 wide).
    private var displayWidth: Int = 640
    private var displayHeight: Int = 256

    // Border dimensions, sourced from the gRPC frame stream.
    private var leftBorder: Int = 0
    private var rightBorder: Int = 0
    private var topBorder: Int = 0
    private var bottomBorder: Int = 0

    // Interlace state (affects aspect ratio calculation via line-doubling).
    private var isInterlaced: Bool = true  // Default to interlaced (MODE 7 boot)

    // Display regions for split-screen modes.
    private var displayRegions: [DisplayRegion] = []

    /// Pixel Aspect Ratio scale.
    /// 0.96 = Authentic (BBC PAL CRT). 1.0 = Crisp (square pixels).
    /// Mutable so the global Pixels toggle (commit D) can update it without a
    /// full renderer rebuild.
    var parScale: Float = 0.96

    /// Initialize the Metal renderer.
    /// - Parameters:
    ///   - device: Metal device to use for rendering.
    ///   - initialStyle: Display style to start with. Pipeline is built immediately;
    ///                   if pipeline creation fails, init returns nil.
    init?(device: MTLDevice, initialStyle: any DisplayStyle = StandardDisplayStyle()) {
        self.device = device

        guard let queue = device.makeCommandQueue() else {
            return nil
        }
        self.commandQueue = queue
        self.activeStyle = initialStyle

        super.init()

        do {
            try ensurePipeline(for: initialStyle)
        } catch {
            print("Failed to build pipeline for \(initialStyle.id): \(error)")
            return nil
        }
    }

    /// Switch to a different display style. Pipeline is built lazily and cached.
    /// Calling with the currently-active style is a no-op.
    func setActiveStyle(_ style: any DisplayStyle) {
        guard style.id != activeStyle.id else { return }
        do {
            try ensurePipeline(for: style)
            activeStyle = style
        } catch {
            print("Failed to build pipeline for \(style.id): \(error). Keeping previous style.")
        }
    }

    private func ensurePipeline(for style: any DisplayStyle) throws {
        if pipelineCache[style.id] == nil {
            pipelineCache[style.id] = try style.makePipelineState(
                device: device, pixelFormat: pixelFormat)
        }
    }

    /// Update the frame texture with new pixel data.
    func updateFrame(data: Data, width: Int, height: Int,
                     displayWidth: Int, displayHeight: Int,
                     leftBorder: Int, rightBorder: Int,
                     topBorder: Int, bottomBorder: Int,
                     interlaced: Bool,
                     regions: [DisplayRegion]) {
        // Store display dimensions for scaling
        self.displayWidth = displayWidth > 0 ? displayWidth : width
        self.displayHeight = displayHeight > 0 ? displayHeight : height

        // Store border dimensions
        self.leftBorder = leftBorder
        self.rightBorder = rightBorder
        self.topBorder = topBorder
        self.bottomBorder = bottomBorder

        // Store interlace state (affects aspect ratio via line-doubling)
        self.isInterlaced = interlaced

        // Store display regions
        self.displayRegions = regions

        // Create or recreate texture if dimensions changed
        if frameTexture == nil || textureWidth != width || textureHeight != height {
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .bgra8Unorm,
                width: width,
                height: height,
                mipmapped: false
            )
            descriptor.usage = [.shaderRead]
            descriptor.storageMode = .shared  // Unified memory - no CPU/GPU sync needed

            guard let texture = device.makeTexture(descriptor: descriptor) else {
                print("Failed to create texture")
                return
            }

            frameTexture = texture
            textureWidth = width
            textureHeight = height
        }

        guard let texture = frameTexture else { return }

        // Upload pixel data to texture
        data.withUnsafeBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress else { return }
            texture.replace(
                region: MTLRegion(
                    origin: MTLOrigin(x: 0, y: 0, z: 0),
                    size: MTLSize(width: width, height: height, depth: 1)
                ),
                mipmapLevel: 0,
                withBytes: baseAddress,
                bytesPerRow: width * 4
            )
        }

        // A new frame is uploaded: mark the paused view for display so draw(in:)
        // presents it. Called on the main actor (VideoClient.handleFrame is
        // @MainActor), which is where AppKit expects needsDisplay to be set.
        mtkView?.needsDisplay = true
    }
}

// MARK: - Selection geometry

extension MetalRenderer {
    /// Snapshot the geometry needed to map view points to frame pixels and
    /// back. Sourced from the active style's own `makeUniforms`, so the mapping
    /// tracks whatever the style produced (Standard's edge-margin inset, Debug's
    /// coloured borders) without this method knowing which style is active.
    ///
    /// Returns nil before the first frame has been drawn at a real drawable
    /// size, when there is nothing to map into yet.
    func captureRenderGeometry() -> ScreenRenderGeometry? {
        guard textureWidth > 0, textureHeight > 0,
              drawableSize.width > 0, drawableSize.height > 0 else { return nil }

        let frame = FrameContext(
            textureWidth: textureWidth,
            textureHeight: textureHeight,
            displayWidth: displayWidth,
            displayHeight: displayHeight,
            leftBorder: leftBorder,
            rightBorder: rightBorder,
            topBorder: topBorder,
            bottomBorder: bottomBorder,
            interlaced: isInterlaced,
            regions: displayRegions
        )
        let drawable = DrawableContext(drawableSize: drawableSize, parScale: parScale)
        let uniforms = activeStyle.makeUniforms(frame: frame, drawable: drawable)
        return ScreenRenderGeometry(uniforms: uniforms, regions: displayRegions)
    }
}

extension ScreenRenderGeometry {
    /// Extract the scalar and vector geometry from the shader uniforms. The
    /// regions arrive separately as a plain array rather than unpacked from the
    /// uniforms' fixed tuple, since the renderer already holds them that way.
    init(uniforms u: Uniforms, regions: [DisplayRegion]) {
        self.init(
            drawableSize: CGSize(width: CGFloat(u.drawableSize.x),
                                 height: CGFloat(u.drawableSize.y)),
            textureSize: CGSize(width: CGFloat(u.textureSize.x),
                                height: CGFloat(u.textureSize.y)),
            displaySize: CGSize(width: CGFloat(u.displaySize.x),
                                height: CGFloat(u.displaySize.y)),
            totalSize: CGSize(width: CGFloat(u.totalSize.x),
                              height: CGFloat(u.totalSize.y)),
            borderOffset: CGPoint(x: CGFloat(u.borderOffset.x),
                                  y: CGFloat(u.borderOffset.y)),
            parScale: Double(u.parScale),
            interlaced: u.interlaced != 0,
            edgeMargin: Double(u.edgeMargin),
            regions: regions
        )
    }
}

// MARK: - MTKViewDelegate
extension MetalRenderer: MTKViewDelegate {
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        drawableSize = size
    }

    func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let renderPassDescriptor = view.currentRenderPassDescriptor,
              let commandBuffer = commandQueue.makeCommandBuffer(),
              let renderEncoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPassDescriptor) else {
            return
        }

        if let texture = frameTexture,
           let pipelineState = pipelineCache[activeStyle.id] {
            let frame = FrameContext(
                textureWidth: textureWidth,
                textureHeight: textureHeight,
                displayWidth: displayWidth,
                displayHeight: displayHeight,
                leftBorder: leftBorder,
                rightBorder: rightBorder,
                topBorder: topBorder,
                bottomBorder: bottomBorder,
                interlaced: isInterlaced,
                regions: displayRegions
            )
            let drawableCtx = DrawableContext(
                drawableSize: view.drawableSize,
                parScale: parScale
            )

            var uniforms = activeStyle.makeUniforms(frame: frame, drawable: drawableCtx)

            renderEncoder.setRenderPipelineState(pipelineState)
            renderEncoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.size, index: 0)
            renderEncoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.size, index: 0)
            renderEncoder.setFragmentTexture(texture, index: 0)
            renderEncoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
        }

        renderEncoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
    }
}
