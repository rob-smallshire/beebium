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

#pragma once

#include "beebium/extension/CpuDebugTarget.hpp"
#include "beebium/service/DebuggerService.hpp"
#include "debugger.grpc.pb.h"

namespace beebium {

// Adapter that exposes a DebuggerControlServiceImpl under the
// CoprocessorDebuggerControl proto service name.
//
// Both DebuggerControl and CoprocessorDebuggerControl have identical RPCs and
// share message types. This adapter inherits from the generated
// CoprocessorDebuggerControl::Service and delegates each RPC to the underlying
// DebuggerControlServiceImpl, allowing host and coprocessor debuggers to coexist
// on the same gRPC server. It lives in the server because the server, not the
// coprocessor extension, instantiates the debugger against the abstract
// CpuDebugTarget interface.

class CoprocessorDebuggerAdapter final : public CoprocessorDebuggerControl::Service {
public:
    explicit CoprocessorDebuggerAdapter(service::DebuggerControlServiceImpl& impl)
        : impl_(impl) {}

    // Forward each RPC to the underlying implementation.
    // The generated Service base class and DebuggerControlServiceImpl use
    // the same proto message types, so the forwarding is trivial.

#define FORWARD_UNARY(method) \
    grpc::Status method(grpc::ServerContext* ctx, \
                        const decltype(std::declval<DebuggerControl::Service>().method)::RequestType* req, \
                        decltype(std::declval<DebuggerControl::Service>().method)::ResponseType* resp) override \
    { return impl_.method(ctx, req, resp); }

    // Execution control
    grpc::Status GetState(grpc::ServerContext* ctx, const Empty* req, ExecutionState* resp) override
    { return impl_.GetState(ctx, req, resp); }

    grpc::Status Run(grpc::ServerContext* ctx, const Empty* req, RunResponse* resp) override
    { return impl_.Run(ctx, req, resp); }

    grpc::Status Stop(grpc::ServerContext* ctx, const Empty* req, StopResponse* resp) override
    { return impl_.Stop(ctx, req, resp); }

    grpc::Status Reset(grpc::ServerContext* ctx, const Empty* req, ResetResponse* resp) override
    { return impl_.Reset(ctx, req, resp); }

    grpc::Status StepInstruction(grpc::ServerContext* ctx, const StepRequest* req, StepResponse* resp) override
    { return impl_.StepInstruction(ctx, req, resp); }

    grpc::Status StepCycle(grpc::ServerContext* ctx, const StepRequest* req, StepResponse* resp) override
    { return impl_.StepCycle(ctx, req, resp); }

    // Event streaming
    grpc::Status WatchExecutionState(grpc::ServerContext* ctx,
                                     const WatchExecutionStateRequest* req,
                                     grpc::ServerWriter<ExecutionStateEvent>* writer) override
    { return impl_.WatchExecutionState(ctx, req, writer); }

    // Memory access
    grpc::Status ReadMemory(grpc::ServerContext* ctx, const ReadMemoryRequest* req, ReadMemoryResponse* resp) override
    { return impl_.ReadMemory(ctx, req, resp); }

    grpc::Status WriteMemory(grpc::ServerContext* ctx, const WriteMemoryRequest* req, WriteMemoryResponse* resp) override
    { return impl_.WriteMemory(ctx, req, resp); }

    grpc::Status PeekMemory(grpc::ServerContext* ctx, const PeekMemoryRequest* req, PeekMemoryResponse* resp) override
    { return impl_.PeekMemory(ctx, req, resp); }

    // Memory regions
    grpc::Status GetMemoryRegions(grpc::ServerContext* ctx, const GetMemoryRegionsRequest* req, GetMemoryRegionsResponse* resp) override
    { return impl_.GetMemoryRegions(ctx, req, resp); }

    grpc::Status PeekRegion(grpc::ServerContext* ctx, const RegionAccessRequest* req, RegionAccessResponse* resp) override
    { return impl_.PeekRegion(ctx, req, resp); }

    grpc::Status ReadRegion(grpc::ServerContext* ctx, const RegionAccessRequest* req, RegionAccessResponse* resp) override
    { return impl_.ReadRegion(ctx, req, resp); }

    grpc::Status WriteRegion(grpc::ServerContext* ctx, const WriteRegionRequest* req, WriteRegionResponse* resp) override
    { return impl_.WriteRegion(ctx, req, resp); }

    // Breakpoints
    grpc::Status AddBreakpoint(grpc::ServerContext* ctx, const AddBreakpointRequest* req, AddBreakpointResponse* resp) override
    { return impl_.AddBreakpoint(ctx, req, resp); }

    grpc::Status RemoveBreakpoint(grpc::ServerContext* ctx, const RemoveBreakpointRequest* req, RemoveBreakpointResponse* resp) override
    { return impl_.RemoveBreakpoint(ctx, req, resp); }

    grpc::Status EnableBreakpoint(grpc::ServerContext* ctx, const EnableBreakpointRequest* req, EnableBreakpointResponse* resp) override
    { return impl_.EnableBreakpoint(ctx, req, resp); }

    grpc::Status ListBreakpoints(grpc::ServerContext* ctx, const Empty* req, ListBreakpointsResponse* resp) override
    { return impl_.ListBreakpoints(ctx, req, resp); }

    grpc::Status ClearBreakpoints(grpc::ServerContext* ctx, const Empty* req, ClearBreakpointsResponse* resp) override
    { return impl_.ClearBreakpoints(ctx, req, resp); }

    // Watchpoints
    grpc::Status AddWatchpoint(grpc::ServerContext* ctx, const AddWatchpointRequest* req, AddWatchpointResponse* resp) override
    { return impl_.AddWatchpoint(ctx, req, resp); }

    grpc::Status RemoveWatchpoint(grpc::ServerContext* ctx, const RemoveWatchpointRequest* req, RemoveWatchpointResponse* resp) override
    { return impl_.RemoveWatchpoint(ctx, req, resp); }

    grpc::Status EnableWatchpoint(grpc::ServerContext* ctx, const EnableWatchpointRequest* req, EnableWatchpointResponse* resp) override
    { return impl_.EnableWatchpoint(ctx, req, resp); }

    grpc::Status ListWatchpoints(grpc::ServerContext* ctx, const Empty* req, ListWatchpointsResponse* resp) override
    { return impl_.ListWatchpoints(ctx, req, resp); }

    grpc::Status ClearWatchpoints(grpc::ServerContext* ctx, const Empty* req, ClearWatchpointsResponse* resp) override
    { return impl_.ClearWatchpoints(ctx, req, resp); }

    // CPU state (family-agnostic register model)
    grpc::Status GetCpuDescriptor(grpc::ServerContext* ctx, const Empty* req, CpuDescriptor* resp) override
    { return impl_.GetCpuDescriptor(ctx, req, resp); }

    grpc::Status GetCpuState(grpc::ServerContext* ctx, const Empty* req, CpuState* resp) override
    { return impl_.GetCpuState(ctx, req, resp); }

    grpc::Status SetCpuState(grpc::ServerContext* ctx, const CpuState* req, CpuState* resp) override
    { return impl_.SetCpuState(ctx, req, resp); }

#undef FORWARD_UNARY

private:
    service::DebuggerControlServiceImpl& impl_;
};

}  // namespace beebium
