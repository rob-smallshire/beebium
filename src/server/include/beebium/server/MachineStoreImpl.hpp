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

// Where `start` keeps the machine it serves on disk: the server's side of
// SystemService.SaveMachine and ResetBattery, and what a graceful shutdown
// keeps. The one place a running machine's directory changes.

#pragma once

#include "beebium/FrameBuffer.hpp"
#include "beebium/disc/DiscUrl.hpp"
#include "beebium/extension/ExtensionStorage.hpp"
#include "beebium/server/FramePng.hpp"
#include "beebium/server/MachineAssembly.hpp"
#include "beebium/server/MachineDirectory.hpp"
#include "beebium/server/MachineDocument.hpp"
#include "beebium/server/MachineInstantiation.hpp"
#include "beebium/server/NvramStore.hpp"
#include "beebium/service/MachineStore.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace beebium::server {

namespace machine_store_detail {

// `filepath` within `dirpath`, relative to it, or nullopt when it is not
// inside. Both are compared as the filesystem has them (/var is /private/var
// on macOS).
inline std::optional<std::filesystem::path> relative_within(const std::filesystem::path& filepath,
                                                           const std::filesystem::path& dirpath) {
    std::error_code ec;
    const auto file = std::filesystem::weakly_canonical(filepath, ec);
    const auto dir = std::filesystem::weakly_canonical(dirpath, ec);
    const auto relative = file.lexically_relative(dir);
    if (relative.empty() || *relative.begin() == "..") return std::nullopt;
    return relative;
}

// Every regular file under `dirpath`, but its lock, with its size and time.
using FileStamps = std::map<std::filesystem::path, std::pair<std::uintmax_t,
                                                             std::filesystem::file_time_type>>;
inline FileStamps file_stamps(const std::filesystem::path& dirpath) {
    FileStamps stamps;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dirpath, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().filename() == machine_layout::LOCK_FILENAME) {
            continue;
        }
        stamps[it->path()] = {it->file_size(ec), it->last_write_time(ec)};
    }
    return stamps;
}

}  // namespace machine_store_detail

template<typename MachineType>
class ServedMachineStore final : public beebium::service::MachineStore {
public:
    // The machine `am` assembled from `config`, whose screen `frame_buffer`
    // holds. `recorded_document` is the preset.json a Save of a machine not
    // yet saved writes: recorded before assembly, which changes the
    // configuration (machine_preset_document); nullopt for a saved machine.
    ServedMachineStore(AssembledMachine<MachineType>& am, ServerConfig<MachineType>& config,
                       const FrameBuffer& frame_buffer,
                       std::optional<nlohmann::ordered_json> recorded_document)
        : am_(am),
          config_(config),
          frame_buffer_(frame_buffer),
          recorded_document_(std::move(recorded_document)) {
        // A transient directory holds nothing but the copies of the media its
        // template named, as assembly left them.
        if (am_.machine_directory->transient()) {
            template_stamps_ = machine_store_detail::file_stamps(am_.machine_directory->dirpath());
        }
    }

    std::string machine_dirpath() const override {
        std::lock_guard lock(mutex_);
        return am_.machine_directory->dirpath().string();
    }

    bool machine_saved() const override {
        std::lock_guard lock(mutex_);
        return !am_.machine_directory->transient();
    }

    bool template_media_written() const override {
        std::lock_guard lock(mutex_);
        if (!am_.machine_directory->transient()) return false;
        return machine_store_detail::file_stamps(am_.machine_directory->dirpath()) !=
               template_stamps_;
    }

    SaveOutcome save(const std::string& destination_dirpath, bool replace_existing,
                     const std::string& machine_uuid, const std::string& name_template) override {
        std::lock_guard lock(mutex_);
        SaveOutcome outcome;
        am_.machine.with_emulation_paused([&] {
            try {
                outcome = save_paused(destination_dirpath, replace_existing, machine_uuid,
                                      name_template);
            } catch (const std::exception& e) {
                outcome.error = SaveOutcome::Error::Internal;
                outcome.message = e.what();
            }
        });
        return outcome;
    }

    std::vector<std::string> reset_battery() override {
        std::lock_guard lock(mutex_);
        battery_reset_ = true;
        std::vector<std::string> units;
        for (auto* unit : am_.machine.battery_units()) units.emplace_back(unit->name());
        for_each_extension_battery(am_, [&](const auto& extension_units, const auto& dirpath) {
            const auto id = dirpath.parent_path().filename().string();
            for (auto* unit : extension_units) units.push_back(id + "/" + std::string(unit->name()));
        });
        if (!am_.machine_directory->transient()) {
            for (const auto& dirpath : battery_dirpaths(am_.machine_directory->dirpath())) {
                std::error_code ec;
                std::filesystem::remove_all(dirpath, ec);
                if (ec) {
                    std::cerr << "Warning: cannot forget " << dirpath.string() << ": "
                              << ec.message() << "\n";
                }
            }
        }
        return units;
    }

    void renamed(const std::string& name_template) override {
        std::lock_guard lock(mutex_);
        if (am_.machine_directory->transient()) return;
        const auto& dirpath = am_.machine_directory->dirpath();
        std::string error;
        auto record = read_machine_record(dirpath, error);
        if (!record) {
            std::cerr << "Warning: cannot record the machine's new name: " << error << "\n";
            return;
        }
        record->name = name_template;
        try {
            write_machine_record(dirpath, *record);
            write_machine_document_files(dirpath, *record, machine_display_name(*record, dirpath));
        } catch (const std::exception& e) {
            std::cerr << "Warning: cannot record the machine's new name: " << e.what() << "\n";
        }
    }

    // What a graceful shutdown keeps of a saved machine: its battery-backed
    // state and its last screen. Call with emulation stopped, before the
    // server stops. Throws when the state cannot be kept.
    void keep_at_shutdown() {
        std::lock_guard lock(mutex_);
        if (am_.machine_directory->transient()) return;
        keep_state(am_.machine_directory->dirpath());
    }

private:
    using Error = SaveOutcome::Error;

    // The battery-backed state, unless the battery was reset this run, and
    // the screen, into the machine directory `dirpath`.
    void keep_state(const std::filesystem::path& dirpath) {
        if (!battery_reset_) {
            save_battery_units(am_.machine.battery_units(), dirpath / machine_layout::NVRAM_DIRNAME);
            const auto& home = am_.machine_directory->dirpath();
            for_each_extension_battery(am_, [&](const auto& units, const auto& unit_dirpath) {
                const auto relative = unit_dirpath.lexically_relative(home);
                save_battery_units(units, dirpath / relative);
            });
        }
        write_frame_png(frame_buffer_, dirpath / machine_layout::THUMBNAIL_FILENAME,
                        kThumbnailBorderPixels, /*crop_auto=*/false);
    }

    SaveOutcome save_paused(const std::string& destination, bool replace_existing,
                            const std::string& machine_uuid, const std::string& name_template) {
        flush_disc_drives(am_.machine);
        const bool saved = !am_.machine_directory->transient();
        const auto home = am_.machine_directory->dirpath();
        if (destination.empty()) {
            if (!saved) {
                return {Error::InvalidArgument,
                        "a machine not yet saved is saved into a new directory: give a "
                        "destination", {}, {}};
            }
            save_in_place(home, name_template);
            return {Error::None, {}, home.string(), machine_uuid};
        }

        const auto target = std::filesystem::absolute(destination).lexically_normal();
        std::error_code ec;
        const bool replacing = std::filesystem::exists(target, ec);
        if (replacing) {
            if (!replace_existing) {
                return {Error::AlreadyExists,
                        target.string() +
                            " already exists; a machine is saved into a new directory, or "
                            "replaces a saved machine",
                        {}, {}};
            }
            if (auto why = why_not_replaceable(target, home)) {
                return {Error::FailedPrecondition, *why, {}, {}};
            }
        }
        // A copy saved as another machine has an identity of its own.
        const auto uuid = saved ? generate_uuid_v4() : machine_uuid;
        save_as(home, target, saved, uuid, name_template, replacing);
        return {Error::None, {}, target.string(), uuid};
    }

    void save_in_place(const std::filesystem::path& home, const std::string& name_template) {
        keep_state(home);
        std::string error;
        auto record = read_machine_record(home, error);
        if (!record) throw std::runtime_error(error);
        record->name = name_template;
        write_machine_record(home, *record);
        write_machine_document_files(home, *record, machine_display_name(*record, home));
    }

    // Why the existing `target` cannot be replaced by the machine at `home`,
    // or nullopt when it can: it must be a saved machine, not this one, that
    // no process runs.
    static std::optional<std::string> why_not_replaceable(const std::filesystem::path& target,
                                                          const std::filesystem::path& home) {
        if (!is_machine_directory(target)) {
            return target.string() + " is not a saved machine, so it is not replaced";
        }
        std::error_code ec;
        if (std::filesystem::equivalent(target, home, ec)) {
            return target.string() + " is this machine's own directory; save it in place";
        }
        try {
            if (auto holder = machine_lock_holder(target)) {
                return target.string() + " is in use by process " +
                       (holder->empty() ? "?" : *holder);
            }
        } catch (const std::exception& e) {
            return std::string("cannot tell whether ") + target.string() +
                   " is in use: " + e.what();
        }
        return std::nullopt;
    }

    // Makes the machine at `target` from the one at `home`, moves the running
    // machine onto it, and lets `home` go: removed when it was transient,
    // left (unlocked) when it was saved. When `replacing`, the saved machine
    // at `target` is put aside once the new one is whole, removed once the
    // running machine is on the new one, and put back on a failure.
    void save_as(const std::filesystem::path& home, const std::filesystem::path& target,
                 bool saved, const std::string& uuid, const std::string& name_template,
                 bool replacing) {
        // Made beside the target, so it appears there whole or not at all.
        std::random_device rd;
        const auto suffix = std::to_string(rd());
        const auto partial =
            target.parent_path() / (target.filename().string() + ".partial-" + suffix);
        std::optional<std::filesystem::path> aside;
        bool at_target = false;
        try {
            std::filesystem::create_directories(partial);
            copy_machine_files(home, partial);
            if (!saved) write_preset(partial);
            MachineRecord record;
            if (saved) {
                std::string error;
                auto kept = read_machine_record(home, error);
                if (!kept) throw std::runtime_error(error);
                record = *kept;
                record.created = iso8601_utc_now();
            } else {
                record = new_machine_record(config_, uuid, name_template);
            }
            record.uuid = uuid;
            record.name = name_template;
            record.last_used = iso8601_utc_now();
            write_machine_record(partial, record);
            keep_state(partial);
            if (replacing) {
                aside = target.parent_path() / (target.filename().string() + ".replaced-" + suffix);
                clear_directory_readonly(target);
                std::filesystem::rename(target, *aside);
            }
            std::filesystem::rename(partial, target);
            at_target = true;

            // From here the machine is at `target`, held before its media move
            // there.
            auto directory = MachineDirectory::open(target);
            try {
                repoint_media(home, target);
            } catch (...) {
                // Back onto `home`, which still holds every medium.
                try {
                    repoint_media(target, home);
                } catch (const std::exception& e) {
                    std::cerr << "Warning: " << e.what() << "\n";
                }
                throw;
            }
            am_.machine_directory.reset();  // releases `home`; removes it if transient
            am_.machine_directory.emplace(std::move(directory));
            config_.launch.machine_dirpath = target;
            template_stamps_.clear();
            try {
                write_machine_document_files(target, record,
                                             machine_display_name(record, target));
            } catch (const std::exception& e) {
                std::cerr << "Warning: cannot write the machine's document file and folder "
                             "metadata: "
                          << e.what() << "\n";
            }
        } catch (...) {
            std::error_code ec;
            const auto& made = at_target ? target : partial;
            clear_directory_readonly(made);
            std::filesystem::remove_all(made, ec);
            if (aside) std::filesystem::rename(*aside, target, ec);
            throw;
        }
        if (aside) {
            std::error_code ec;
            clear_directory_readonly(*aside);
            std::filesystem::remove_all(*aside, ec);
            if (ec) {
                std::cerr << "Warning: cannot remove the machine replaced, kept at "
                          << aside->string() << ": " << ec.message() << "\n";
            }
        }
    }

    // Copies the machine's own files from `home` into `partial`: everything
    // but its lock, its document file and folder metadata (written for the
    // new directory) and its battery-backed state (written afresh).
    static void copy_machine_files(const std::filesystem::path& home,
                                   const std::filesystem::path& partial) {
        for (const auto& entry : std::filesystem::directory_iterator(home)) {
            const auto name = entry.path().filename();
            if (name == machine_layout::LOCK_FILENAME ||
                name == machine_layout::DESKTOP_INI_FILENAME ||
                name == machine_layout::KDE_DIRECTORY_FILENAME ||
                name == machine_layout::NVRAM_DIRNAME ||
                name.extension() == machine_layout::DOCUMENT_SUFFIX) {
                continue;
            }
            std::filesystem::copy(entry.path(), partial / name,
                                  std::filesystem::copy_options::recursive);
        }
        // Extensions keep more than their battery state in extensions/<id>/;
        // only the nvram/ beneath is written afresh.
        std::error_code ec;
        for (const auto& extension : std::filesystem::directory_iterator(
                 partial / machine_layout::EXTENSIONS_DIRNAME, ec)) {
            std::filesystem::remove_all(extension.path() / machine_layout::NVRAM_DIRNAME, ec);
        }
    }

    // The preset.json of a machine saved for the first time, and every file
    // its preset carries that the machine has no copy of yet (a copy it has
    // holds the guest's writes).
    void write_preset(const std::filesystem::path& partial) {
        if (!recorded_document_) {
            throw std::runtime_error("the machine's configuration was not recorded at launch");
        }
        write_text_atomically(partial / PresetSource::DOCUMENT_FILENAME,
                              recorded_document_->dump(2) + "\n");
        for (const auto& member : carried_preset_members(config_, /*with_thumbnail=*/false)) {
            const auto filepath = partial / std::filesystem::path(member.name);
            if (std::filesystem::exists(filepath)) continue;
            std::filesystem::create_directories(filepath.parent_path());
            write_text_atomically(
                filepath, std::string_view(reinterpret_cast<const char*>(member.data.data()),
                                           member.data.size()));
        }
    }

    // Points every medium open in `home` at its copy in `target`: the
    // floppies, and every storage device an extension has there. Throws,
    // naming the device, when one cannot be moved.
    void repoint_media(const std::filesystem::path& home, const std::filesystem::path& target) {
        using Memory = typename MachineType::Memory;
        if constexpr (beebium::HasDiscDrives<Memory>) {
            auto& memory = am_.machine.state().memory;
            for (auto* drive : {&memory.disc_drive_0, &memory.disc_drive_1}) {
                if (!drive->disc()) continue;
                const auto relative = machine_store_detail::relative_within(
                    drive->disc()->source_filepath(), home);
                if (!relative) continue;
                const auto filepath = target / *relative;
                drive->reopen_at(filepath, beebium::DiscUrl::from_filepath(filepath).url());
            }
        }
        for (auto* ext : am_.extension_registry.extensions()) {
            auto* storage = ext->storage();
            if (!storage) continue;
            for (const auto& device : storage->devices()) {
                if (device.backing_path.empty()) continue;
                const auto relative =
                    machine_store_detail::relative_within(device.backing_path, home);
                if (!relative) continue;
                std::string error;
                if (!storage->reopen(device.id, target / *relative, error)) {
                    throw std::runtime_error("cannot move " + device.id + " to " +
                                             (target / *relative).string() + ": " + error);
                }
            }
        }
    }

    // As preset thumbnails are captured (capture-screenshot's default).
    static constexpr std::uint32_t kThumbnailBorderPixels = 20;

    AssembledMachine<MachineType>& am_;
    ServerConfig<MachineType>& config_;
    const FrameBuffer& frame_buffer_;
    const std::optional<nlohmann::ordered_json> recorded_document_;
    mutable std::mutex mutex_;
    machine_store_detail::FileStamps template_stamps_;
    bool battery_reset_ = false;
};

}  // namespace beebium::server
