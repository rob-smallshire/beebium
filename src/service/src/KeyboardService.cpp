#include "beebium/service/KeyboardService.hpp"
#include "beebium/SystemViaPeripheral.hpp"

namespace beebium::service {

KeyboardServiceImpl::KeyboardServiceImpl(SystemViaPeripheral& keyboard)
    : keyboard_(keyboard) {
}

KeyboardServiceImpl::~KeyboardServiceImpl() = default;

grpc::Status KeyboardServiceImpl::KeyDown(
    grpc::ServerContext* /*context*/,
    const KeyRequest* request,
    KeyResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t row = request->row();
    uint32_t column = request->column();

    if (row >= 10 || column >= 10) {
        response->set_accepted(false);
        return grpc::Status::OK;
    }

    keyboard_.key_down(static_cast<uint8_t>(row), static_cast<uint8_t>(column));
    response->set_accepted(true);

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::KeyUp(
    grpc::ServerContext* /*context*/,
    const KeyRequest* request,
    KeyResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t row = request->row();
    uint32_t column = request->column();

    if (row >= 10 || column >= 10) {
        response->set_accepted(false);
        return grpc::Status::OK;
    }

    keyboard_.key_up(static_cast<uint8_t>(row), static_cast<uint8_t>(column));
    response->set_accepted(true);

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::TypeText(
    grpc::ServerContext* /*context*/,
    const TypeTextRequest* /*request*/,
    TypeTextResponse* response) {

    // TODO: Implement text typing with timing
    response->set_started(false);
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "TypeText not yet implemented");
}

grpc::Status KeyboardServiceImpl::GetState(
    grpc::ServerContext* /*context*/,
    const GetStateRequest* /*request*/,
    KeyboardState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Get keyboard matrix state from peripheral
    for (int row = 0; row < 10; ++row) {
        response->add_pressed_rows(keyboard_.get_row_state(row));
    }

    return grpc::Status::OK;
}

} // namespace beebium::service
