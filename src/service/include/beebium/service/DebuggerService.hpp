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

#ifndef BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
#define BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP

#include "debugger.grpc.pb.h"
#include "beebium/Machines.hpp"
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <vector>
#include <atomic>

namespace beebium::service {

/// Internal breakpoint representation
struct BreakpointEntry {
    uint32_t id;
    uint32_t address;
};

/// gRPC service implementation for DebuggerControl
class DebuggerControlServiceImpl final : public DebuggerControl::Service {
public:
    explicit DebuggerControlServiceImpl(ModelB& machine);
    ~DebuggerControlServiceImpl() override;

    // Non-copyable
    DebuggerControlServiceImpl(const DebuggerControlServiceImpl&) = delete;
    DebuggerControlServiceImpl& operator=(const DebuggerControlServiceImpl&) = delete;

    // Execution control
    grpc::Status GetState(
        grpc::ServerContext* context,
        const Empty* request,
        ExecutionState* response) override;

    grpc::Status Run(
        grpc::ServerContext* context,
        const Empty* request,
        RunResponse* response) override;

    grpc::Status Stop(
        grpc::ServerContext* context,
        const Empty* request,
        StopResponse* response) override;

    grpc::Status Reset(
        grpc::ServerContext* context,
        const Empty* request,
        ResetResponse* response) override;

    grpc::Status StepInstruction(
        grpc::ServerContext* context,
        const StepRequest* request,
        StepResponse* response) override;

    grpc::Status StepCycle(
        grpc::ServerContext* context,
        const StepRequest* request,
        StepResponse* response) override;

    // Memory access
    grpc::Status ReadMemory(
        grpc::ServerContext* context,
        const ReadMemoryRequest* request,
        ReadMemoryResponse* response) override;

    grpc::Status WriteMemory(
        grpc::ServerContext* context,
        const WriteMemoryRequest* request,
        WriteMemoryResponse* response) override;

    grpc::Status PeekMemory(
        grpc::ServerContext* context,
        const PeekMemoryRequest* request,
        PeekMemoryResponse* response) override;

    // Breakpoints
    grpc::Status AddBreakpoint(
        grpc::ServerContext* context,
        const AddBreakpointRequest* request,
        AddBreakpointResponse* response) override;

    grpc::Status RemoveBreakpoint(
        grpc::ServerContext* context,
        const RemoveBreakpointRequest* request,
        RemoveBreakpointResponse* response) override;

    grpc::Status ListBreakpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ListBreakpointsResponse* response) override;

    grpc::Status ClearBreakpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ClearBreakpointsResponse* response) override;

private:
    void fill_execution_state(ExecutionState* state);
    void update_breakpoint_callback();

    ModelB& machine_;
    std::mutex mutex_;
    std::vector<BreakpointEntry> breakpoints_;
    std::atomic<uint32_t> next_breakpoint_id_{1};
    std::string halt_reason_;
};

/// gRPC service implementation for Debugger6502
class Debugger6502ServiceImpl final : public Debugger6502::Service {
public:
    explicit Debugger6502ServiceImpl(ModelB& machine);
    ~Debugger6502ServiceImpl() override;

    // Non-copyable
    Debugger6502ServiceImpl(const Debugger6502ServiceImpl&) = delete;
    Debugger6502ServiceImpl& operator=(const Debugger6502ServiceImpl&) = delete;

    grpc::Status ReadRegisters(
        grpc::ServerContext* context,
        const Empty* request,
        Registers6502* response) override;

    grpc::Status WriteRegisters(
        grpc::ServerContext* context,
        const WriteRegisters6502Request* request,
        WriteRegistersResponse* response) override;

private:
    ModelB& machine_;
    std::mutex mutex_;
};

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
