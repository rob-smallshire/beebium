#include "beebium/service/VideoService.hpp"
#include "beebium/FrameBuffer.hpp"

namespace beebium::service {

VideoServiceImpl::VideoServiceImpl(FrameBuffer& frame_buffer)
    : frame_buffer_(frame_buffer) {
}

VideoServiceImpl::~VideoServiceImpl() = default;

grpc::Status VideoServiceImpl::SubscribeFrames(
    grpc::ServerContext* context,
    const SubscribeFramesRequest* /*request*/,
    grpc::ServerWriter<Frame>* writer) {

    uint64_t last_version = 0;

    while (!context->IsCancelled()) {
        uint64_t current_version = frame_buffer_.version();

        if (current_version != last_version) {
            // New frame available
            Frame frame;
            frame.set_frame_number(current_version);
            frame.set_width(frame_buffer_.width());
            frame.set_height(frame_buffer_.height());

            // Copy frame data
            auto pixels = frame_buffer_.read_frame();
            frame.set_pixels(pixels.data(), pixels.size_bytes());

            if (!writer->Write(frame)) {
                // Client disconnected
                break;
            }

            last_version = current_version;
        }

        // Brief sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return grpc::Status::OK;
}

grpc::Status VideoServiceImpl::GetConfig(
    grpc::ServerContext* /*context*/,
    const GetConfigRequest* /*request*/,
    VideoConfig* response) {

    response->set_width(frame_buffer_.width());
    response->set_height(frame_buffer_.height());
    response->set_framerate_hz(50);  // PAL

    return grpc::Status::OK;
}

} // namespace beebium::service
