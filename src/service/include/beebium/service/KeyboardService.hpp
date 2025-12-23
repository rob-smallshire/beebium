#ifndef BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP
#define BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP

#include "keyboard.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <mutex>

namespace beebium {

class SystemViaPeripheral;

namespace service {

/// gRPC service implementation for keyboard input
class KeyboardServiceImpl final : public KeyboardService::Service {
public:
    explicit KeyboardServiceImpl(SystemViaPeripheral& keyboard);
    ~KeyboardServiceImpl() override;

    // Non-copyable
    KeyboardServiceImpl(const KeyboardServiceImpl&) = delete;
    KeyboardServiceImpl& operator=(const KeyboardServiceImpl&) = delete;

    grpc::Status KeyDown(
        grpc::ServerContext* context,
        const KeyRequest* request,
        KeyResponse* response) override;

    grpc::Status KeyUp(
        grpc::ServerContext* context,
        const KeyRequest* request,
        KeyResponse* response) override;

    grpc::Status TypeText(
        grpc::ServerContext* context,
        const TypeTextRequest* request,
        TypeTextResponse* response) override;

    grpc::Status GetState(
        grpc::ServerContext* context,
        const GetStateRequest* request,
        KeyboardState* response) override;

private:
    SystemViaPeripheral& keyboard_;
    std::mutex mutex_;
};

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_KEYBOARD_SERVICE_HPP
