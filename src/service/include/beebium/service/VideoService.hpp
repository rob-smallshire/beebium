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
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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

    grpc::Status HoldScreen(
        grpc::ServerContext* context,
        const HoldScreenRequest* request,
        ScreenHold* response) override;

    grpc::Status ReleaseScreen(
        grpc::ServerContext* context,
        const ReleaseScreenRequest* request,
        ReleaseScreenResponse* response) override;

private:
    // Everything a screen-text reading depends on, captured at one instant:
    // the pixels, the frame metadata the bands come from, the teletext grid,
    // and the glyph sets read from the machine. Defined in the .cpp -- nothing
    // outside needs its shape, and forward-declaring it keeps the core's
    // headers out of this one.
    struct ScreenCapture;

    // The live screen, captured. Taken whole so the four parts describe one
    // frame rather than four moments.
    std::shared_ptr<const ScreenCapture> capture_screen() const;

    // The screen held under an id, or null when it is unknown or expired.
    std::shared_ptr<const ScreenCapture> held_screen(uint64_t hold_id) const;

    // How long a hold survives without being released. A hold is a
    // user-scale thing -- the life of a drag -- so this only has to outlast
    // someone thinking, and exists so a client that dies does not leak one.
    static constexpr std::chrono::minutes HOLD_LIFETIME{5};

    // A ceiling on holds, so a client that takes them and never releases
    // cannot grow the server without bound. Each is a frame's worth of pixels.
    static constexpr size_t MAX_HOLDS = 8;

    FrameBuffer& frame_buffer_;
    TeletextGrid& teletext_grid_;
    PeekByte peek_byte_;

    mutable std::mutex holds_mutex_;
    std::map<uint64_t, std::shared_ptr<const ScreenCapture>> holds_;
    uint64_t next_hold_id_ = 1;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_VIDEO_SERVICE_HPP
