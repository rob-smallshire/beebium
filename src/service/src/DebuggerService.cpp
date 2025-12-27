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

#include "beebium/service/DebuggerService.hpp"
#include <sstream>
#include <iomanip>
#include <concepts>

namespace beebium::service {

// Concept to detect if a memory type has PC-aware access methods
template<typename T>
concept HasPcAwareMemory = requires(T& m, uint16_t addr, uint16_t pc, uint8_t val) {
    { m.read_with_pc(addr, pc) } -> std::same_as<uint8_t>;
    { m.write_with_pc(addr, val, pc) } -> std::same_as<void>;
};

// Helper to read with PC context when available, otherwise use regular read
template<typename Machine>
uint8_t read_with_optional_pc(Machine& machine, uint16_t addr, bool has_pc, uint16_t pc) {
    if constexpr (HasPcAwareMemory<decltype(machine.memory())>) {
        if (has_pc) {
            return machine.memory().read_with_pc(addr, pc);
        }
    }
    return machine.read(addr);
}

// Helper to peek with PC context when available, otherwise use regular peek
template<typename Machine>
uint8_t peek_with_optional_pc(Machine& machine, uint16_t addr, bool has_pc, uint16_t pc) {
    if constexpr (HasPcAwareMemory<decltype(machine.memory())>) {
        if (has_pc) {
            // For peek with PC, we use the PC-aware read (side-effect-free routing)
            return machine.memory().read_with_pc(addr, pc);
        }
    }
    return machine.peek(addr);
}

// Helper to write with PC context when available, otherwise use regular write
template<typename Machine>
void write_with_optional_pc(Machine& machine, uint16_t addr, uint8_t val, bool has_pc, uint16_t pc) {
    if constexpr (HasPcAwareMemory<decltype(machine.memory())>) {
        if (has_pc) {
            machine.memory().write_with_pc(addr, val, pc);
            return;
        }
    }
    machine.write(addr, val);
}

//////////////////////////////////////////////////////////////////////////////
// DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

DebuggerControlServiceImpl::DebuggerControlServiceImpl(ModelB& machine)
    : machine_(machine) {
}

DebuggerControlServiceImpl::~DebuggerControlServiceImpl() = default;

void DebuggerControlServiceImpl::fill_execution_state(ExecutionState* state) {
    state->set_is_running(!machine_.is_paused());
    state->set_cycle_count(machine_.cycle_count());
    state->set_halt_reason(halt_reason_);
    state->set_sequence(machine_.sequence());
}

void DebuggerControlServiceImpl::update_breakpoint_callback() {
    if (breakpoints_.empty()) {
        machine_.set_instruction_callback(nullptr);
    } else {
        machine_.set_instruction_callback(
            [this](uint16_t pc, uint64_t /*cycle*/) -> bool {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& bp : breakpoints_) {
                    if (bp.address == pc) {
                        std::ostringstream oss;
                        oss << "breakpoint at $" << std::hex << std::uppercase
                            << std::setw(4) << std::setfill('0') << pc;
                        halt_reason_ = oss.str();
                        machine_.pause();
                        return false;  // Stop execution
                    }
                }
                return true;  // Continue
            }
        );
    }
}

grpc::Status DebuggerControlServiceImpl::GetState(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ExecutionState* response) {

    std::lock_guard<std::mutex> lock(mutex_);
    fill_execution_state(response);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::Run(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    RunResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("already running");
        return grpc::Status::OK;
    }

    halt_reason_.clear();
    machine_.resume();
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::Stop(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    StopResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    machine_.pause();
    halt_reason_ = "stopped by debugger";
    response->set_success(true);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::Reset(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ResetResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    machine_.reset();
    halt_reason_.clear();
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::StepInstruction(
    grpc::ServerContext* /*context*/,
    const StepRequest* request,
    StepResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("machine is running");
        return grpc::Status::OK;
    }

    uint32_t count = request->count();
    if (count == 0) count = 1;

    uint64_t start_cycle = machine_.cycle_count();
    uint32_t instructions = 0;

    for (uint32_t i = 0; i < count; ++i) {
        machine_.step_instruction();
        ++instructions;
    }

    halt_reason_.clear();
    response->set_success(true);
    response->set_instructions_executed(instructions);
    response->set_cycles_executed(machine_.cycle_count() - start_cycle);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::StepCycle(
    grpc::ServerContext* /*context*/,
    const StepRequest* request,
    StepResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("machine is running");
        return grpc::Status::OK;
    }

    uint32_t count = request->count();
    if (count == 0) count = 1;

    uint64_t start_cycle = machine_.cycle_count();

    for (uint32_t i = 0; i < count; ++i) {
        machine_.step();
    }

    halt_reason_.clear();
    response->set_success(true);
    response->set_instructions_executed(0);  // Unknown for cycle stepping
    response->set_cycles_executed(machine_.cycle_count() - start_cycle);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::ReadMemory(
    grpc::ServerContext* /*context*/,
    const ReadMemoryRequest* request,
    ReadMemoryResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    uint32_t length = request->length();
    bool has_pc = request->has_simulated_pc();
    uint16_t pc = has_pc ? static_cast<uint16_t>(request->simulated_pc()) : 0;

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length && (address + i) <= 0xFFFF; ++i) {
        uint16_t addr = static_cast<uint16_t>(address + i);
        data.push_back(static_cast<char>(
            read_with_optional_pc(machine_, addr, has_pc, pc)));
    }

    response->set_data(std::move(data));
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::WriteMemory(
    grpc::ServerContext* /*context*/,
    const WriteMemoryRequest* request,
    WriteMemoryResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    const std::string& data = request->data();
    bool has_pc = request->has_simulated_pc();
    uint16_t pc = has_pc ? static_cast<uint16_t>(request->simulated_pc()) : 0;

    for (size_t i = 0; i < data.size() && (address + i) <= 0xFFFF; ++i) {
        uint16_t addr = static_cast<uint16_t>(address + i);
        write_with_optional_pc(machine_, addr, static_cast<uint8_t>(data[i]), has_pc, pc);
    }

    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::PeekMemory(
    grpc::ServerContext* /*context*/,
    const PeekMemoryRequest* request,
    PeekMemoryResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    uint32_t length = request->length();
    bool has_pc = request->has_simulated_pc();
    uint16_t pc = has_pc ? static_cast<uint16_t>(request->simulated_pc()) : 0;

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length && (address + i) <= 0xFFFF; ++i) {
        uint16_t addr = static_cast<uint16_t>(address + i);
        data.push_back(static_cast<char>(
            peek_with_optional_pc(machine_, addr, has_pc, pc)));
    }

    response->set_data(std::move(data));
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::GetMemoryRegions(
    grpc::ServerContext* /*context*/,
    const GetMemoryRegionsRequest* /*request*/,
    GetMemoryRegionsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Get machine type from hardware
    response->set_machine_type(std::string(machine_.memory().MACHINE_TYPE));

    // Get regions from hardware
    auto regions = machine_.memory().get_memory_regions();
    for (const auto& region : regions) {
        auto* pb_region = response->add_regions();
        pb_region->set_name(std::string(region.name));
        pb_region->set_base_address(region.base_address);
        pb_region->set_size(region.size);
        pb_region->set_readable(beebium::has_flag(region.flags, beebium::RegionFlags::Readable));
        pb_region->set_writable(beebium::has_flag(region.flags, beebium::RegionFlags::Writable));
        pb_region->set_has_side_effects(beebium::has_flag(region.flags, beebium::RegionFlags::HasSideEffects));
        pb_region->set_populated(beebium::has_flag(region.flags, beebium::RegionFlags::Populated));
        pb_region->set_active(beebium::has_flag(region.flags, beebium::RegionFlags::Active));
    }

    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::PeekRegion(
    grpc::ServerContext* /*context*/,
    const RegionAccessRequest* request,
    RegionAccessResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    uint32_t length = request->length();

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length; ++i) {
        data.push_back(static_cast<char>(
            machine_.memory().peek_region(region_name, address + i)));
    }

    response->set_data(std::move(data));
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::ReadRegion(
    grpc::ServerContext* /*context*/,
    const RegionAccessRequest* request,
    RegionAccessResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    uint32_t length = request->length();

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length; ++i) {
        data.push_back(static_cast<char>(
            machine_.memory().read_region(region_name, address + i)));
    }

    response->set_data(std::move(data));
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::WriteRegion(
    grpc::ServerContext* /*context*/,
    const WriteRegionRequest* request,
    WriteRegionResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    const std::string& data = request->data();

    for (size_t i = 0; i < data.size(); ++i) {
        machine_.memory().write_region(region_name, address + static_cast<uint32_t>(i),
            static_cast<uint8_t>(data[i]));
    }

    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::AddBreakpoint(
    grpc::ServerContext* /*context*/,
    const AddBreakpointRequest* request,
    AddBreakpointResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t address = request->address();
    if (address > 0xFFFF) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    uint32_t id = next_breakpoint_id_++;
    breakpoints_.push_back({id, address});
    update_breakpoint_callback();

    response->set_success(true);
    response->set_id(id);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::RemoveBreakpoint(
    grpc::ServerContext* /*context*/,
    const RemoveBreakpointRequest* request,
    RemoveBreakpointResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t id = request->id();
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const BreakpointEntry& bp) { return bp.id == id; });

    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        update_breakpoint_callback();
        response->set_success(true);
    } else {
        response->set_success(false);
    }

    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::ListBreakpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ListBreakpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& bp : breakpoints_) {
        auto* pb_bp = response->add_breakpoints();
        pb_bp->set_id(bp.id);
        pb_bp->set_address(bp.address);
    }

    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::ClearBreakpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ClearBreakpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t count = static_cast<uint32_t>(breakpoints_.size());
    breakpoints_.clear();
    update_breakpoint_callback();

    response->set_count_removed(count);
    return grpc::Status::OK;
}

//////////////////////////////////////////////////////////////////////////////
// Debugger6502ServiceImpl
//////////////////////////////////////////////////////////////////////////////

Debugger6502ServiceImpl::Debugger6502ServiceImpl(ModelB& machine)
    : machine_(machine) {
}

Debugger6502ServiceImpl::~Debugger6502ServiceImpl() = default;

grpc::Status Debugger6502ServiceImpl::ReadRegisters(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    Registers6502* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_a(machine_.a());
    response->set_x(machine_.x());
    response->set_y(machine_.y());
    response->set_sp(machine_.sp());
    response->set_pc(machine_.pc());
    response->set_p(machine_.p());

    return grpc::Status::OK;
}

grpc::Status Debugger6502ServiceImpl::WriteRegisters(
    grpc::ServerContext* /*context*/,
    const WriteRegisters6502Request* request,
    WriteRegistersResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (request->has_a()) {
        machine_.set_a(static_cast<uint8_t>(request->a()));
    }
    if (request->has_x()) {
        machine_.set_x(static_cast<uint8_t>(request->x()));
    }
    if (request->has_y()) {
        machine_.set_y(static_cast<uint8_t>(request->y()));
    }
    if (request->has_sp()) {
        machine_.set_sp(static_cast<uint8_t>(request->sp()));
    }
    if (request->has_pc()) {
        machine_.set_pc(static_cast<uint16_t>(request->pc()));
    }
    if (request->has_p()) {
        machine_.set_p(static_cast<uint8_t>(request->p()));
    }

    response->set_success(true);
    return grpc::Status::OK;
}

} // namespace beebium::service
