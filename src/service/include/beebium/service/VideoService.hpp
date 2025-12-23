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
