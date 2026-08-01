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
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

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
        : machine_(machine)
    {
        if constexpr (HasDiscDrives<typename MachineType::Memory>) {
            attach_observer(machine_.state().memory.disc_drive_0, 0);
            attach_observer(machine_.state().memory.disc_drive_1, 1);
        }
    }

    ~DiscServiceImpl() override {
        // The drives outlive this service, so stop them calling into a
        // half-destroyed object.
        if constexpr (HasDiscDrives<typename MachineType::Memory>) {
            machine_.state().memory.disc_drive_0.set_observer(nullptr);
            machine_.state().memory.disc_drive_1.set_observer(nullptr);
        }
        broker_.close();
    }

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

            // Park the emulation loop first. This runs on an RPC thread, and
            // the disc controller reads pulses out of the drive from the
            // emulation thread; swapping the drive's disc underneath it is a
            // data race on a pointer that is about to be freed.
            machine_.with_emulation_paused([&] {
                drive.insert(std::move(result.disc), request->url());
            });

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
                // Forcing is the caller's decision, and this is where it is
                // taken: the disc leaves whatever the drive is doing. Park
                // the emulation loop, since completing the eject frees the
                // disc the controller may be reading (see InsertDisc).
                machine_.with_emulation_paused([&] {
                    drive.eject_immediate();
                });
                response->set_accepted(true);
            } else {
                // Safe eject: ask, then let the emulation loop finish it once
                // the drive falls quiet. It waits indefinitely; the server
                // never decides on the caller's behalf to force.
                EjectOptions opts;
                if (request->quiescence_ms() > 0) {
                    opts.quiescence_duration = std::chrono::milliseconds(request->quiescence_ms());
                }

                bool accepted = false;
                machine_.with_emulation_paused([&] {
                    accepted = drive.request_eject(opts);
                });
                response->set_accepted(accepted);
                if (!accepted) {
                    response->set_error("Eject already in progress");
                }
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status CancelEject(
        grpc::ServerContext* context,
        const CancelEjectRequest* request,
        CancelEjectResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        if constexpr (!HasDiscDrives<typename MachineType::Memory>) {
            response->set_cancelled(false);
            response->set_error("Machine has no disc controller");
            return grpc::Status::OK;
        } else {
            uint32_t drive_num = request->drive();
            if (drive_num > 1) {
                response->set_cancelled(false);
                response->set_error("Invalid drive number (must be 0 or 1)");
                return grpc::Status::OK;
            }

            DiscDrive& drive = (drive_num == 0)
                ? machine_.state().memory.disc_drive_0
                : machine_.state().memory.disc_drive_1;

            // Only touches the drive's state word, but it shares a thread
            // with everything else that does, so it is serialised the same
            // way. See InsertDisc for why.
            bool cancelled = false;
            machine_.with_emulation_paused([&] {
                cancelled = drive.cancel_eject();
            });

            response->set_cancelled(cancelled);
            if (!cancelled) {
                response->set_error("No eject pending");
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
            // No disc controller - nothing will ever happen
            return grpc::Status::OK;
        } else {
            auto subscriber = broker_.subscribe();

            // Events arrive from the drives themselves, so nothing here
            // samples drive state and nothing here can miss a change that
            // began and ended between two looks. The wait has a timeout only
            // because a cancelled RPC cannot wake a condition variable.
            std::vector<DiscEvent> batch;
            while (!context->IsCancelled()) {
                subscriber->wait_for_events(std::chrono::milliseconds(100), batch);
                for (const auto& event : batch) {
                    if (!writer->Write(event)) {
                        broker_.unsubscribe(subscriber);
                        return grpc::Status::OK;
                    }
                }
                batch.clear();
            }

            broker_.unsubscribe(subscriber);
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

    // Fan-out of drive events to the open SubscribeDiscEvents streams.
    //
    // A drive announces a change on whichever thread made it -- the emulation
    // thread for a motor transition or a safe eject completing, an RPC thread
    // for an insert or an immediate eject -- so there are several producers
    // and this cannot be the single-producer queue used elsewhere. Publishing
    // takes a short lock per subscriber to append a small value and signal;
    // it never waits on a subscriber, so a client that stops reading cannot
    // hold up the emulation thread. Such a client loses its oldest events
    // rather than growing the queue without bound.
    class Broker {
    public:
        class Subscription {
        public:
            // Wait for events, then hand over everything queued. Returns
            // having waited at most `timeout`, possibly with nothing.
            void wait_for_events(std::chrono::milliseconds timeout,
                                 std::vector<DiscEvent>& out) {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); });
                while (!queue_.empty()) {
                    out.push_back(std::move(queue_.front()));
                    queue_.pop_front();
                }
            }

            void post(const DiscEvent& event) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (queue_.size() >= MAX_QUEUED) {
                        queue_.pop_front();
                    }
                    queue_.push_back(event);
                }
                cv_.notify_one();
            }

            void wake() { cv_.notify_all(); }

        private:
            // Deep enough for any burst a drive can produce; a subscriber
            // this far behind is not reading at all.
            static constexpr size_t MAX_QUEUED = 256;

            std::mutex mutex_;
            std::condition_variable cv_;
            std::deque<DiscEvent> queue_;
        };

        std::shared_ptr<Subscription> subscribe() {
            auto sub = std::make_shared<Subscription>();
            std::lock_guard<std::mutex> lock(mutex_);
            subscriptions_.push_back(sub);
            return sub;
        }

        void unsubscribe(const std::shared_ptr<Subscription>& sub) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::erase(subscriptions_, sub);
        }

        void publish(const DiscEvent& event) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& sub : subscriptions_) {
                sub->post(event);
            }
        }

        // Wake every subscriber so an in-progress stream notices it should
        // wind up, rather than sitting out its timeout.
        void close() {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& sub : subscriptions_) {
                sub->wake();
            }
        }

    private:
        std::mutex mutex_;
        std::vector<std::shared_ptr<Subscription>> subscriptions_;
    };

    // Bridge one drive's changes onto the wire. The drive knows nothing of
    // protobuf or of which drive number it is; both are supplied here.
    void attach_observer(DiscDrive& drive, uint32_t drive_num) {
        drive.set_observer([this, drive_num](const DiscDriveEvent& change) {
            DiscEvent event;
            event.set_drive(drive_num);
            event.set_timestamp_cycles(machine_.cycle_count());

            switch (change.type) {
                case DiscDriveEventType::Inserted:
                    event.set_type(DISC_EVENT_INSERTED);
                    event.set_disc_url(change.source_url);
                    event.mutable_disc()->set_name(change.disc_name);
                    event.mutable_disc()->set_sides(change.sides);
                    event.mutable_disc()->set_write_protected(change.write_protected);
                    event.mutable_disc()->set_format(change.format);
                    break;
                case DiscDriveEventType::EjectRequested:
                    event.set_type(DISC_EVENT_EJECT_REQUESTED);
                    break;
                case DiscDriveEventType::EjectCancelled:
                    event.set_type(DISC_EVENT_EJECT_CANCELLED);
                    break;
                case DiscDriveEventType::Ejected:
                    event.set_type(DISC_EVENT_EJECTED);
                    break;
                case DiscDriveEventType::ForceEjected:
                    event.set_type(DISC_EVENT_FORCE_EJECTED);
                    break;
                case DiscDriveEventType::MotorOn:
                    event.set_type(DISC_EVENT_MOTOR_ON);
                    break;
                case DiscDriveEventType::MotorOff:
                    event.set_type(DISC_EVENT_MOTOR_OFF);
                    break;
            }

            broker_.publish(event);
        });
    }

    MachineType& machine_;
    std::mutex mutex_;
    Broker broker_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DISC_SERVICE_HPP
