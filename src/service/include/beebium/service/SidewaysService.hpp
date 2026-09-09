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

#ifndef BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
#define BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP

#include "sideways.grpc.pb.h"
#include "beebium/SlotTopology.hpp"
#include "beebium/SidewaysRomHeader.hpp"
#include "beebium/devices/ConfigurableSlot.hpp"

#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace beebium::service {

// Memory has a sideways memory device. The peek_bank/select_bank surface
// of that device is used directly by ReadSlotData and the live header
// scanner.
template<typename T>
concept HasSideways = requires(T t) {
    { t.sideways } -> std::convertible_to<typename T::SidewaysType&>;
    { T::SidewaysType::has_aliasing } -> std::convertible_to<bool>;
};

// Memory exposes the uniform per-slot status accessor. Every machine
// variant with sideways memory implements this; SidewaysService uses
// it as the single source of truth for type / populated / image_name.
template<typename T>
concept HasSlotInfo = requires(const T t) {
    { t.slot_info(uint8_t{0}) } -> std::convertible_to<beebium::SlotInfo>;
};

// Memory exposes the uniform per-slot mutator surface used by
// ConfigureSlot to apply runtime changes.
template<typename T>
concept HasSlotMutators = requires(T t,
                                   const uint8_t* p,
                                   std::size_t n,
                                   std::string_view name) {
    { t.configure_slot_as_ram(uint8_t{0}) };
    { t.configure_slot_as_empty(uint8_t{0}) };
    { t.load_sideways_rom(uint8_t{0}, p, n, name) };
};

// Helper to convert SlotType to protobuf enum
inline beebium::SidewaysSlotType slot_type_to_proto(beebium::SlotType type) {
    switch (type) {
        case beebium::SlotType::Empty: return beebium::SIDEWAYS_SLOT_TYPE_EMPTY;
        case beebium::SlotType::Rom: return beebium::SIDEWAYS_SLOT_TYPE_ROM;
        case beebium::SlotType::Ram: return beebium::SIDEWAYS_SLOT_TYPE_RAM;
        default: return beebium::SIDEWAYS_SLOT_TYPE_EMPTY;
    }
}

// Populate proto SocketCapabilities from a SocketSpec (topology source of truth).
inline void fill_capabilities(beebium::SocketCapabilities* caps,
                              const beebium::SocketSpec& spec) {
    caps->set_supports_rom(spec.supports_rom);
    caps->set_supports_ram(spec.supports_ram);
    caps->set_supports_empty(spec.supports_empty);
    caps->set_runtime_configurable(spec.runtime_configurable);
}

// Helper to convert protobuf enum to SlotType
inline beebium::SlotType proto_to_slot_type(beebium::SidewaysSlotType proto_type) {
    switch (proto_type) {
        case beebium::SIDEWAYS_SLOT_TYPE_EMPTY: return beebium::SlotType::Empty;
        case beebium::SIDEWAYS_SLOT_TYPE_ROM: return beebium::SlotType::Rom;
        case beebium::SIDEWAYS_SLOT_TYPE_RAM: return beebium::SlotType::Ram;
        default: return beebium::SlotType::Empty;
    }
}

// gRPC service implementation for SidewaysService
template<typename MachineType>
class SidewaysServiceImpl final : public SidewaysService::Service {
public:
    using Memory = typename MachineType::Memory;
    using MotherboardLinks = typename Memory::MotherboardLinks;

    explicit SidewaysServiceImpl(MachineType& machine)
        : machine_(machine)
    {
        // Spin up the live header scanner. The tick is cheap when no
        // subscriber has asked for it; the thread runs for the service's
        // lifetime so subscribe/unsubscribe doesn't have to start/stop
        // threads. See docs/discussion/sideways-live-header-updates.md.
        if constexpr (HasSideways<Memory>) {
            scanner_thread_ = std::thread([this] { run_header_scanner(); });
        }
    }

    ~SidewaysServiceImpl() override {
        scanner_running_.store(false);
        if (scanner_thread_.joinable()) {
            scanner_thread_.join();
        }
    }

    // Non-copyable
    SidewaysServiceImpl(const SidewaysServiceImpl&) = delete;
    SidewaysServiceImpl& operator=(const SidewaysServiceImpl&) = delete;

    // Capture the motherboard link state the server was configured with at
    // start-up so GetSlotStatus reports the correct topology and link list
    // to clients. Motherboard links are not runtime-mutable; this is a
    // one-shot setter called by the server bootstrap code.
    void set_motherboard_links(const MotherboardLinks& links) {
        std::lock_guard<std::mutex> lock(mutex_);
        motherboard_links_ = links;
    }

    grpc::Status GetSlotStatus(
        grpc::ServerContext* context,
        const GetSlotStatusRequest* request,
        GetSlotStatusResponse* response) override
    {
        (void)context;
        (void)request;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        // Topology comes from the machine variant when available, and is
        // the source of truth for socket labels, alias sets, and capability
        // flags. The link-aware overload is used so machines like the
        // Model B+ (where S13 changes which slots IC71 owns) report what
        // the user actually asked for at startup. The runtime per-slot
        // type/image_name fields are read from the live device when
        // accessible.
        if constexpr (requires { Memory::slot_topology(motherboard_links_); }) {
            auto topo = Memory::slot_topology(motherboard_links_);

            // Report the link state itself so clients can display it.
            for (const auto& info : motherboard_links_.describe()) {
                auto* link = response->add_motherboard_links();
                link->set_name(info.name);
                link->set_value(info.value);
                link->set_description(info.description);
            }

            response->set_has_aliasing(topo.has_aliasing);
            response->set_num_physical_slots(
                static_cast<uint32_t>(topo.sockets.size()));

            for (const auto& spec : topo.sockets) {
                auto* socket_status = response->add_sockets();
                socket_status->set_socket_index(spec.socket_index);
                socket_status->set_socket_label(spec.label);
                for (int slot : spec.slots) {
                    socket_status->add_aliased_slots(static_cast<uint32_t>(slot));
                }
                fill_capabilities(
                    socket_status->mutable_capabilities(), spec);

                // Peek the slot's 16 KiB and run the standard
                // sideways-ROM-header parser. An empty socket or a
                // non-standard image produces recognised=false. The parse
                // result drives both rom_header and (in the fallback
                // branch below) type/populated for machines that don't
                // expose per-socket runtime accessors.
                beebium::SidewaysRomHeader parsed;
                if constexpr (HasSideways<Memory>) {
                    if (!spec.slots.empty()) {
                        auto& sw = machine_.state().memory.sideways;
                        const auto probe = static_cast<uint8_t>(spec.slots[0]);
                        // Copy the 16 KiB bank with the emulation thread parked
                        // (as the header scanner does); the CPU may be writing
                        // this RAM bank. Parse the private copy outside the pause.
                        std::vector<uint8_t> bytes(16384);
                        machine_.with_emulation_paused([&] {
                            for (uint16_t i = 0; i < 16384; ++i) {
                                bytes[i] = sw.peek_bank(probe, i);
                            }
                        });
                        parsed = beebium::parse_sideways_rom_header(bytes);
                    }
                }

                // Runtime fields: ask the memory once for the unified
                // SlotInfo. Every machine variant implements this in
                // whatever way matches its layout (Model B reflects the
                // physical socket through 4-way aliasing; the ROM/RAM
                // board has a per-slot ConfigurableSlot; the B+ family
                // assembles from user sockets + S13-routed BASIC/SRAM).
                if constexpr (HasSlotInfo<Memory>) {
                    const auto info = machine_.state().memory.slot_info(
                        static_cast<uint8_t>(spec.slots[0]));
                    socket_status->set_type(slot_type_to_proto(info.type));
                    socket_status->set_populated(info.populated);
                    socket_status->set_image_name(info.image_name);
                }

                if (parsed.recognised) {
                    auto* h = socket_status->mutable_rom_header();
                    h->set_recognised(true);
                    h->set_title(parsed.title);
                    h->set_version(parsed.version);
                    h->set_copyright(parsed.copyright);
                    h->set_contains_romfs(parsed.contains_romfs);
                    if (parsed.has_language_entry) h->add_kinds("language");
                    if (parsed.has_service_entry) h->add_kinds("service");
                    if (parsed.contains_romfs) h->add_kinds("romfs");
                }
            }
            return grpc::Status::OK;
        } else if constexpr (!HasSideways<Memory>) {
            // Machine has no sideways memory and no topology.
            response->set_has_aliasing(false);
            response->set_num_physical_slots(0);
            return grpc::Status::OK;
        } else {
            // Should be unreachable: any machine with sideways now also
            // provides slot_topology().
            return grpc::Status::OK;
        }
    }

    grpc::Status ConfigureSlot(
        grpc::ServerContext* context,
        const ConfigureSlotRequest* request,
        ConfigureSlotResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasSideways<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no configurable sideways memory");
            return grpc::Status::OK;
        } else {
            uint32_t slot_num = request->slot();
            if (slot_num > 15) {
                response->set_success(false);
                response->set_error("Invalid slot number (must be 0-15)");
                return grpc::Status::OK;
            }
            uint8_t slot = static_cast<uint8_t>(slot_num);

            // Reject runtime configuration of sockets the topology marks as
            // not runtime_configurable. On real hardware (Model B, Model B+,
            // Master 128 internal sockets) this is a power-off chip-swap
            // operation; only the fantasy ROM/RAM expansion board and
            // future cartridge slots support hot reconfiguration.
            if constexpr (requires { Memory::slot_topology(motherboard_links_); }) {
                auto topo = Memory::slot_topology(motherboard_links_);
                const auto* spec = topo.find_socket_for_slot(
                    static_cast<int>(slot));
                if (spec == nullptr) {
                    response->set_success(false);
                    response->set_error(
                        "Slot " + std::to_string(slot)
                        + " does not exist on this machine variant");
                    return grpc::Status::OK;
                }
                if (!spec->runtime_configurable) {
                    response->set_success(false);
                    response->set_error(
                        "Socket " + spec->label
                        + " is not runtime-reconfigurable on this machine "
                          "variant; configure at startup with --sideways");
                    return grpc::Status::OK;
                }
            }

            auto& memory = machine_.state().memory;
            beebium::SlotType new_type = proto_to_slot_type(request->type());

            // Report the physical socket index the request resolved to.
            // The topology already knows: it's the socket that owns this
            // slot. Reporting socket_index (rather than slot number) lets
            // clients see when two slots aliased to the same socket.
            uint8_t actual_socket = slot;
            if constexpr (requires { Memory::slot_topology(motherboard_links_); }) {
                auto topo = Memory::slot_topology(motherboard_links_);
                if (const auto* sp = topo.find_socket_for_slot(
                        static_cast<int>(slot))) {
                    actual_socket = static_cast<uint8_t>(sp->socket_index);
                }
            }
            response->set_actual_socket(actual_socket);

            // Gather the image bytes first, off the emulation thread: file I/O
            // must not run inside the quiesce below (a slow disk read would
            // stall the emulator). Validation and errors happen here too.
            std::vector<uint8_t> image;
            bool have_image = false;
            std::string image_filepath;
            if (request->has_url()) {
                std::string url = request->url();
                image_filepath = (url.rfind("file://", 0) == 0)
                    ? url.substr(7) : url;

                std::ifstream file(image_filepath, std::ios::binary | std::ios::ate);
                if (!file) {
                    response->set_success(false);
                    response->set_error("Failed to open file: " + image_filepath);
                    return grpc::Status::OK;
                }

                auto size = file.tellg();
                if (size > 16384) {
                    response->set_success(false);
                    response->set_error("Image too large (max 16384 bytes)");
                    return grpc::Status::OK;
                }

                file.seekg(0, std::ios::beg);
                image.resize(static_cast<size_t>(size));
                file.read(reinterpret_cast<char*>(image.data()), size);
                have_image = true;

                // Store the full filepath as image_name so the Memory
                // sidebar's Copy Path / Reveal in Finder actions have
                // something useful; clients take the basename for display.
                response->set_image_name(
                    std::filesystem::path(image_filepath).filename().string());
            } else if (request->has_data()) {
                const std::string& data = request->data();
                if (data.size() > 16384) {
                    response->set_success(false);
                    response->set_error("Image too large (max 16384 bytes)");
                    return grpc::Status::OK;
                }
                image.assign(data.begin(), data.end());
                have_image = true;
            }

            // Retype the slot and load its image as one atomic step with the
            // emulation thread parked: it fetches instructions and reads data
            // from this bank every cycle, so it must never observe a
            // half-reconfigured slot or a bank being rewritten under it.
            machine_.with_emulation_paused([&] {
                if constexpr (HasSlotMutators<Memory>) {
                    if (new_type == beebium::SlotType::Ram) {
                        memory.configure_slot_as_ram(slot);
                    } else if (new_type == beebium::SlotType::Empty) {
                        memory.configure_slot_as_empty(slot);
                    }
                    // SlotType::Rom is configured by load_sideways_rom below.

                    if (have_image) {
                        if (new_type == beebium::SlotType::Ram) {
                            memory.load_sideways_data(
                                slot, image.data(), image.size(), image_filepath);
                        } else {
                            memory.load_sideways_rom(
                                slot, image.data(), image.size(), image_filepath);
                        }
                    }
                }
            });

            response->set_success(true);
            return grpc::Status::OK;
        }
    }

    grpc::Status ReadSlotData(
        grpc::ServerContext* context,
        const ReadSlotDataRequest* request,
        ReadSlotDataResponse* response) override
    {
        (void)context;
        std::lock_guard<std::mutex> lock(mutex_);

        using Memory = typename MachineType::Memory;

        if constexpr (!HasSideways<Memory>) {
            response->set_success(false);
            response->set_error("Machine has no sideways memory");
            return grpc::Status::OK;
        } else {
            uint32_t slot_num = request->slot();
            uint32_t offset = request->offset();
            uint32_t length = request->length();

            if (slot_num > 15) {
                response->set_success(false);
                response->set_error("Invalid slot number (must be 0-15)");
                return grpc::Status::OK;
            }
            uint8_t slot = static_cast<uint8_t>(slot_num);

            if (offset > 16383) {
                response->set_success(false);
                response->set_error("Invalid offset (must be 0-16383)");
                return grpc::Status::OK;
            }

            if (length == 0) {
                length = 16384 - offset;  // Read to end of slot
            }

            if (offset + length > 16384) {
                length = 16384 - offset;  // Clamp to slot boundary
            }

            auto& sideways = machine_.state().memory.sideways;

            // Read the requested span with the emulation thread parked; the CPU
            // may be writing this RAM bank (see the header scanner).
            std::string data;
            data.reserve(length);
            machine_.with_emulation_paused([&] {
                for (uint32_t i = 0; i < length; ++i) {
                    data.push_back(static_cast<char>(
                        sideways.peek_bank(slot, static_cast<uint16_t>(offset + i))));
                }
            });

            response->set_success(true);
            response->set_data(std::move(data));
            if constexpr (HasSlotInfo<Memory>) {
                response->set_type(slot_type_to_proto(
                    machine_.state().memory.slot_info(slot).type));
            } else {
                response->set_type(beebium::SIDEWAYS_SLOT_TYPE_ROM);
            }

            return grpc::Status::OK;
        }
    }

    grpc::Status SubscribeEvents(
        grpc::ServerContext* context,
        const SubscribeEventsRequest* request,
        grpc::ServerWriter<SidewaysEvent>* writer) override
    {
        if constexpr (!HasSideways<Memory>) {
            (void)request;
            (void)writer;
            return grpc::Status::OK;
        } else {
            auto sub = std::make_shared<HeaderSubscriber>();
            sub->writer = writer;
            sub->monitor_headers = request->monitor_header_changes();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                subscribers_.push_back(sub);
            }

            // Block here until the RPC is cancelled. The scanner thread
            // writes to `sub->writer` under `mutex_`; we drop the
            // subscriber from the list under the same lock once the
            // stream is closing so the scanner can never touch a stale
            // writer.
            while (!context->IsCancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                subscribers_.erase(
                    std::remove(subscribers_.begin(), subscribers_.end(), sub),
                    subscribers_.end());
            }
            return grpc::Status::OK;
        }
    }

private:
    // One open SubscribeEvents stream. Captured under mutex_ so the
    // scanner can write to it concurrently with the RPC handler holding
    // the stream open.
    struct HeaderSubscriber {
        grpc::ServerWriter<SidewaysEvent>* writer = nullptr;
        bool monitor_headers = false;
    };

    // Per-slot scanner state. last_hash short-circuits the parse when
    // bytes are unchanged; the last_emitted_* fields are the value last
    // delivered to subscribers so we only emit on a real difference.
    struct SlotState {
        bool hash_valid = false;
        size_t last_hash = 0;
        bool emitted_recognised = false;
        std::string emitted_title;
        std::string emitted_version;
        std::string emitted_copyright;
        bool emitted_contains_romfs = false;
        std::vector<std::string> emitted_kinds;
    };

    // 1 Hz scanner: while any subscriber has opted into
    // monitor_header_changes, walk RAM-typed slots, hash-gate, parse,
    // and emit SlotHeaderChangedEvent when the parsed RomHeader for any
    // slot diverges from what we last broadcast.
    //
    // Threading: scanner_running_ is an atomic flag the destructor
    // clears; the thread polls it every 100 ms so shutdown is prompt.
    // Subscriber list and per-slot state are mutated under mutex_.
    // ServerWriter::Write is called with mutex_ held - that serialises
    // writes to any single subscriber (gRPC requires that) and also
    // serialises against subscribe/unsubscribe.
    void run_header_scanner() {
        scanner_running_.store(true);
        using namespace std::chrono_literals;
        while (scanner_running_.load()) {
            // Sleep in 100 ms slices so destruction never has to wait a
            // full second for the thread to wake.
            for (int slice = 0; slice < 10 && scanner_running_.load(); ++slice) {
                std::this_thread::sleep_for(100ms);
            }
            if (!scanner_running_.load()) break;
            try {
                scan_tick();
            } catch (...) {
                // The scanner must never take the service down. Swallow
                // any anomaly and retry on the next tick.
            }
        }
    }

    void scan_tick() {
        if constexpr (HasSideways<Memory>) {
            std::lock_guard<std::mutex> lock(mutex_);

            // Refcount gate: any subscriber actually wants this work?
            bool any_watcher = false;
            for (const auto& sub : subscribers_) {
                if (sub->monitor_headers) {
                    any_watcher = true;
                    break;
                }
            }
            if (!any_watcher) {
                return;
            }

            if constexpr (!requires { Memory::slot_topology(motherboard_links_); }) {
                return;
            } else {
                auto topo = Memory::slot_topology(motherboard_links_);
                auto& sw = machine_.state().memory.sideways;

                for (const auto& spec : topo.sockets) {
                    if (spec.slots.empty()) continue;
                    const uint8_t probe_slot =
                        static_cast<uint8_t>(spec.slots[0]);

                    // Skip non-RAM slots. ROM contents don't change between
                    // ConfigureSlot calls. The unified slot_info() accessor
                    // gives the live answer regardless of how the machine
                    // implements its sideways memory.
                    bool is_ram = false;
                    if constexpr (HasSlotInfo<Memory>) {
                        is_ram = machine_.state().memory.slot_info(probe_slot)
                                     .type == beebium::SlotType::Ram;
                    } else {
                        is_ram = spec.supports_ram && !spec.supports_rom
                                 && !spec.supports_empty;
                    }
                    if (!is_ram) continue;

                    scan_slot(sw, probe_slot);
                }
            }
        }
    }

    // Sample one RAM slot. Skip when bytes are unchanged (cheap hash
    // gate). On change, parse the full bank and emit if the resulting
    // RomHeader differs from the last value we broadcast for this slot.
    template<typename Sideways>
    void scan_slot(Sideways& sw, uint8_t slot) {
        if (slot >= slot_states_.size()) return;

        // Copy the whole 16 KiB bank with the emulation thread parked: this
        // runs on the scanner thread while the CPU may be writing that bank,
        // so an unsynchronised read would race and could tear. The quiesce is
        // scoped to the copy alone -- the hash, parse and emit below work on
        // the private copy off the emulation thread. Measured cost of the
        // quiesce+copy is ~0.6 microseconds per bank (release build,
        // ModelBRomRamBoard, emulation loop running; see the [bench] case in
        // tests/test_tier2_quiesce.cpp), incurred at most once a second per RAM
        // slot and only while a client is monitoring headers -- about one host
        // cycle's worth of pacing per second, negligible. (A change-driven
        // publish from the emulation side was rejected: it would burden the
        // instruction-fetch hot path to serve a rare, debug-facing observer.)
        std::vector<uint8_t> bytes(16384);
        machine_.with_emulation_paused([&] {
            for (uint16_t i = 0; i < 16384; ++i) {
                bytes[i] = sw.peek_bank(slot, i);
            }
        });

        const size_t h = std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(bytes.data()),
                             bytes.size())
        );

        auto& state = slot_states_[slot];
        if (state.hash_valid && state.last_hash == h) {
            return;
        }
        state.last_hash = h;
        state.hash_valid = true;

        const auto parsed = beebium::parse_sideways_rom_header(bytes);
        std::vector<std::string> parsed_kinds;
        if (parsed.has_language_entry) parsed_kinds.emplace_back("language");
        if (parsed.has_service_entry) parsed_kinds.emplace_back("service");
        if (parsed.contains_romfs) parsed_kinds.emplace_back("romfs");

        const bool same =
            state.emitted_recognised == parsed.recognised
            && state.emitted_title == parsed.title
            && state.emitted_version == parsed.version
            && state.emitted_copyright == parsed.copyright
            && state.emitted_contains_romfs == parsed.contains_romfs
            && state.emitted_kinds == parsed_kinds;
        if (same) {
            return;
        }

        state.emitted_recognised = parsed.recognised;
        state.emitted_title = parsed.title;
        state.emitted_version = parsed.version;
        state.emitted_copyright = parsed.copyright;
        state.emitted_contains_romfs = parsed.contains_romfs;
        state.emitted_kinds = parsed_kinds;

        SidewaysEvent event;
        event.set_timestamp_cycles(machine_.cycle_count());
        auto* slot_changed = event.mutable_slot_header_changed();
        slot_changed->set_slot(slot);
        if (parsed.recognised) {
            auto* h_proto = slot_changed->mutable_rom_header();
            h_proto->set_recognised(true);
            h_proto->set_title(parsed.title);
            h_proto->set_version(parsed.version);
            h_proto->set_copyright(parsed.copyright);
            h_proto->set_contains_romfs(parsed.contains_romfs);
            for (const auto& kind : parsed_kinds) {
                h_proto->add_kinds(kind);
            }
        }
        // recognised=false case: leave rom_header unset so clients see a
        // cleared header (RamHeader.recognised reads as false / has_field
        // returns false).

        for (const auto& sub : subscribers_) {
            if (sub->monitor_headers && sub->writer) {
                sub->writer->Write(event);
            }
        }
    }

    MachineType& machine_;
    std::mutex mutex_;
    MotherboardLinks motherboard_links_{};

    // Header scanner state. subscribers_ and slot_states_ are guarded
    // by mutex_; scanner_running_ is an atomic flag for the thread.
    std::vector<std::shared_ptr<HeaderSubscriber>> subscribers_;
    std::array<SlotState, 16> slot_states_{};
    std::atomic<bool> scanner_running_{false};
    std::thread scanner_thread_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_SIDEWAYS_SERVICE_HPP
