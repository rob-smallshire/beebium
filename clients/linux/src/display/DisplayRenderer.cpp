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

#include "display/DisplayRenderer.hpp"

#include "display/DisplayUniforms.hpp"

#include <QFile>

#include <rhi/qshader.h>

#include <stdexcept>

namespace beebium::linux_client {

namespace {

QShader load_shader(const QString& resource_path) {
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("cannot open shader " + resource_path.toStdString());
    }
    QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid()) {
        throw std::runtime_error("invalid shader " + resource_path.toStdString());
    }
    return shader;
}

}  // namespace

DisplayRenderer::~DisplayRenderer() {
    release();
}

void DisplayRenderer::initialize(QRhi* rhi, QRhiRenderPassDescriptor* render_pass) {
    release();
    rhi_ = rhi;

    uniforms_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                    sizeof(DisplayUniforms)));
    uniforms_->create();

    // Nearest sampling for now: the phase 0 scaler. Filtering is a display
    // style choice for later (client specification 6.3).
    sampler_.reset(rhi_->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                    QRhiSampler::None, QRhiSampler::ClampToEdge,
                                    QRhiSampler::ClampToEdge));
    sampler_->create();

    // A placeholder texture so the bindings and pipeline exist before the
    // first frame; replaced at the first frame's size.
    texture_.reset(rhi_->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    texture_->create();
    texture_size_ = QSize(1, 1);

    bindings_.reset(rhi_->newShaderResourceBindings());
    bindings_->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            uniforms_.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, texture_.get(), sampler_.get()),
    });
    bindings_->create();

    pipeline_.reset(rhi_->newGraphicsPipeline());
    pipeline_->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pipeline_->setShaderStages({
        {QRhiShaderStage::Vertex, load_shader(QStringLiteral(":/shaders/display.vert.qsb"))},
        {QRhiShaderStage::Fragment, load_shader(QStringLiteral(":/shaders/display.frag.qsb"))},
    });
    pipeline_->setVertexInputLayout({});
    pipeline_->setShaderResourceBindings(bindings_.get());
    pipeline_->setRenderPassDescriptor(render_pass);
    pipeline_->create();

    // A frame kept across a device change is uploaded again.
    upload_pending_ = static_cast<bool>(current_);
}

void DisplayRenderer::release() {
    pipeline_.reset();
    bindings_.reset();
    texture_.reset();
    sampler_.reset();
    uniforms_.reset();
    texture_size_ = QSize();
    rhi_ = nullptr;
}

void DisplayRenderer::set_frame(std::unique_ptr<DecodedFrame> frame) {
    if (!frame) {
        return;
    }
    current_ = std::move(frame);
    upload_pending_ = true;
}

void DisplayRenderer::ensure_texture(QRhiResourceUpdateBatch* updates) {
    const QSize size(static_cast<int>(current_->width), static_cast<int>(current_->height));
    if (size != texture_size_) {
        // Only a mode change alters the size; the bindings name the texture
        // object, which is rebuilt in place, so they are refreshed too.
        texture_->setPixelSize(size);
        texture_->create();
        bindings_->create();
        texture_size_ = size;
    }
    QRhiTextureSubresourceUploadDescription upload(current_->pixels.data(),
                                                   static_cast<quint32>(current_->pixels.size()));
    upload.setDataStride(current_->width * 4);
    updates->uploadTexture(texture_.get(), QRhiTextureUploadDescription({0, 0, upload}));
}

void DisplayRenderer::render(QRhiCommandBuffer* command_buffer, QRhiRenderTarget* target) {
    const QSize output_size = target->pixelSize();
    QRhiResourceUpdateBatch* updates = rhi_->nextResourceUpdateBatch();

    if (current_) {
        if (upload_pending_) {
            ensure_texture(updates);
            upload_pending_ = false;
        }
        const DisplayUniforms uniforms =
            make_display_uniforms(*current_, pixel_aspect_, output_size.width(),
                                  output_size.height(), rhi_->isYUpInNDC());
        updates->updateDynamicBuffer(uniforms_.get(), 0, sizeof(uniforms), &uniforms);
    }

    command_buffer->beginPass(target, background_, {1.0f, 0}, updates);
    if (current_) {
        command_buffer->setGraphicsPipeline(pipeline_.get());
        command_buffer->setViewport(
            QRhiViewport(0, 0, float(output_size.width()), float(output_size.height())));
        command_buffer->setShaderResources();
        command_buffer->draw(4);
    }
    command_buffer->endPass();
}

}  // namespace beebium::linux_client
