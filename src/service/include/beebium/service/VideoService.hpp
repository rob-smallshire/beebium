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

#ifndef BEEBIUM_SERVICE_VIDEO_SERVICE_HPP
#define BEEBIUM_SERVICE_VIDEO_SERVICE_HPP

#include "video.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace beebium {

class FrameBuffer;
class TeletextGrid;

namespace service {

/// gRPC service implementation for video frame streaming
class VideoServiceImpl final : public VideoService::Service {
public:
    // Reads a guest memory byte without side effects, the way the debugger
    // reaches memory. The bitmap screen-text strategy uses it to read the soft
    // font out of the VDU driver's workspace, read-only and only at
    // GetScreenText time. Empty when no machine is attached, in which case the
    // ROM base font stands alone.
    using PeekByte = std::function<uint8_t(uint16_t)>;

    VideoServiceImpl(FrameBuffer& frame_buffer, TeletextGrid& teletext_grid,
                     PeekByte peek_byte = {});
    ~VideoServiceImpl() override;

    // Non-copyable
    VideoServiceImpl(const VideoServiceImpl&) = delete;
    VideoServiceImpl& operator=(const VideoServiceImpl&) = delete;

    grpc::Status SubscribeFrames(
        grpc::ServerContext* context,
        const SubscribeFramesRequest* request,
        grpc::ServerWriter<Frame>* writer) override;

    grpc::Status GetConfig(
        grpc::ServerContext* context,
        const GetConfigRequest* request,
        VideoConfig* response) override;

    grpc::Status GetTeletextScreen(
        grpc::ServerContext* context,
        const GetTeletextScreenRequest* request,
        TeletextScreen* response) override;

    grpc::Status GetScreenText(
        grpc::ServerContext* context,
        const GetScreenTextRequest* request,
        ScreenText* response) override;

    grpc::Status GetScreenGeometry(
        grpc::ServerContext* context,
        const GetScreenGeometryRequest* request,
        ScreenGeometry* response) override;

private:
    FrameBuffer& frame_buffer_;
    TeletextGrid& teletext_grid_;
    PeekByte peek_byte_;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_VIDEO_SERVICE_HPP
