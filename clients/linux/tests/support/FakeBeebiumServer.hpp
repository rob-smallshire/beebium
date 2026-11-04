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

// An in-process stand-in for a Beebium server: the services a machine
// window's session uses, with a status stream the test writes event by event.

#ifndef BEEBIUM_LINUX_TESTS_FAKE_BEEBIUM_SERVER_HPP
#define BEEBIUM_LINUX_TESTS_FAKE_BEEBIUM_SERVER_HPP

#include "app/ApplicationInfo.hpp"

#include "beebium/host_identity/HostFingerprint.hpp"

#include "audio.grpc.pb.h"
#include "disc.grpc.pb.h"
#include "extension_ui.grpc.pb.h"
#include "indicator.grpc.pb.h"
#include "keyboard.grpc.pb.h"
#include "sideways.grpc.pb.h"
#include "system.grpc.pb.h"
#include "video.grpc.pb.h"
#include "debugger.grpc.pb.h"

#include "WaitUntil.hpp"

#include <grpcpp/grpcpp.h>

#include <QCoreApplication>
#include <QString>

#include <atomic>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace beebium::linux_client::testing {

using namespace std::chrono_literals;

// A status stream the test writes event by event; nullopt ends it.
class ScriptedSystemService final : public beebium::SystemService::Service {
public:
    std::string protocol_fingerprint{client_protocol_fingerprint()};
    std::string host_fingerprint = beebium::host_identity::host_fingerprint();

    // What the machine-control calls saw, and how they answer.
    std::mutex calls_mutex;
    std::string last_save_destination;
    bool last_save_replace = false;
    std::string last_name;
    std::string shutdown_authority;  // x-beebium-instance-uuid
    int shutdown_requests = 0;
    std::string release_authority;  // x-beebium-instance-uuid
    int release_requests = 0;
    bool save_fails = false;
    int save_requests = 0;
    std::string last_move_destination;
    // The code a move is refused with; OK lets it through.
    grpc::StatusCode move_refusal = grpc::StatusCode::OK;
    std::atomic<int> client_count{1};
    // GetSystemInfo blocks, as a stalled server's would, while this is set,
    // and answers again when it is cleared or the call is cancelled.
    std::atomic<bool> stall_system_info{false};
    std::atomic<int> stalled_calls{0};
    // Answers as a saved machine in this directory, when set (calls_mutex).
    std::string saved_machine_dirpath;

    void push(std::optional<beebium::ServerStatusEvent> event) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(std::move(event));
        }
        changed_.notify_all();
    }

    // WatchPacingStats: how many watch now, the interval the last asked
    // for, and the samples to send them.
    std::atomic<int> pacing_watchers{0};
    std::atomic<std::uint32_t> last_pacing_interval_ms{0};
    void push_pacing(const beebium::PacingStats& stats) {
        {
            std::lock_guard lock(mutex_);
            pacing_script_.push_back(stats);
        }
        changed_.notify_all();
    }
    // SetSpeedMultiplier: every multiplier asked for, in order (calls_mutex).
    std::vector<double> speed_requests;

    grpc::Status GetSystemInfo(grpc::ServerContext* context, const beebium::GetSystemInfoRequest*,
                               beebium::SystemInfo* info) override {
        info->set_protocol_fingerprint(protocol_fingerprint);
        info->set_host_fingerprint(host_fingerprint);
        info->mutable_identity()->set_name("Model B #1");
        info->mutable_identity()->set_description("Station 1.254");
        info->mutable_identity()->set_model_name("BBC Model B");
        info->mutable_connections()->set_client_count(client_count);
        {
            std::lock_guard lock(calls_mutex);
            if (!saved_machine_dirpath.empty()) {
                info->set_machine_saved(true);
                info->set_machine_dirpath(saved_machine_dirpath);
            }
        }
        if (stall_system_info) {
            ++stalled_calls;
            while (stall_system_info && !context->IsCancelled()) {
                std::this_thread::sleep_for(5ms);
            }
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status SaveMachine(grpc::ServerContext*, const beebium::SaveMachineRequest* request,
                             beebium::SaveMachineResponse* response) override {
        std::lock_guard lock(calls_mutex);
        last_save_destination = request->destination_dirpath();
        ++save_requests;
        last_save_replace = request->replace_existing();
        if (save_fails) {
            return {grpc::StatusCode::ALREADY_EXISTS, "destination exists"};
        }
        response->set_machine_dirpath(request->destination_dirpath().empty()
                                          ? std::string("/m/Saved.beebium-machine")
                                          : request->destination_dirpath());
        return grpc::Status::OK;
    }

    grpc::Status MoveMachine(grpc::ServerContext*, const beebium::MoveMachineRequest* request,
                             beebium::MoveMachineResponse* response) override {
        std::lock_guard lock(calls_mutex);
        last_move_destination = request->destination_dirpath();
        if (move_refusal != grpc::StatusCode::OK) {
            return {move_refusal, "refused by the fake server"};
        }
        response->set_machine_dirpath(request->destination_dirpath());
        return grpc::Status::OK;
    }

    grpc::Status SetMachineName(grpc::ServerContext*, const beebium::SetMachineNameRequest* request,
                                beebium::SetMachineNameResponse*) override {
        std::lock_guard lock(calls_mutex);
        last_name = request->name();
        return grpc::Status::OK;
    }

    grpc::Status ResetBattery(grpc::ServerContext*, const beebium::ResetBatteryRequest*,
                              beebium::ResetBatteryResponse* response) override {
        response->add_reset_units("integra-b-rtc");
        return grpc::Status::OK;
    }

    grpc::Status RequestShutdown(grpc::ServerContext* context, const beebium::ShutdownRequest*,
                                 beebium::ShutdownResponse* response) override {
        std::lock_guard lock(calls_mutex);
        ++shutdown_requests;
        const auto found = context->client_metadata().find("x-beebium-instance-uuid");
        if (found != context->client_metadata().end()) {
            shutdown_authority = std::string(found->second.data(), found->second.size());
        }
        response->set_accepted(true);
        return grpc::Status::OK;
    }

    grpc::Status ReleaseFromLauncher(grpc::ServerContext* context,
                                     const beebium::ReleaseFromLauncherRequest*,
                                     beebium::ReleaseFromLauncherResponse* response) override {
        std::lock_guard lock(calls_mutex);
        ++release_requests;
        const auto found = context->client_metadata().find("x-beebium-instance-uuid");
        if (found != context->client_metadata().end()) {
            release_authority = std::string(found->second.data(), found->second.size());
        }
        response->set_message("released");
        return grpc::Status::OK;
    }

    grpc::Status WatchServerStatus(grpc::ServerContext* context,
                                   const beebium::WatchServerStatusRequest*,
                                   grpc::ServerWriter<beebium::ServerStatusEvent>* writer) override {
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            auto next = std::move(script_.front());
            script_.pop_front();
            lock.unlock();
            if (!next) {
                return grpc::Status::OK;
            }
            writer->Write(*next);
        }
    }

    grpc::Status WatchPacingStats(grpc::ServerContext* context,
                                  const beebium::WatchPacingStatsRequest* request,
                                  grpc::ServerWriter<beebium::PacingStats>* writer) override {
        last_pacing_interval_ms = request->interval_ms();
        ++pacing_watchers;
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !pacing_script_.empty(); });
            if (context->IsCancelled()) {
                --pacing_watchers;
                return grpc::Status::CANCELLED;
            }
            if (pacing_script_.empty()) {
                continue;
            }
            const beebium::PacingStats next = pacing_script_.front();
            pacing_script_.pop_front();
            lock.unlock();
            writer->Write(next);
        }
    }

    grpc::Status SetSpeedMultiplier(grpc::ServerContext*,
                                    const beebium::SetSpeedMultiplierRequest* request,
                                    beebium::SetSpeedMultiplierResponse* response) override {
        std::lock_guard lock(calls_mutex);
        speed_requests.push_back(request->speed_multiplier());
        response->set_speed_multiplier(request->speed_multiplier());
        return grpc::Status::OK;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::optional<beebium::ServerStatusEvent>> script_;
    std::deque<beebium::PacingStats> pacing_script_;
};

class QuietVideoService final : public beebium::VideoService::Service {
    grpc::Status GetConfig(grpc::ServerContext*, const beebium::GetConfigRequest*,
                           beebium::VideoConfig* config) override {
        config->set_width(640);
        config->set_height(256);
        config->set_framerate_hz(50);
        return grpc::Status::OK;
    }
    grpc::Status SubscribeFrames(grpc::ServerContext* context, const beebium::SubscribeFramesRequest*,
                                 grpc::ServerWriter<beebium::Frame>*) override {
        while (!context->IsCancelled()) {
            std::this_thread::sleep_for(10ms);
        }
        return grpc::Status::CANCELLED;
    }
};

// The SN76489 alone, as the server describes it, and a scripted stream of
// chunks: push a chunk to send it, nullopt to end the stream.
class ScriptedAudioService final : public beebium::AudioService::Service {
public:
    std::atomic<int> format_calls{0};
    std::atomic<int> subscriptions{0};

    void push(std::optional<beebium::AudioChunk> chunk) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(std::move(chunk));
        }
        changed_.notify_all();
    }

    grpc::Status GetAudioFormat(grpc::ServerContext*, const beebium::GetAudioFormatRequest*,
                                beebium::AudioFormat* format) override {
        ++format_calls;
        format->set_sample_rate(48000);
        format->set_source_count(2);
        auto* group = format->add_groups();
        group->set_group_id(1);
        group->set_group_name("Internal Sound");
        const char* names[2][2] = {{"Tone 0", "Tone 1"}, {"Tone 2", "Noise"}};
        for (std::uint32_t field = 0; field < 2; ++field) {
            auto* source = format->add_sources();
            source->set_source_index(field);
            source->set_source_name("SN76489");
            source->set_encoding(beebium::ENCODING_2X16BIT_SIGNED);
            source->set_group_id(1);
            source->add_channel_names(names[field][0]);
            source->add_channel_names(names[field][1]);
        }
        return grpc::Status::OK;
    }

    grpc::Status SubscribeAudio(grpc::ServerContext* context, const beebium::SubscribeAudioRequest*,
                                grpc::ServerWriter<beebium::AudioChunk>* writer) override {
        ++subscriptions;
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            auto next = std::move(script_.front());
            script_.pop_front();
            lock.unlock();
            if (!next) {
                return grpc::Status::OK;
            }
            writer->Write(*next);
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::optional<beebium::AudioChunk>> script_;
};

// A chunk of `count` two-field frames starting at produced sample `sequence`,
// every field `value`.
inline beebium::AudioChunk audio_chunk(std::uint64_t sequence, std::uint32_t count,
                                       std::uint32_t value = 0) {
    beebium::AudioChunk chunk;
    chunk.set_sequence(sequence);
    chunk.set_sample_count(count);
    std::string bytes(static_cast<std::size_t>(count) * 2 * 4, '\0');
    for (std::size_t word = 0; word < static_cast<std::size_t>(count) * 2; ++word) {
        std::memcpy(&bytes[word * 4], &value, 4);
    }
    chunk.set_samples(bytes);
    return chunk;
}

// Two floppy drives that behave as the server's do: an insert succeeds
// unless told to fail, and announces itself on the event stream; a safe
// eject asks, then completes unless the drive is held busy; an immediate
// one force-ejects; CancelEject abandons a pending one. Blank discs are two
// templates; CreateDisc writes an empty file with the template's extension.
class ScriptedDiscService final : public beebium::DiscService::Service {
public:
    struct Drive {
        beebium::DiscDriveState state = beebium::DISC_DRIVE_STATE_EMPTY;
        std::string name;
        std::string url;
        bool motor = false;
        bool write_protected = false;
    };

    std::mutex calls_mutex;
    bool has_controller = true;
    std::vector<Drive> drives{Drive{}, Drive{}};
    // The next insert fails with this kind and message, when set.
    std::optional<std::pair<beebium::DiscErrorKind, std::string>> fail_next_insert;
    // A safe eject waits while this is set, as a drive whose motor runs.
    bool drive_busy = false;
    std::vector<std::string> inserted_urls;
    int immediate_ejects = 0;
    int cancels = 0;
    std::atomic<int> subscriptions{0};

    void push(std::optional<beebium::DiscEvent> event) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(std::move(event));
        }
        changed_.notify_all();
    }
    // An event as the server would send it.
    void announce(beebium::DiscEventType type, std::uint32_t drive) {
        beebium::DiscEvent event;
        event.set_type(type);
        event.set_drive(drive);
        {
            std::lock_guard lock(calls_mutex);
            if (drive < drives.size() && !drives[drive].name.empty()) {
                event.mutable_disc()->set_name(drives[drive].name);
                event.mutable_disc()->set_format("SSD");
                event.mutable_disc()->set_sides(1);
                event.set_disc_url(drives[drive].url);
            }
        }
        push(event);
    }

    grpc::Status GetDriveStatus(grpc::ServerContext*, const beebium::GetDriveStatusRequest*,
                                beebium::GetDriveStatusResponse* response) override {
        std::lock_guard lock(calls_mutex);
        response->set_has_disc_controller(has_controller);
        if (!has_controller) {
            return grpc::Status::OK;
        }
        response->set_controller_type("WD1770");
        for (std::uint32_t i = 0; i < drives.size(); ++i) {
            auto* status = response->add_drives();
            status->set_drive(i);
            status->set_state(drives[i].state);
            status->set_disc_name(drives[i].name);
            status->set_disc_url(drives[i].url);
            status->set_motor_on(drives[i].motor);
            status->set_write_protected(drives[i].write_protected);
            if (!drives[i].name.empty()) {
                status->mutable_disc()->set_name(drives[i].name);
                status->mutable_disc()->set_format("SSD");
                status->mutable_disc()->set_sides(1);
                status->mutable_disc()->set_write_protected(drives[i].write_protected);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status InsertDisc(grpc::ServerContext*, const beebium::InsertDiscRequest* request,
                            beebium::InsertDiscResponse* response) override {
        {
            std::lock_guard lock(calls_mutex);
            inserted_urls.push_back(request->url());
            if (fail_next_insert) {
                response->set_success(false);
                response->set_kind(fail_next_insert->first);
                response->set_error(fail_next_insert->second);
                fail_next_insert.reset();
                return grpc::Status::OK;
            }
            if (request->drive() >= drives.size()) {
                response->set_kind(beebium::DISC_ERROR_KIND_INVALID_DRIVE);
                response->set_error("invalid drive number");
                return grpc::Status::OK;
            }
            Drive& drive = drives[request->drive()];
            if (drive.state != beebium::DISC_DRIVE_STATE_EMPTY) {
                response->set_kind(beebium::DISC_ERROR_KIND_DRIVE_OCCUPIED);
                response->set_error("drive 0 already holds a disc");
                return grpc::Status::OK;
            }
            const std::string& url = request->url();
            drive.state = beebium::DISC_DRIVE_STATE_LOADED;
            drive.url = url;
            drive.name = url.substr(url.find_last_of('/') + 1);
            response->set_success(true);
            response->mutable_disc()->set_name(drive.name);
            response->mutable_disc()->set_format("SSD");
            response->mutable_disc()->set_sides(1);
        }
        announce(beebium::DISC_EVENT_INSERTED, request->drive());
        return grpc::Status::OK;
    }

    grpc::Status EjectDisc(grpc::ServerContext*, const beebium::EjectDiscRequest* request,
                           beebium::EjectDiscResponse* response) override {
        bool complete = false;
        {
            std::lock_guard lock(calls_mutex);
            if (request->drive() >= drives.size() ||
                drives[request->drive()].state == beebium::DISC_DRIVE_STATE_EMPTY) {
                response->set_error("drive empty");
                return grpc::Status::OK;
            }
            response->set_accepted(true);
            if (request->immediate()) {
                ++immediate_ejects;
            }
            complete = request->immediate() || !drive_busy;
            drives[request->drive()].state = beebium::DISC_DRIVE_STATE_EJECTING;
        }
        if (!request->immediate()) {
            announce(beebium::DISC_EVENT_EJECT_REQUESTED, request->drive());
        }
        if (complete) {
            {
                std::lock_guard lock(calls_mutex);
                drives[request->drive()] = Drive{};
            }
            announce(request->immediate() ? beebium::DISC_EVENT_FORCE_EJECTED
                                          : beebium::DISC_EVENT_EJECTED,
                     request->drive());
        }
        return grpc::Status::OK;
    }

    grpc::Status CancelEject(grpc::ServerContext*, const beebium::CancelEjectRequest* request,
                             beebium::CancelEjectResponse* response) override {
        {
            std::lock_guard lock(calls_mutex);
            ++cancels;
            if (request->drive() >= drives.size() ||
                drives[request->drive()].state != beebium::DISC_DRIVE_STATE_EJECTING) {
                response->set_error("no eject pending");
                return grpc::Status::OK;
            }
            drives[request->drive()].state = beebium::DISC_DRIVE_STATE_LOADED;
            response->set_cancelled(true);
        }
        announce(beebium::DISC_EVENT_EJECT_CANCELLED, request->drive());
        return grpc::Status::OK;
    }

    grpc::Status ListBlankDiscs(grpc::ServerContext*, const beebium::ListBlankDiscsRequest*,
                                beebium::ListBlankDiscsResponse* response) override {
        for (const char* filename : {"Acorn DFS 80T DS 400K.dsd", "Acorn DFS 80T SS 200K.ssd"}) {
            auto* disc = response->add_discs();
            const std::string name(filename);
            disc->set_filename(name);
            disc->set_label(name.substr(0, name.find_last_of('.')));
            disc->set_kind("floppy");
            disc->set_size_bytes(name.find(".dsd") != std::string::npos ? 409600 : 204800);
        }
        return grpc::Status::OK;
    }

    grpc::Status CreateDisc(grpc::ServerContext*, const beebium::CreateDiscRequest* request,
                            beebium::CreateDiscResponse* response) override {
        const std::string& label = request->label();
        std::string extension;
        if (label == "Acorn DFS 80T DS 400K") {
            extension = ".dsd";
        } else if (label == "Acorn DFS 80T SS 200K") {
            extension = ".ssd";
        } else {
            response->set_kind(beebium::CREATE_DISC_ERROR_KIND_UNKNOWN_LABEL);
            response->set_error("no blank disc is called " + label);
            return grpc::Status::OK;
        }
        std::string path = request->destination_path();
        const auto dot = path.find_last_of('.');
        const auto slash = path.find_last_of('/');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
            path += extension;
        } else if (path.substr(dot) != extension) {
            response->set_kind(beebium::CREATE_DISC_ERROR_KIND_WRONG_EXTENSION);
            response->set_error("a blank disc from " + label + " must end in " + extension);
            return grpc::Status::OK;
        }
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) {
            response->set_kind(beebium::CREATE_DISC_ERROR_KIND_WRITE_FAILED);
            response->set_error("cannot write " + path);
            return grpc::Status::OK;
        }
        std::fclose(file);
        response->set_success(true);
        response->set_created_path(path);
        return grpc::Status::OK;
    }

    grpc::Status SubscribeDiscEvents(grpc::ServerContext* context,
                                     const beebium::SubscribeDiscEventsRequest*,
                                     grpc::ServerWriter<beebium::DiscEvent>* writer) override {
        ++subscriptions;
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            auto next = std::move(script_.front());
            script_.pop_front();
            lock.unlock();
            if (!next) {
                return grpc::Status::OK;
            }
            writer->Write(*next);
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::optional<beebium::DiscEvent>> script_;
};

// The server's LEDs: the two lock LEDs, then the drives', as it registers
// them, and a scripted stream of brightness updates.
class ScriptedIndicatorService final : public beebium::IndicatorService::Service {
public:
    std::atomic<int> subscriptions{0};
    std::atomic<std::uint32_t> min_interval_ms{0};

    void push(std::optional<std::vector<std::pair<std::string, std::uint32_t>>> values) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(std::move(values));
        }
        changed_.notify_all();
    }

    grpc::Status ListIndicators(grpc::ServerContext*, const beebium::ListIndicatorsRequest*,
                                beebium::ListIndicatorsResponse* response) override {
        const auto add = [response](const char* name, const char* label, const char* color,
                                    const char* shape, const char* related_key) {
            auto* info = response->add_indicators();
            info->set_name(name);
            auto& metadata = *info->mutable_metadata();
            metadata["label"] = label;
            metadata["color"] = color;
            metadata["shape"] = shape;
            if (related_key) {
                metadata["related_key"] = related_key;
            }
        };
        add("caps-lock-led", "CAPS LOCK", "625nm", "domed", "Caps Lock");
        add("shift-lock-led", "SHIFT LOCK", "625nm", "domed", "Shift Lock");
        add("drive-0-led", "DRIVE 0", "568nm", "rectangular", nullptr);
        add("drive-1-led", "DRIVE 1", "#00ff00", "rectangular", nullptr);
        return grpc::Status::OK;
    }

    grpc::Status Subscribe(grpc::ServerContext* context,
                           const beebium::SubscribeIndicatorsRequest* request,
                           grpc::ServerWriter<beebium::IndicatorUpdate>* writer) override {
        ++subscriptions;
        min_interval_ms = request->min_interval_ms();
        std::uint64_t sequence = 0;
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            auto next = std::move(script_.front());
            script_.pop_front();
            lock.unlock();
            if (!next) {
                return grpc::Status::OK;
            }
            beebium::IndicatorUpdate update;
            update.set_sequence(++sequence);
            for (const auto& [name, value] : *next) {
                (*update.mutable_values())[name] = value;
            }
            writer->Write(update);
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::optional<std::vector<std::pair<std::string, std::uint32_t>>>> script_;
};

// A machine that starts blocked, as a launched server does, until Run.
class BlockedDebuggerService final : public beebium::DebuggerControl::Service {
public:
    std::atomic<bool> running{false};
    std::atomic<int> runs{0};

    grpc::Status GetState(grpc::ServerContext*, const beebium::Empty*,
                          beebium::ExecutionState* state) override {
        state->set_is_running(running);
        return grpc::Status::OK;
    }
    grpc::Status Run(grpc::ServerContext*, const beebium::RunRequest*,
                     beebium::RunResponse* response) override {
        ++runs;
        running = true;
        response->set_success(true);
        return grpc::Status::OK;
    }
};

// The keyboard as the server keeps it: a key table, the machine's lock
// latches (a Caps Lock key-down toggles caps, as the MOS does), the
// type-ahead queue, and a scripted typing-status stream.
class KeyTableService final : public beebium::KeyboardService::Service {
public:
    std::atomic<bool> caps_lock_on{false};
    std::atomic<bool> shift_lock_on{false};
    std::atomic<int> lock_reads{0};
    std::atomic<int> caps_taps{0};
    std::mutex typing_mutex;
    std::vector<std::string> typed;  // TypeQuickly texts accepted
    // TypeQuickly refuses a text containing this, naming it (typing_mutex).
    std::string refuse;
    std::atomic<int> clears{0};
    std::atomic<int> typing_watchers{0};
    // TypeQuickly reports the text as typed already: nothing pending.
    std::atomic<bool> typed_at_once{false};
    std::mutex keys_mutex;
    std::vector<std::uint32_t> keys_down;  // KeyDown key numbers, in order (keys_mutex)

    void push_typing(std::optional<beebium::TypingStatus> status) {
        {
            std::lock_guard lock(script_mutex_);
            script_.push_back(std::move(status));
        }
        changed_.notify_all();
    }

    grpc::Status GetAllKeyMappings(grpc::ServerContext*, const beebium::GetAllKeyMappingsRequest*,
                                   beebium::AllKeyMappingsResponse* response) override {
        const auto add = [response](const char* character, std::uint32_t ik, const char* name) {
            auto* entry = response->add_mappings();
            entry->set_character(character);
            entry->set_ik_number(ik);
            entry->set_name(name);
        };
        add("a", 0x41, "a");
        add("h", 0x54, "h");
        add("k", 0x46, "k");
        add("v", 0x63, "v");
        add("/", 0x68, "/");
        add("", 0x40, "Caps Lock");
        add("", 0x50, "Shift Lock");
        return grpc::Status::OK;
    }

    grpc::Status GetLockState(grpc::ServerContext*, const beebium::GetLockStateRequest*,
                              beebium::LockKeyState* state) override {
        ++lock_reads;
        state->set_caps_lock_on(caps_lock_on);
        state->set_shift_lock_on(shift_lock_on);
        return grpc::Status::OK;
    }

    grpc::Status KeyDown(grpc::ServerContext*, const beebium::KeyRequest* request,
                         beebium::KeyResponse*) override {
        {
            std::lock_guard lock(keys_mutex);
            keys_down.push_back(request->ik_number());
        }
        if (request->ik_number() == 0x40) {
            ++caps_taps;
            caps_lock_on = !caps_lock_on;
        }
        return grpc::Status::OK;
    }

    grpc::Status KeyUp(grpc::ServerContext*, const beebium::KeyRequest*,
                       beebium::KeyResponse*) override {
        return grpc::Status::OK;
    }

    grpc::Status TypeQuickly(grpc::ServerContext*, const beebium::TypeQuicklyRequest* request,
                             beebium::TypeQuicklyResponse* response) override {
        std::lock_guard lock(typing_mutex);
        if (!refuse.empty() && request->text().find(refuse) != std::string::npos) {
            response->set_accepted(false);
            response->set_error("cannot type \"" + refuse + "\" on a BBC Micro keyboard");
            return grpc::Status::OK;
        }
        typed.push_back(request->text());
        response->set_accepted(true);
        response->set_pending_characters(
            typed_at_once ? 0u : static_cast<std::uint32_t>(request->text().size()));
        return grpc::Status::OK;
    }

    grpc::Status ClearTyping(grpc::ServerContext*, const beebium::ClearTypingRequest*,
                             beebium::ClearTypingResponse* response) override {
        ++clears;
        response->set_characters_cleared(7);
        return grpc::Status::OK;
    }

    grpc::Status WatchTypingStatus(grpc::ServerContext* context,
                                   const beebium::WatchTypingStatusRequest*,
                                   grpc::ServerWriter<beebium::TypingStatus>* writer) override {
        ++typing_watchers;
        for (;;) {
            std::unique_lock lock(script_mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            auto next = std::move(script_.front());
            script_.pop_front();
            lock.unlock();
            if (!next) {
                return grpc::Status::OK;
            }
            writer->Write(*next);
        }
    }

private:
    std::mutex script_mutex_;
    std::condition_variable changed_;
    std::deque<std::optional<beebium::TypingStatus>> script_;
};

// A typing status as the server would push it.
inline beebium::TypingStatus typing_status(bool guest_ready, std::uint32_t pending,
                                           std::uint32_t discarded = 0) {
    beebium::TypingStatus status;
    status.set_guest_ready(guest_ready);
    status.set_pending_characters(pending);
    status.set_idle(pending == 0);
    status.set_machine_running(true);
    status.set_discarded_by_reset(discarded);
    return status;
}

// SidewaysService: a status to report, protection switches that change it,
// and events to stream to whoever subscribes.
class ScriptedSidewaysService final : public beebium::SidewaysService::Service {
public:
    std::mutex status_mutex;
    beebium::GetSlotStatusResponse status;  // under status_mutex
    std::atomic<int> status_requests{0};
    std::atomic<int> watchers{0};
    std::atomic<bool> last_monitor_header_changes{false};
    // The last SetSlotProtection, and whether to refuse the next ones.
    std::string last_group_id;
    beebium::SidewaysProtectionKind last_kind = beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT;
    bool last_engaged = false;
    std::string refusal;  // non-empty: refuse with this error

    void push(const beebium::SidewaysEvent& event) {
        {
            std::lock_guard lock(mutex_);
            script_.push_back(event);
        }
        changed_.notify_all();
    }

    grpc::Status GetSlotStatus(grpc::ServerContext*, const beebium::GetSlotStatusRequest*,
                               beebium::GetSlotStatusResponse* response) override {
        std::lock_guard lock(status_mutex);
        ++status_requests;
        *response = status;
        return grpc::Status::OK;
    }

    grpc::Status SetSlotProtection(grpc::ServerContext*,
                                   const beebium::SetSlotProtectionRequest* request,
                                   beebium::SetSlotProtectionResponse* response) override {
        std::lock_guard lock(status_mutex);
        last_group_id = request->group_id();
        last_kind = request->kind();
        last_engaged = request->engaged();
        if (!refusal.empty()) {
            response->set_success(false);
            response->set_error(refusal);
            return grpc::Status::OK;
        }
        for (auto& group : *status.mutable_protection_groups()) {
            if (group.id() == request->group_id()) {
                if (request->kind() == beebium::SIDEWAYS_PROTECTION_KIND_HIDE) {
                    group.set_hidden(request->engaged());
                } else {
                    group.set_write_protected(request->engaged());
                }
                response->set_success(true);
                response->set_engaged(request->engaged());
                return grpc::Status::OK;
            }
        }
        response->set_success(false);
        response->set_error("unknown protection group");
        return grpc::Status::OK;
    }

    grpc::Status SubscribeEvents(grpc::ServerContext* context,
                                 const beebium::SubscribeEventsRequest* request,
                                 grpc::ServerWriter<beebium::SidewaysEvent>* writer) override {
        last_monitor_header_changes = request->monitor_header_changes();
        ++watchers;
        for (;;) {
            std::unique_lock lock(mutex_);
            changed_.wait_for(lock, 20ms, [this] { return !script_.empty(); });
            if (context->IsCancelled()) {
                --watchers;
                return grpc::Status::CANCELLED;
            }
            if (script_.empty()) {
                continue;
            }
            const beebium::SidewaysEvent next = script_.front();
            script_.pop_front();
            lock.unlock();
            writer->Write(next);
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<beebium::SidewaysEvent> script_;
};

// ExtensionUiService: a current view per extension, sent on subscription
// and again on each push; Dispatch accepted only at the current revision.
class ScriptedExtensionUiService final : public beebium::ExtensionUiService::Service {
public:
    // Set (or replace) an extension's view; subscribers receive it.
    void push(const beebium::View& view) {
        {
            std::lock_guard lock(mutex_);
            current_[view.extension_id()] = view;
            ++generation_[view.extension_id()];
            ending_.erase(view.extension_id());
        }
        changed_.notify_all();
    }
    // End every subscription to an extension with this status.
    void end(const std::string& extension_id, grpc::StatusCode code) {
        {
            std::lock_guard lock(mutex_);
            ending_[extension_id] = code;
            current_.erase(extension_id);
        }
        changed_.notify_all();
    }
    int subscribers(const std::string& extension_id) {
        std::lock_guard lock(mutex_);
        return subscribers_[extension_id];
    }
    std::vector<beebium::DispatchRequest> dispatched() {
        std::lock_guard lock(mutex_);
        return dispatched_;
    }

    grpc::Status SubscribeView(grpc::ServerContext* context,
                               const beebium::SubscribeViewRequest* request,
                               grpc::ServerWriter<beebium::View>* writer) override {
        const std::string id = request->extension_id();
        std::unique_lock lock(mutex_);
        if (!current_.count(id)) {
            return {grpc::StatusCode::NOT_FOUND, "no such extension"};
        }
        ++subscribers_[id];
        std::uint64_t sent = 0;
        for (;;) {
            if (ending_.count(id)) {
                --subscribers_[id];
                return {ending_[id], "ended by the fake"};
            }
            if (current_.count(id) && generation_[id] != sent) {
                sent = generation_[id];
                const beebium::View view = current_[id];
                lock.unlock();
                writer->Write(view);
                lock.lock();
                continue;
            }
            changed_.wait_for(lock, 20ms);
            if (context->IsCancelled()) {
                --subscribers_[id];
                return grpc::Status::CANCELLED;
            }
        }
    }

    grpc::Status Dispatch(grpc::ServerContext*, const beebium::DispatchRequest* request,
                          beebium::DispatchResponse* response) override {
        std::lock_guard lock(mutex_);
        dispatched_.push_back(*request);
        const auto found = current_.find(request->extension_id());
        if (found == current_.end()) {
            response->set_error("unknown extension");
        } else if (found->second.view_revision() != request->view_revision()) {
            response->set_error("stale view revision");
        } else {
            response->set_accepted(true);
        }
        return grpc::Status::OK;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::map<std::string, beebium::View> current_;
    std::map<std::string, std::uint64_t> generation_;
    std::map<std::string, grpc::StatusCode> ending_;
    std::map<std::string, int> subscribers_;
    std::vector<beebium::DispatchRequest> dispatched_;
};

struct FakeServer {
    ScriptedSystemService system;
    ScriptedExtensionUiService extension_ui;
    ScriptedSidewaysService sideways;
    QuietVideoService video;
    KeyTableService keyboard;
    BlockedDebuggerService debugger;
    ScriptedAudioService audio;
    ScriptedDiscService disc;
    ScriptedIndicatorService indicators;
    int port = 0;
    std::unique_ptr<grpc::Server> server;

    FakeServer() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&system);
        builder.RegisterService(&video);
        builder.RegisterService(&keyboard);
        builder.RegisterService(&debugger);
        builder.RegisterService(&audio);
        builder.RegisterService(&disc);
        builder.RegisterService(&indicators);
        builder.RegisterService(&sideways);
        builder.RegisterService(&extension_ui);
        server = builder.BuildAndStart();
    }
    ~FakeServer() { server->Shutdown(std::chrono::system_clock::now() + 1s); }
    QString endpoint() const { return QStringLiteral("127.0.0.1:%1").arg(port); }
};

inline beebium::ServerStatusEvent event(beebium::ServerStatusType type,
                                        bool guest_ready = false) {
    beebium::ServerStatusEvent e;
    e.set_status(type);
    e.set_guest_ready(guest_ready);
    return e;
}

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_FAKE_BEEBIUM_SERVER_HPP
