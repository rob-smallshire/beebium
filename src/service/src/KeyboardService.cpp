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
