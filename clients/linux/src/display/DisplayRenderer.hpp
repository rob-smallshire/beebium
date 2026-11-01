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

#ifndef BEEBIUM_LINUX_DISPLAY_DISPLAY_RENDERER_HPP
#define BEEBIUM_LINUX_DISPLAY_DISPLAY_RENDERER_HPP

#include "connection/DecodedFrame.hpp"

#include <QColor>
#include <QSize>

#include <rhi/qrhi.h>

#include <memory>

namespace beebium::linux_client {

// Draws emulator frames with QRhi, on whichever backend the surface chose
// (Vulkan preferred, OpenGL ES as the fallback). It knows nothing of widgets,
// so the on-screen display and the offscreen bench share it.
//
// A frame given to it stays current until the next, so a paused machine or
// a resize redraws the last picture (client specification 6.1).
class DisplayRenderer {
public:
    DisplayRenderer() = default;
    ~DisplayRenderer();

    DisplayRenderer(const DisplayRenderer&) = delete;
    DisplayRenderer& operator=(const DisplayRenderer&) = delete;

    // Create the device resources for render targets compatible with
    // `render_pass`. Call again after release() when the QRhi changes.
    void initialize(QRhi* rhi, QRhiRenderPassDescriptor* render_pass);

    // Drop every device resource. Safe to call repeatedly.
    void release();

    // Make `frame` the current picture; it is uploaded by the next render().
    void set_frame(std::unique_ptr<DecodedFrame> frame);

    bool has_frame() const { return static_cast<bool>(current_); }
    const DecodedFrame* current_frame() const { return current_.get(); }

    void set_pixel_aspect(double pixel_aspect) { pixel_aspect_ = pixel_aspect; }
    void set_background(const QColor& colour) { background_ = colour; }

    // Record one pass into `target`: clear to the background and, once a
    // frame has arrived, draw it aspect-fitted.
    void render(QRhiCommandBuffer* command_buffer, QRhiRenderTarget* target);

private:
    void ensure_texture(QRhiResourceUpdateBatch* updates);

    QRhi* rhi_ = nullptr;
    std::unique_ptr<QRhiBuffer> uniforms_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiTexture> texture_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
    QSize texture_size_;

    std::unique_ptr<DecodedFrame> current_;
    bool upload_pending_ = false;
    double pixel_aspect_ = 0.96;
    QColor background_ = QColor(16, 16, 16);
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_DISPLAY_DISPLAY_RENDERER_HPP
