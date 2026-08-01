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

#ifndef BEEBIUM_SERVICE_DISC_SERVICE_HPP
#define BEEBIUM_SERVICE_DISC_SERVICE_HPP

#include "disc.grpc.pb.h"
#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/disc/DiscDrive.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"

#include <grpcpp/grpcpp.h>
#include <mutex>

namespace beebium::service {

// Import concepts from beebium namespace
using beebium::HasDiscDrives;
using beebium::HasDiscControllerSocket;
using beebium::HasOptionalDiscController;
using beebium::disc_controller_present;

// gRPC service implementation for DiscService
template<typename MachineType>
class DiscServiceImpl final : public DiscService::Service {
public:
    explicit DiscServiceImpl(MachineType& machine)
        : machine_(machine) {}

    ~DiscServiceImpl() override = default;

    // Non-copyable
    DiscServiceImpl(const DiscServiceImpl&) = delete;
    DiscServiceImpl& operator=(const DiscServiceImpl&) = delete;

    grpc::Status InsertDisc(
        grpc::ServerContext* context,
        const InsertDiscRequest* request,
        InsertDiscResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_success(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            // Check if controller is present (Model B+ always, Model B via socket)
            if (!disc_controller_present(machine_.state().memory)) {
                response->set_success(false);
                response->set_error("Machine has no disc controller");
                return grpc::Status::OK;
            }

            uint32_t drive_num = request->drive();
            if (drive_num > 1) {
                response->set_success(false);
                response->set_error("Invalid drive number (must be 0 or 1)");
                return grpc::Status::OK;
            }

            DiscDrive& drive = (drive_num == 0)
                ? machine_.state().memory.disc_drive_0
                : machine_.state().memory.disc_drive_1;

            // Loading into an occupied drive is refused rather than made to
            // work. Doing it for the caller would mean ejecting the disc
            // that is there, and the only eject available synchronously is
            // the immediate one, which skips the quiescence wait and so can
            // pull a disc out from under a spinning motor mid-command.
            // Removing a disc is the user's decision and needs the safe
            // eject path, so it has to be a separate, deliberate request.
            if (drive.state() != DriveState::Empty) {
                response->set_success(false);
                response->set_error("Drive " + std::to_string(drive_num) +
                                    " already holds a disc; eject it first");
                return grpc::Status::OK;
            }

            // Load disc from URL
            auto result = load_disc_from_url_or_filepath(request->url());
            if (!result) {
                response->set_success(false);
                response->set_error(result.error);
                return grpc::Status::OK;
            }

            // Apply write protection override if requested
            if (request->write_protect_override()) {
                result.disc->set_write_protected(true);
            }

            // Fill metadata before inserting (since insert moves the disc)
            fill_disc_metadata(response->mutable_disc(), result.disc.get());

            // Insert the new disc
            drive.insert(std::move(result.disc), request->url());

            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status EjectDisc(
        grpc::ServerContext* context,
        const EjectDiscRequest* request,
        EjectDiscResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_accepted(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            uint32_t drive_num = request->drive();
            if (drive_num > 1) {
                response->set_accepted(false);
                response->set_error("Invalid drive number (must be 0 or 1)");
                return grpc::Status::OK;
            }

            DiscDrive& drive = (drive_num == 0)
                ? machine_.state().memory.disc_drive_0
                : machine_.state().memory.disc_drive_1;

            if (drive.state() == DriveState::Empty) {
                response->set_accepted(false);
                response->set_error("Drive is empty");
                return grpc::Status::OK;
            }

            if (request->immediate()) {
                // Immediate eject
                drive.eject_immediate();
                response->set_accepted(true);
            } else {
                // Safe eject with quiescence
                EjectOptions opts;
                if (request->quiescence_ms() > 0) {
                    opts.quiescence_duration = std::chrono::milliseconds(request->quiescence_ms());
                }
                if (request->force_after_ms() > 0) {
                    opts.force_after = std::chrono::milliseconds(request->force_after_ms());
                }

                bool accepted = drive.request_eject(opts);
                response->set_accepted(accepted);
                if (!accepted) {
                    response->set_error("Eject already in progress");
                }
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status GetDriveStatus(
        grpc::ServerContext* context,
        const GetDriveStatusRequest* request,
        GetDriveStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_has_disc_controller(false);
            response->set_controller_type("");
            response->set_is_socketed(false);
            response->set_installed_controller_id("");
            return grpc::Status::OK;
        } else {
            using Memory = typename MachineType::Memory;

            // Check if machine has a socket (Model B) or built-in controller (Model B+)
            constexpr bool is_socketed = HasOptionalDiscController<Memory>;
            response->set_is_socketed(is_socketed);

            // Check if controller is present (Model B+ always, Model B via socket)
            bool has_controller = disc_controller_present(machine_.state().memory);
            response->set_has_disc_controller(has_controller);

            if (has_controller) {
                if constexpr (is_socketed) {
                    // Model B with socket - report installed controller
                    if (auto* ctrl = machine_.state().memory.disc_socket.controller()) {
                        response->set_controller_type(std::string(ctrl->name()));
                        response->set_installed_controller_id(
                            std::string(machine_.state().memory.installed_controller_id()));
                    }
                } else {
                    // Model B+ with built-in controller
                    response->set_controller_type("WD1770");
                    response->set_installed_controller_id("");  // Built-in, not from registry
                }

                // Report drive status (only drives connected to controller)
                fill_drive_status(response->add_drives(), 0,
                    machine_.state().memory.disc_drive_0);
                fill_drive_status(response->add_drives(), 1,
                    machine_.state().memory.disc_drive_1);
            } else {
                response->set_controller_type("");
                response->set_installed_controller_id("");
                // Don't report drives if no controller to use them
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status SubscribeDiscEvents(
        grpc::ServerContext* context,
        const SubscribeDiscEventsRequest* request,
        grpc::ServerWriter<DiscEvent>* writer) override
    {
        (void)request;

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            // No disc controller - just return immediately
            return grpc::Status::OK;
        } else {
            // Track previous state for change detection
            DriveState prev_state_0 = DriveState::Empty;
            DriveState prev_state_1 = DriveState::Empty;
            bool prev_motor_0 = false;
            bool prev_motor_1 = false;
            std::string prev_url_0;
            std::string prev_url_1;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                prev_state_0 = machine_.state().memory.disc_drive_0.state();
                prev_state_1 = machine_.state().memory.disc_drive_1.state();
                prev_motor_0 = machine_.state().memory.disc_drive_0.motor_on();
                prev_motor_1 = machine_.state().memory.disc_drive_1.motor_on();
                prev_url_0 = machine_.state().memory.disc_drive_0.source_url();
                prev_url_1 = machine_.state().memory.disc_drive_1.source_url();
            }

            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                std::lock_guard<std::mutex> lock(mutex_);

                // Tick safe eject for both drives (checks quiescence, completes ejection)
                machine_.state().memory.disc_drive_0.tick_eject();
                machine_.state().memory.disc_drive_1.tick_eject();

                // Check drive 0
                check_and_send_events(writer, 0,
                    machine_.state().memory.disc_drive_0,
                    prev_state_0, prev_motor_0, prev_url_0);

                // Check drive 1
                check_and_send_events(writer, 1,
                    machine_.state().memory.disc_drive_1,
                    prev_state_1, prev_motor_1, prev_url_1);
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status SetSpinUpDelay(
        grpc::ServerContext* context,
        const SetSpinUpDelayRequest* request,
        SetSpinUpDelayResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_success(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            machine_.state().memory.set_spin_up_delay_enabled(request->enabled());
            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status GetSpinUpDelay(
        grpc::ServerContext* context,
        const GetSpinUpDelayRequest* request,
        GetSpinUpDelayResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            // No disc controller - return false (no delay possible)
            response->set_enabled(false);
            return grpc::Status::OK;
        } else {
            response->set_enabled(machine_.state().memory.spin_up_delay_enabled());
            return grpc::Status::OK;
        }
    }

    grpc::Status ListAvailableControllers(
        grpc::ServerContext* context,
        const ListAvailableControllersRequest* request,
        ListAvailableControllersResponse* response) override
    {
        (void)context;
        (void)request;
        // No locking needed - registry is const

        // Return all available controller types from the registry
        for (const auto& info : DiscControllerRegistry::available()) {
            auto* ctrl_info = response->add_controllers();
            ctrl_info->set_id(std::string(info.id));
            ctrl_info->set_display_name(std::string(info.display_name));
            ctrl_info->set_fdc_chip(std::string(info.fdc_chip));
            ctrl_info->set_description(std::string(info.description));
        }

        return grpc::Status::OK;
    }

    grpc::Status InstallDiscController(
        grpc::ServerContext* context,
        const InstallDiscControllerRequest* request,
        InstallDiscControllerResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasOptionalDiscController<Memory>) {
            // Machine has built-in controller (Model B+) - can't change
            response->set_success(false);
            response->set_error("Machine has built-in disc controller (not socketed)");
            return grpc::Status::OK;
        } else {
            const std::string& controller_id = request->controller_id();

            // Empty string or "none" removes the controller
            if (controller_id.empty() || controller_id == "none") {
                machine_.state().memory.disc_socket.remove();
                response->set_success(true);
                response->set_controller_type("");
                return grpc::Status::OK;
            }

            // Validate controller ID
            if (!DiscControllerRegistry::is_valid(controller_id)) {
                response->set_success(false);
                response->set_error("Unknown disc controller type: " + controller_id);
                return grpc::Status::OK;
            }

            // Create and install the controller
            auto controller = DiscControllerRegistry::create(controller_id);
            if (!controller) {
                response->set_success(false);
                response->set_error("Failed to create disc controller: " + controller_id);
                return grpc::Status::OK;
            }

            std::string controller_name(controller->name());
            machine_.state().memory.install_disc_controller(std::move(controller), controller_id);

            response->set_success(true);
            response->set_controller_type(controller_name);
            return grpc::Status::OK;
        }
    }

private:
    void fill_disc_metadata(DiscMetadata* metadata, const Disc* disc) {
        if (!disc) return;

        metadata->set_name(disc->name());
        metadata->set_sides(disc->is_double_sided() ? 2 : 1);
        metadata->set_write_protected(disc->is_write_protected());
        metadata->set_format(disc->format_name());
    }

    void fill_drive_status(beebium::DriveStatus* status, uint32_t drive_num,
                           const DiscDrive& drive) {
        status->set_drive(drive_num);

        switch (drive.state()) {
            case DriveState::Empty:
                status->set_state(DISC_DRIVE_STATE_EMPTY);
                break;
            case DriveState::Loaded:
                status->set_state(DISC_DRIVE_STATE_LOADED);
                break;
            case DriveState::Ejecting:
                status->set_state(DISC_DRIVE_STATE_EJECTING);
                break;
        }

        if (drive.disc()) {
            status->set_disc_name(drive.disc()->name());
            fill_disc_metadata(status->mutable_disc(), drive.disc());
        }

        status->set_disc_url(drive.source_url());
        status->set_motor_on(drive.motor_on());
        status->set_current_track(drive.current_track());
        status->set_write_protected(drive.is_write_protected());
    }

    void check_and_send_events(grpc::ServerWriter<DiscEvent>* writer,
                               uint32_t drive_num,
                               DiscDrive& drive,
                               DriveState& prev_state,
                               bool& prev_motor,
                               std::string& prev_url) {
        DriveState curr_state = drive.state();
        bool curr_motor = drive.motor_on();
        const std::string curr_url = drive.source_url();
        uint64_t cycle = machine_.cycle_count();

        // Drive state is sampled every 50ms, so a drive can leave a state and
        // return to it between two samples and look as though nothing
        // happened. An immediate eject followed straight away by an insert
        // does exactly that: Loaded -> Empty -> Loaded, all inside one poll
        // interval, which a client watching state alone would never see. The
        // source URL is what survives the excursion to reveal it, so a
        // different URL under a still-loaded drive is reported as the
        // ejection and insertion that must have happened in between.
        const bool replaced = (curr_state == DriveState::Loaded &&
                               prev_state != DriveState::Empty &&
                               curr_url != prev_url);

        if (replaced) {
            DiscEvent ejected;
            ejected.set_drive(drive_num);
            ejected.set_timestamp_cycles(cycle);
            ejected.set_type(DISC_EVENT_EJECTED);
            writer->Write(ejected);
        }

        // State change events
        if (curr_state != prev_state || replaced) {
            DiscEvent event;
            event.set_drive(drive_num);
            event.set_timestamp_cycles(cycle);

            if (replaced ||
                (prev_state == DriveState::Empty && curr_state == DriveState::Loaded)) {
                event.set_type(DISC_EVENT_INSERTED);
                fill_disc_metadata(event.mutable_disc(), drive.disc());
                event.set_disc_url(curr_url);
            } else if (prev_state == DriveState::Loaded && curr_state == DriveState::Ejecting) {
                event.set_type(DISC_EVENT_EJECT_REQUESTED);
            } else if (prev_state == DriveState::Ejecting && curr_state == DriveState::Empty) {
                if (drive.was_forced_eject()) {
                    event.set_type(DISC_EVENT_FORCE_EJECTED);
                } else {
                    event.set_type(DISC_EVENT_EJECTED);
                }
            } else if (prev_state == DriveState::Ejecting && curr_state == DriveState::Loaded) {
                event.set_type(DISC_EVENT_EJECT_CANCELLED);
            } else if (curr_state == DriveState::Empty) {
                // Any other transition to empty (e.g., immediate eject from Loaded)
                event.set_type(DISC_EVENT_EJECTED);
            }

            writer->Write(event);
            prev_state = curr_state;
        }

        prev_url = curr_url;

        // Motor change events
        if (curr_motor != prev_motor) {
            DiscEvent event;
            event.set_drive(drive_num);
            event.set_timestamp_cycles(cycle);
            event.set_type(curr_motor ? DISC_EVENT_MOTOR_ON : DISC_EVENT_MOTOR_OFF);
            writer->Write(event);
            prev_motor = curr_motor;
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DISC_SERVICE_HPP
