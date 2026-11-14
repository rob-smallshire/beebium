// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_CONTROLLER_SERVICE_HPP
#define BEEBIUM_SERVICE_CONTROLLER_SERVICE_HPP

#include "controller.grpc.pb.h"

#include "beebium/extension/Controller.hpp"
#include "beebium/extension/ControllerRegistry.hpp"

#include <grpcpp/grpcpp.h>

#include <string>
#include <string_view>

namespace beebium::service {

// ControllerService over one machine's ControllerRegistry: lists the
// attached controllers and reads and sets their controls.
//
// A controller stores its controls in atomics, so a set here needs no pause:
// the emulation thread sees the new value at its next conversion start or
// VIA tick. SetControls validates the whole request against the
// controller's published controls before it stores anything, so a request
// with one bad value changes nothing.
class ControllerServiceImpl final : public beebium::ControllerService::Service {
public:
    explicit ControllerServiceImpl(const ControllerRegistry& registry) : registry_(registry) {}

    ControllerServiceImpl(const ControllerServiceImpl&) = delete;
    ControllerServiceImpl& operator=(const ControllerServiceImpl&) = delete;

    grpc::Status ListControllers(grpc::ServerContext*, const ListControllersRequest*,
                                 ListControllersResponse* response) override {
        for (const auto* controller : registry_.controllers()) {
            auto* description = response->add_controllers();
            description->set_controller_id(std::string(controller->controller_id()));
            description->set_display_name(std::string(controller->display_name()));
            for (const auto& control : controller->controls()) {
                auto* c = description->add_controls();
                c->set_id(control.id);
                c->set_display_name(control.display_name);
                c->set_group(control.group);
                c->set_kind(control.kind == beebium::ControlKind::Axis ? beebium::AXIS
                                                                        : beebium::BUTTON);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status GetControls(grpc::ServerContext*, const GetControlsRequest* request,
                             GetControlsResponse* response) override {
        auto* controller = registry_.find(request->controller_id());
        if (!controller) return not_found(request->controller_id());
        for (const auto& control : controller->controls()) {
            auto* value = response->add_values();
            value->set_id(control.id);
            if (control.kind == beebium::ControlKind::Axis) {
                value->set_axis(controller->axis(control.id));
            } else {
                value->set_pressed(controller->button(control.id));
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status SetControls(grpc::ServerContext*, const SetControlsRequest* request,
                             SetControlsResponse*) override {
        auto* controller = registry_.find(request->controller_id());
        if (!controller) return not_found(request->controller_id());

        for (const auto& value : request->values()) {
            if (auto status = validate(request->controller_id(), *controller, value);
                !status.ok()) {
                return status;
            }
        }
        for (const auto& value : request->values()) {
            if (value.value_case() == ControlValue::kAxis) {
                controller->set_axis(value.id(), static_cast<uint16_t>(value.axis()));
            } else {
                controller->set_button(value.id(), value.pressed());
            }
        }
        return grpc::Status::OK;
    }

private:
    static grpc::Status not_found(const std::string& controller_id) {
        return {grpc::StatusCode::NOT_FOUND,
                "There is no controller named " + controller_id + "."};
    }

    static grpc::Status invalid(std::string message) {
        return {grpc::StatusCode::INVALID_ARGUMENT, std::move(message)};
    }

    // Sentences name the controller by the id the client addressed it by.
    static grpc::Status validate(const std::string& controller_id, const Controller& controller,
                                 const ControlValue& value) {
        const beebium::ControlDescription* control = nullptr;
        for (const auto& c : controller.controls()) {
            if (c.id == value.id()) {
                control = &c;
                break;
            }
        }
        if (!control) {
            return invalid("The controller " + controller_id + " has no control named " +
                           value.id() + ".");
        }
        switch (value.value_case()) {
            case ControlValue::kAxis:
                if (control->kind != beebium::ControlKind::Axis) {
                    return invalid("The control " + value.id() + " of controller " + controller_id +
                                   " is a button, so it takes pressed, not axis.");
                }
                if (value.axis() > 65535) {
                    return invalid("The axis value for " + value.id() + " of controller " +
                                   controller_id + " is " +
                                   std::to_string(value.axis()) +
                                   ", above the largest, 65535.");
                }
                return grpc::Status::OK;
            case ControlValue::kPressed:
                if (control->kind != beebium::ControlKind::Button) {
                    return invalid("The control " + value.id() + " of controller " + controller_id +
                                   " is an axis, so it takes axis, not pressed.");
                }
                return grpc::Status::OK;
            default:
                return invalid("The value for " + value.id() + " of controller " + controller_id +
                               " is neither an axis nor pressed.");
        }
    }

    const ControllerRegistry& registry_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_CONTROLLER_SERVICE_HPP
