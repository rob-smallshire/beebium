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
#include <thread>

namespace beebium {

class FrameBuffer;

namespace service {

/// gRPC service implementation for video frame streaming
class VideoServiceImpl final : public VideoService::Service {
public:
    explicit VideoServiceImpl(FrameBuffer& frame_buffer);
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

private:
    FrameBuffer& frame_buffer_;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_VIDEO_SERVICE_HPP
