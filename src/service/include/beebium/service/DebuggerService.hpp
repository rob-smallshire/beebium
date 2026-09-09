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
#include "beebium/extension/CpuDebugTarget.hpp"
#include "beebium/Cpu6502Descriptor.hpp"
#include "beebium/MemoryRegion.hpp"
#include "beebium/Types.hpp"
#include <moodycamel/readerwriterqueue.h>
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <iomanip>

namespace beebium::service {

// PC-aware access falls back to the plain access when no PC is supplied. The
// target's read_with_pc/peek_with_pc/write_with_pc default to the plain form;
// only the host adapter, whose memory routing depends on the program counter,
// overrides them.
inline uint8_t read_with_optional_pc(CpuDebugTarget& target, uint32_t addr, bool has_pc, uint32_t pc) {
    return has_pc ? target.read_with_pc(addr, pc) : target.read(addr);
}

inline uint8_t peek_with_optional_pc(CpuDebugTarget& target, uint32_t addr, bool has_pc, uint32_t pc) {
    return has_pc ? target.peek_with_pc(addr, pc) : target.peek(addr);
}

inline void write_with_optional_pc(CpuDebugTarget& target, uint32_t addr, uint8_t val, bool has_pc, uint32_t pc) {
    if (has_pc) {
        target.write_with_pc(addr, val, pc);
    } else {
        target.write(addr, val);
    }
}

/// Internal breakpoint record (service-layer, holds parsed condition)
struct BreakpointRecord {
    uint32_t id;
    uint32_t start_address;
    uint32_t end_address;  // exclusive
    bool stop_counterpart = false;
    std::optional<beebium::CompiledExpression> condition;
    bool enabled = true;
    uint64_t hit_count = 0;
};

/// gRPC service implementation for DebuggerControl
class DebuggerControlServiceImpl final : public DebuggerControl::Service {
public:
    explicit DebuggerControlServiceImpl(CpuDebugTarget& machine);
    ~DebuggerControlServiceImpl() override = default;

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

    // Memory region access
    grpc::Status GetMemoryRegions(
        grpc::ServerContext* context,
        const GetMemoryRegionsRequest* request,
        GetMemoryRegionsResponse* response) override;

    grpc::Status PeekRegion(
        grpc::ServerContext* context,
        const RegionAccessRequest* request,
        RegionAccessResponse* response) override;

    grpc::Status ReadRegion(
        grpc::ServerContext* context,
        const RegionAccessRequest* request,
        RegionAccessResponse* response) override;

    grpc::Status WriteRegion(
        grpc::ServerContext* context,
        const WriteRegionRequest* request,
        WriteRegionResponse* response) override;

    // Breakpoints
    grpc::Status AddBreakpoint(
        grpc::ServerContext* context,
        const AddBreakpointRequest* request,
        AddBreakpointResponse* response) override;

    grpc::Status RemoveBreakpoint(
        grpc::ServerContext* context,
        const RemoveBreakpointRequest* request,
        RemoveBreakpointResponse* response) override;

    grpc::Status EnableBreakpoint(
        grpc::ServerContext* context,
        const EnableBreakpointRequest* request,
        EnableBreakpointResponse* response) override;

    grpc::Status ListBreakpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ListBreakpointsResponse* response) override;

    grpc::Status ClearBreakpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ClearBreakpointsResponse* response) override;

    // Watchpoints
    grpc::Status AddWatchpoint(
        grpc::ServerContext* context,
        const AddWatchpointRequest* request,
        AddWatchpointResponse* response) override;

    grpc::Status RemoveWatchpoint(
        grpc::ServerContext* context,
        const RemoveWatchpointRequest* request,
        RemoveWatchpointResponse* response) override;

    grpc::Status EnableWatchpoint(
        grpc::ServerContext* context,
        const EnableWatchpointRequest* request,
        EnableWatchpointResponse* response) override;

    grpc::Status ListWatchpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ListWatchpointsResponse* response) override;

    grpc::Status ClearWatchpoints(
        grpc::ServerContext* context,
        const Empty* request,
        ClearWatchpointsResponse* response) override;

    // CPU state (family-agnostic register model)
    grpc::Status GetCpuDescriptor(
        grpc::ServerContext* context,
        const Empty* request,
        CpuDescriptor* response) override;

    grpc::Status GetCpuState(
        grpc::ServerContext* context,
        const Empty* request,
        CpuState* response) override;

    grpc::Status SetCpuState(
        grpc::ServerContext* context,
        const CpuState* request,
        CpuState* response) override;

    // Event streaming
    grpc::Status WatchExecutionState(
        grpc::ServerContext* context,
        const WatchExecutionStateRequest* request,
        grpc::ServerWriter<ExecutionStateEvent>* writer) override;

private:
    void fill_execution_state(ExecutionState* state);
    void update_breakpoint_entries();
    void update_watchpoint_entries();
    void enqueue_event(StopReason reason);
    void enqueue_event(StopReason reason, const WatchpointHitInfo& watchpoint_hit);
    void signal_counterpart_stop();

public:
    // Set a callback invoked when a breakpoint/watchpoint with
    // stop_counterpart fires. The callback should pause the counterpart
    // processor. Used by the Tube extension to coordinate host and coprocessor.
    using CounterpartStopCallback = std::function<void()>;
    void set_counterpart_stop_callback(CounterpartStopCallback cb) {
        counterpart_stop_cb_ = std::move(cb);
    }

private:
    CpuDebugTarget& machine_;
    // The target's CPU description, cached once at construction, and a
    // register-name -> index map over it. Conditional breakpoints read 6502
    // registers by name (6502-only, like the disassembler); reg_value_by_name
    // returns 0 for a name the CPU does not have.
    cpu::CpuDescriptor descriptor_;
    std::unordered_map<std::string, size_t> register_index_;
    uint64_t reg_value_by_name(std::string_view name) const;
    // The highest addressable byte for this CPU, from the descriptor's
    // address_bits (0xFFFF for a 16-bit CPU, wider for a 24- or 32-bit one).
    uint32_t max_address() const {
        const uint32_t bits = descriptor_.address_bits;
        return bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    }
    // Run an execution-state or entry-mutating handler under the full lock
    // order: control_mutex_ held throughout, the machine halted across the
    // change, and mutex_ held while `body` runs. `body` returns the RPC status.
    template <typename F>
    grpc::Status with_execution_change(F&& body) {
        std::lock_guard<std::mutex> ctrl(control_mutex_);
        grpc::Status status = grpc::Status::OK;
        machine_.with_execution_stopped([&] {
            std::lock_guard<std::mutex> lock(mutex_);
            status = body();
        });
        return status;
    }

    CounterpartStopCallback counterpart_stop_cb_;

    // Two mutexes with a strict lock order: control_mutex_ -> (with_execution_stopped)
    // -> mutex_.
    //
    // control_mutex_ is taken FIRST, and held for the whole handler, by every
    // RPC that changes execution state or mutates the breakpoint/watchpoint entry
    // vectors: Run, Stop, Reset, StepInstruction, StepCycle, and Add/Remove/
    // Enable/Clear for both breakpoints and watchpoints. It serialises those
    // against one another, so no handler can resume or step the machine in the
    // window where a mutation has paused it but has not yet taken mutex_.
    //
    // mutex_ protects the service-side records and event state. The emulation
    // thread takes ONLY mutex_ (in the breakpoint/watchpoint hit callbacks),
    // never control_mutex_, so it can always complete a callback and reach the
    // paused_ check -- with_execution_stopped, which waits for the emulation
    // thread to go idle, is entered while holding control_mutex_ but before
    // taking mutex_, so it never blocks that thread. Read-only handlers take
    // mutex_ alone.
    std::mutex control_mutex_;
    std::mutex mutex_;
    std::vector<BreakpointRecord> breakpoints_;
    std::atomic<uint32_t> next_breakpoint_id_{1};
    std::string halt_reason_;

    // Watchpoint state
    struct WatchpointRecord {
        uint32_t id;
        uint32_t start_address;
        uint32_t end_address;
        beebium::WatchType type;
        bool stop_counterpart = false;
        std::optional<beebium::CompiledExpression> condition;
        bool enabled = true;
        uint64_t hit_count = 0;
    };
    std::vector<WatchpointRecord> watchpoints_;
    std::atomic<uint32_t> next_watchpoint_id_{1};

    // Event distribution for execution state watchers.
    //
    // Architecture:
    //   Emulation loop → SPSC queue → fan-out → per-subscriber queues
    //
    // The emulation loop (breakpoint/watchpoint callbacks) writes to a single
    // SPSC queue (event_queue_). The gRPC streaming handlers each have their
    // own per-subscriber queue. Whichever subscriber wakes up first drains
    // the SPSC queue into all subscriber queues (including its own). The
    // SPSC drain is protected by event_drain_mutex_ to maintain the
    // single-consumer guarantee. The emulation loop never contends on this
    // mutex -- it only writes to the SPSC queue (lock-free).

    struct ExecutionEvent {
        StopReason reason = STOP_REASON_UNKNOWN;
        bool is_running = false;
        uint64_t cycle_count = 0;
        uint64_t sequence = 0;
        std::string halt_reason;
        WatchpointHitInfo watchpoint_hit;
        bool has_watchpoint_hit = false;
    };

    // Per-subscriber state
    struct Subscriber {
        std::queue<ExecutionEvent> queue;
        std::mutex mutex;
        std::condition_variable cv;
    };

    // SPSC queue: emulation loop → service layer (single producer, single consumer)
    moodycamel::ReaderWriterQueue<ExecutionEvent> event_queue_{32};

    // Protects draining event_queue_ into subscriber queues (single-consumer guarantee)
    std::mutex event_drain_mutex_;

    // Active subscribers (protected by subscribers_mutex_)
    std::mutex subscribers_mutex_;
    std::vector<std::shared_ptr<Subscriber>> subscribers_;

    // Notify all subscribers that new events may be available
    void notify_subscribers() {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        for (auto& sub : subscribers_) {
            sub->cv.notify_all();
        }
    }

    // Drain the SPSC queue and fan out to all subscriber queues.
    // Called by whichever subscriber wakes up first.
    void drain_and_fanout() {
        std::lock_guard<std::mutex> drain_lock(event_drain_mutex_);
        ExecutionEvent evt;
        while (event_queue_.try_dequeue(evt)) {
            std::lock_guard<std::mutex> sub_lock(subscribers_mutex_);
            for (auto& sub : subscribers_) {
                std::lock_guard<std::mutex> q_lock(sub->mutex);
                sub->queue.push(evt);
            }
        }
    }

    // Register a new subscriber
    std::shared_ptr<Subscriber> add_subscriber() {
        auto sub = std::make_shared<Subscriber>();
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.push_back(sub);
        return sub;
    }

    // Unregister a subscriber
    void remove_subscriber(const std::shared_ptr<Subscriber>& sub) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), sub),
            subscribers_.end());
    }
};

//////////////////////////////////////////////////////////////////////////////
// DebuggerControlServiceImpl template implementation
//////////////////////////////////////////////////////////////////////////////

DebuggerControlServiceImpl::DebuggerControlServiceImpl(CpuDebugTarget& machine)
    : machine_(machine), descriptor_(machine.cpu_descriptor()) {
    for (size_t i = 0; i < descriptor_.registers.size(); ++i) {
        register_index_[descriptor_.registers[i].name] = i;
    }
    machine_.set_breakpoint_hit_callback([this](const beebium::BreakpointEntry& bp, uint32_t pc) {
        // Increment hit counter
        auto& mutable_bp = const_cast<beebium::BreakpointEntry&>(bp);
        ++mutable_bp.hit_count;

        // Evaluate condition (if present; absent = unconditional = stop)
        bool should_stop = true;
        if (bp.condition) {
            beebium::ExprCpuState cpu_state{
                static_cast<uint8_t>(reg_value_by_name("A")),
                static_cast<uint8_t>(reg_value_by_name("X")),
                static_cast<uint8_t>(reg_value_by_name("Y")),
                static_cast<uint8_t>(reg_value_by_name("SP")),
                static_cast<uint8_t>(reg_value_by_name("P")),
                static_cast<uint16_t>(reg_value_by_name("PC")),
                machine_.cycle_count(),
                bp.hit_count
            };
            auto peek_fn = [](void* ctx, uint16_t a) -> uint8_t {
                return static_cast<CpuDebugTarget*>(ctx)->peek(a);
            };
            should_stop = beebium::evaluate(
                *bp.condition, cpu_state, peek_fn, &machine_) != 0;
        }

        if (!should_stop) return;

        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << "breakpoint at $" << std::hex << std::uppercase
            << std::setw(4) << std::setfill('0') << pc;
        halt_reason_ = oss.str();

        if (bp.stop_counterpart) {
            signal_counterpart_stop();
        }

        machine_.pause();
        enqueue_event(STOP_REASON_BREAKPOINT);
    });

    machine_.set_watchpoint_hit_callback(
        [this](const beebium::WatchpointEntry& wp, uint32_t addr, uint8_t value, bool is_write) {
            // Increment hit counter (available as `hits` in condition expression)
            auto& mutable_wp = const_cast<beebium::WatchpointEntry&>(wp);
            ++mutable_wp.hit_count;

            // Evaluate condition (if present; absent = unconditional = stop)
            bool should_stop = true;
            if (wp.condition) {
                beebium::ExprCpuState cpu_state{
                    static_cast<uint8_t>(reg_value_by_name("A")),
                    static_cast<uint8_t>(reg_value_by_name("X")),
                    static_cast<uint8_t>(reg_value_by_name("Y")),
                    static_cast<uint8_t>(reg_value_by_name("SP")),
                    static_cast<uint8_t>(reg_value_by_name("P")),
                    static_cast<uint16_t>(reg_value_by_name("PC")),
                    machine_.cycle_count(),
                    wp.hit_count
                };
                auto peek_fn = [](void* ctx, uint16_t a) -> uint8_t {
                    return static_cast<CpuDebugTarget*>(ctx)->peek(a);
                };
                should_stop = beebium::evaluate(
                    *wp.condition, cpu_state, peek_fn, &machine_) != 0;
            }

            if (!should_stop) return;

            std::lock_guard<std::mutex> lock(mutex_);
            std::ostringstream oss;
            oss << (is_write ? "write" : "read") << " watchpoint at $"
                << std::hex << std::uppercase
                << std::setw(4) << std::setfill('0') << addr
                << " = $" << std::setw(2) << static_cast<unsigned>(value);
            halt_reason_ = oss.str();

            if (wp.stop_counterpart) {
                signal_counterpart_stop();
            }

            machine_.pause();

            WatchpointHitInfo hit_info;
            hit_info.set_watchpoint_id(wp.id);
            hit_info.set_address(addr);
            hit_info.set_value(value);
            hit_info.set_is_write(is_write);
            enqueue_event(STOP_REASON_WATCHPOINT, hit_info);
        });
}

void DebuggerControlServiceImpl::fill_execution_state(ExecutionState* state) {
    state->set_is_running(!machine_.is_paused());
    state->set_cycle_count(machine_.cycle_count());
    state->set_halt_reason(halt_reason_);
    state->set_sequence(machine_.sequence());
}

void DebuggerControlServiceImpl::enqueue_event(StopReason reason) {
    ExecutionEvent evt;
    evt.reason = reason;
    evt.is_running = !machine_.is_paused();
    evt.cycle_count = machine_.cycle_count();
    evt.sequence = machine_.sequence();
    evt.halt_reason = halt_reason_;
    event_queue_.enqueue(std::move(evt));
    notify_subscribers();
}

void DebuggerControlServiceImpl::update_breakpoint_entries() {
    // Snapshot live hit counts back into service-layer records
    for (const auto& entry : machine_.breakpoint_entries()) {
        for (auto& bp : breakpoints_) {
            if (bp.id == entry.id) {
                bp.hit_count = entry.hit_count;
                break;
            }
        }
    }

    // Rebuild machine entries from enabled records only
    std::vector<beebium::BreakpointEntry> entries;
    entries.reserve(breakpoints_.size());
    for (const auto& bp : breakpoints_) {
        if (!bp.enabled) continue;
        entries.push_back({bp.id,
                          bp.start_address,
                          bp.end_address,
                          bp.stop_counterpart,
                          bp.condition,
                          bp.hit_count});
    }
    machine_.set_breakpoint_entries(std::move(entries));
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

    // Execution-state change: serialise with entry mutations (see the mutex
    // ordering note) so a resume cannot slip into a mutation's pause window.
    std::lock_guard<std::mutex> ctrl(control_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("already running");
        return grpc::Status::OK;
    }

    halt_reason_.clear();
    // Enqueue the "running" event BEFORE resuming, so it's in the queue
    // before any breakpoint stop event that might fire immediately.
    {
        ExecutionEvent evt;
        evt.reason = STOP_REASON_UNKNOWN;
        evt.is_running = true;  // explicitly true -- machine is about to resume
        evt.cycle_count = machine_.cycle_count();
        evt.sequence = machine_.sequence();
        event_queue_.enqueue(std::move(evt));
        notify_subscribers();
    }
    machine_.resume();
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::Stop(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    StopResponse* response) {

    std::lock_guard<std::mutex> ctrl(control_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);

    machine_.pause();
    halt_reason_ = "stopped by debugger";
    enqueue_event(STOP_REASON_MANUAL);
    response->set_success(true);
    fill_execution_state(response->mutable_state());
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::Reset(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ResetResponse* response) {

    std::lock_guard<std::mutex> ctrl(control_mutex_);

    // Reset leaves the machine stopped, so pause first: with_execution_stopped
    // then keeps it paused (it restores the prior state, which is now "paused").
    // The halt and its wait-for-idle happen before mutex_ is taken (inside
    // with_execution_stopped), so the emulation thread's hit callback -- which
    // takes mutex_ -- can never block the wait and deadlock.
    machine_.pause();
    machine_.with_execution_stopped([&] {
        std::lock_guard<std::mutex> lock(mutex_);
        machine_.reset();
        // Complete the reset sequence to the first instruction boundary.
        machine_.step_instruction();
        halt_reason_.clear();
    });

    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::StepInstruction(
    grpc::ServerContext* /*context*/,
    const StepRequest* request,
    StepResponse* response) {

    // Stepping mutates machine state on this thread; serialise with entry
    // mutations and other execution-state changes via control_mutex_. The
    // machine is already paused (checked below), so no with_execution_stopped.
    std::lock_guard<std::mutex> ctrl(control_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("machine is running");
        return grpc::Status::OK;
    }

    uint32_t count = request->count();
    if (count == 0) count = 1;

    machine_.prepare_for_step();

    uint64_t start_cycle = machine_.cycle_count();
    uint32_t instructions = 0;

    for (uint32_t i = 0; i < count; ++i) {
        machine_.step_instruction();
        ++instructions;
    }
    machine_.finish_step();  // sync the coprocessor to the stopped host

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

    // Stepping mutates machine state on this thread; serialise with entry
    // mutations and other execution-state changes via control_mutex_. The
    // machine is already paused (checked below), so no with_execution_stopped.
    std::lock_guard<std::mutex> ctrl(control_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);

    if (!machine_.is_paused()) {
        response->set_success(false);
        response->set_error("machine is running");
        return grpc::Status::OK;
    }

    uint32_t count = request->count();
    if (count == 0) count = 1;

    machine_.prepare_for_step();

    uint64_t start_cycle = machine_.cycle_count();

    for (uint32_t i = 0; i < count; ++i) {
        machine_.step();
    }
    machine_.finish_step();  // sync the coprocessor to the stopped host

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
    uint32_t pc = has_pc ? request->simulated_pc() : 0;

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length && (address + i) <= max_address(); ++i) {
        uint32_t addr = address + i;
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
    uint32_t pc = has_pc ? request->simulated_pc() : 0;

    for (size_t i = 0; i < data.size() && (address + i) <= max_address(); ++i) {
        uint32_t addr = address + i;
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
    uint32_t pc = has_pc ? request->simulated_pc() : 0;

    std::string data;
    data.reserve(length);

    for (uint32_t i = 0; i < length && (address + i) <= max_address(); ++i) {
        uint32_t addr = address + i;
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
    response->set_machine_type(std::string(machine_.machine_type()));

    // Get regions from hardware
    auto regions = machine_.get_memory_regions();
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

    try {
        std::string data;
        data.reserve(length);

        for (uint32_t i = 0; i < length; ++i) {
            data.push_back(static_cast<char>(
                machine_.peek_region(region_name, address + i)));
        }

        response->set_data(std::move(data));
        return grpc::Status::OK;
    } catch (const std::invalid_argument& e) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
    }
}

grpc::Status DebuggerControlServiceImpl::ReadRegion(
    grpc::ServerContext* /*context*/,
    const RegionAccessRequest* request,
    RegionAccessResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    uint32_t length = request->length();

    try {
        std::string data;
        data.reserve(length);

        for (uint32_t i = 0; i < length; ++i) {
            data.push_back(static_cast<char>(
                machine_.read_region(region_name, address + i)));
        }

        response->set_data(std::move(data));
        return grpc::Status::OK;
    } catch (const std::invalid_argument& e) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
    }
}

grpc::Status DebuggerControlServiceImpl::WriteRegion(
    grpc::ServerContext* /*context*/,
    const WriteRegionRequest* request,
    WriteRegionResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const std::string& region_name = request->region_name();
    uint32_t address = request->address();
    const std::string& data = request->data();

    try {
        for (size_t i = 0; i < data.size(); ++i) {
            machine_.write_region(region_name, address + static_cast<uint32_t>(i),
                static_cast<uint8_t>(data[i]));
        }

        response->set_success(true);
        return grpc::Status::OK;
    } catch (const std::invalid_argument& e) {
        response->set_success(false);
        response->set_error(e.what());
        return grpc::Status::OK;
    }
}

grpc::Status DebuggerControlServiceImpl::AddBreakpoint(
    grpc::ServerContext* /*context*/,
    const AddBreakpointRequest* request,
    AddBreakpointResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t start = request->start_address();
    uint32_t end = request->end_address();
    // end_address == 0 means single-address breakpoint [start, start+1)
    if (end == 0) end = start + 1;
    if (start > max_address()
        || static_cast<uint64_t>(end) > static_cast<uint64_t>(max_address()) + 1
        || start >= end) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    // Parse optional condition expression
    std::optional<beebium::CompiledExpression> condition;
    if (!request->condition().empty()) {
        auto result = beebium::compile(request->condition());
        if (std::holds_alternative<std::string>(result)) {
            response->set_success(false);
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                std::get<std::string>(result));
        }
        condition = std::get<beebium::CompiledExpression>(std::move(result));
    }

    bool enabled = request->has_enabled() ? request->enabled() : true;

    uint32_t id = next_breakpoint_id_++;
    breakpoints_.push_back({id, start, end, request->stop_counterpart(),
                           std::move(condition), enabled, 0});
    update_breakpoint_entries();

    response->set_success(true);
    response->set_id(id);
    return grpc::Status::OK;
  });
}

grpc::Status DebuggerControlServiceImpl::RemoveBreakpoint(
    grpc::ServerContext* /*context*/,
    const RemoveBreakpointRequest* request,
    RemoveBreakpointResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t id = request->id();
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const BreakpointRecord& bp) { return bp.id == id; });

    if (it != breakpoints_.end()) {
        breakpoints_.erase(it);
        update_breakpoint_entries();
        response->set_success(true);
    } else {
        response->set_success(false);
    }

    return grpc::Status::OK;
  });
}

grpc::Status DebuggerControlServiceImpl::EnableBreakpoint(
    grpc::ServerContext* /*context*/,
    const EnableBreakpointRequest* request,
    EnableBreakpointResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t id = request->id();
    auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
        [id](const BreakpointRecord& bp) { return bp.id == id; });
    if (it == breakpoints_.end()) {
        response->set_success(false);
        return grpc::Status::OK;
    }
    if (it->enabled != request->enabled()) {
        it->enabled = request->enabled();
        update_breakpoint_entries();
    }
    response->set_success(true);
    return grpc::Status::OK;
  });
}

grpc::Status DebuggerControlServiceImpl::ListBreakpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ListBreakpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Hit counts are only safe to read when the machine is paused
    // (they're mutated by the emulation loop during run()).
    const bool can_read_hits = machine_.is_paused();
    const auto& live_entries = machine_.breakpoint_entries();

    for (const auto& bp : breakpoints_) {
        auto* pb_bp = response->add_breakpoints();
        pb_bp->set_id(bp.id);
        pb_bp->set_start_address(bp.start_address);
        pb_bp->set_end_address(bp.end_address);
        if (bp.condition) {
            pb_bp->set_condition(bp.condition->source);
        }
        pb_bp->set_stop_counterpart(bp.stop_counterpart);
        pb_bp->set_enabled(bp.enabled);
        if (can_read_hits && bp.enabled) {
            for (const auto& entry : live_entries) {
                if (entry.id == bp.id) {
                    pb_bp->set_hit_count(entry.hit_count);
                    break;
                }
            }
        } else {
            pb_bp->set_hit_count(bp.hit_count);
        }
    }

    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::ClearBreakpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ClearBreakpointsResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t count = static_cast<uint32_t>(breakpoints_.size());
    breakpoints_.clear();
    update_breakpoint_entries();

    response->set_count_removed(count);
    return grpc::Status::OK;
  });
}

//////////////////////////////////////////////////////////////////////////////
// Watchpoint RPCs - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

void DebuggerControlServiceImpl::update_watchpoint_entries() {
    // Snapshot live hit counts back into service-layer records
    for (const auto& entry : machine_.watchpoint_entries()) {
        for (auto& wp : watchpoints_) {
            if (wp.id == entry.id) {
                wp.hit_count = entry.hit_count;
                break;
            }
        }
    }

    // Rebuild machine entries from enabled records only
    std::vector<beebium::WatchpointEntry> entries;
    entries.reserve(watchpoints_.size());
    for (const auto& wp : watchpoints_) {
        if (!wp.enabled) continue;
        entries.push_back({wp.id,
                          wp.start_address,
                          wp.end_address,
                          wp.type,
                          wp.stop_counterpart,
                          wp.condition,
                          wp.hit_count});
    }
    machine_.set_watchpoint_entries(std::move(entries));
}

void DebuggerControlServiceImpl::enqueue_event(
    StopReason reason, const WatchpointHitInfo& watchpoint_hit) {
    ExecutionEvent evt;
    evt.reason = reason;
    evt.is_running = !machine_.is_paused();
    evt.cycle_count = machine_.cycle_count();
    evt.sequence = machine_.sequence();
    evt.halt_reason = halt_reason_;
    evt.watchpoint_hit = watchpoint_hit;
    evt.has_watchpoint_hit = true;
    event_queue_.enqueue(std::move(evt));
    notify_subscribers();
}

void DebuggerControlServiceImpl::signal_counterpart_stop() {
    if (counterpart_stop_cb_)
        counterpart_stop_cb_();
}

grpc::Status DebuggerControlServiceImpl::AddWatchpoint(
    grpc::ServerContext* /*context*/,
    const AddWatchpointRequest* request,
    AddWatchpointResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t start = request->start_address();
    uint32_t end = request->end_address();
    if (start > max_address()
        || static_cast<uint64_t>(end) > static_cast<uint64_t>(max_address()) + 1
        || start >= end) {
        response->set_success(false);
        return grpc::Status::OK;
    }

    beebium::WatchType type;
    switch (request->type()) {
        case WATCHPOINT_READ:  type = beebium::WATCH_READ; break;
        case WATCHPOINT_WRITE: type = beebium::WATCH_WRITE; break;
        default:               type = beebium::WATCH_BOTH; break;
    }

    // Parse optional condition expression
    std::optional<beebium::CompiledExpression> condition;
    if (!request->condition().empty()) {
        auto result = beebium::compile(request->condition());
        if (std::holds_alternative<std::string>(result)) {
            response->set_success(false);
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                std::get<std::string>(result));
        }
        condition = std::get<beebium::CompiledExpression>(std::move(result));
    }

    bool enabled = request->has_enabled() ? request->enabled() : true;

    uint32_t id = next_watchpoint_id_++;
    watchpoints_.push_back({id, start, end, type, request->stop_counterpart(),
                           std::move(condition), enabled, 0});
    update_watchpoint_entries();

    response->set_success(true);
    response->set_id(id);
    return grpc::Status::OK;
  });
}

grpc::Status DebuggerControlServiceImpl::RemoveWatchpoint(
    grpc::ServerContext* /*context*/,
    const RemoveWatchpointRequest* request,
    RemoveWatchpointResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t id = request->id();
    auto it = std::find_if(watchpoints_.begin(), watchpoints_.end(),
        [id](const WatchpointRecord& wp) { return wp.id == id; });

    if (it != watchpoints_.end()) {
        watchpoints_.erase(it);
        update_watchpoint_entries();
        response->set_success(true);
    } else {
        response->set_success(false);
    }

    return grpc::Status::OK;
  });
}

grpc::Status DebuggerControlServiceImpl::EnableWatchpoint(
    grpc::ServerContext* /*context*/,
    const EnableWatchpointRequest* request,
    EnableWatchpointResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t id = request->id();
    auto it = std::find_if(watchpoints_.begin(), watchpoints_.end(),
        [id](const WatchpointRecord& wp) { return wp.id == id; });
    if (it == watchpoints_.end()) {
        response->set_success(false);
        return grpc::Status::OK;
    }
    if (it->enabled != request->enabled()) {
        it->enabled = request->enabled();
        update_watchpoint_entries();
    }
    response->set_success(true);
    return grpc::Status::OK;
  });
}

grpc::Status DebuggerControlServiceImpl::ListWatchpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ListWatchpointsResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    const bool can_read_hits = machine_.is_paused();
    const auto& live_entries = machine_.watchpoint_entries();

    for (const auto& wp : watchpoints_) {
        auto* pb_wp = response->add_watchpoints();
        pb_wp->set_id(wp.id);
        pb_wp->set_start_address(wp.start_address);
        pb_wp->set_end_address(wp.end_address);
        switch (wp.type) {
            case beebium::WATCH_READ:  pb_wp->set_type(WATCHPOINT_READ); break;
            case beebium::WATCH_WRITE: pb_wp->set_type(WATCHPOINT_WRITE); break;
            default:                   pb_wp->set_type(WATCHPOINT_BOTH); break;
        }
        if (wp.condition) {
            pb_wp->set_condition(wp.condition->source);
        }
        pb_wp->set_stop_counterpart(wp.stop_counterpart);
        pb_wp->set_enabled(wp.enabled);
        if (can_read_hits && wp.enabled) {
            for (const auto& entry : live_entries) {
                if (entry.id == wp.id) {
                    pb_wp->set_hit_count(entry.hit_count);
                    break;
                }
            }
        } else {
            pb_wp->set_hit_count(wp.hit_count);
        }
    }

    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::ClearWatchpoints(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ClearWatchpointsResponse* response) {
  return with_execution_change([&]() -> grpc::Status {

    uint32_t count = static_cast<uint32_t>(watchpoints_.size());
    watchpoints_.clear();
    update_watchpoint_entries();

    response->set_count_removed(count);
    return grpc::Status::OK;
  });
}

//////////////////////////////////////////////////////////////////////////////
// Event Streaming - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

grpc::Status DebuggerControlServiceImpl::WatchExecutionState(
    grpc::ServerContext* context,
    const WatchExecutionStateRequest* /*request*/,
    grpc::ServerWriter<ExecutionStateEvent>* writer) {

    // Register this subscriber
    auto subscriber = add_subscriber();

    // Send initial state immediately
    {
        ExecutionStateEvent event;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fill_execution_state(event.mutable_state());
        }
        if (!writer->Write(event)) {
            remove_subscriber(subscriber);
            return grpc::Status::OK;
        }
    }

    // Process events from per-subscriber queue
    while (!context->IsCancelled()) {
        // Wait for events on this subscriber's queue
        {
            std::unique_lock<std::mutex> lock(subscriber->mutex);
            subscriber->cv.wait_for(lock, std::chrono::milliseconds(100),
                [&subscriber, context] {
                    return !subscriber->queue.empty() || context->IsCancelled();
                });
        }

        if (context->IsCancelled()) {
            break;
        }

        // Drain SPSC queue into all subscriber queues (first waker wins)
        drain_and_fanout();

        // Process this subscriber's queue
        while (true) {
            ExecutionEvent evt;
            {
                std::lock_guard<std::mutex> lock(subscriber->mutex);
                if (subscriber->queue.empty()) break;
                evt = std::move(subscriber->queue.front());
                subscriber->queue.pop();
            }

            ExecutionStateEvent proto_event;
            proto_event.set_reason(evt.reason);
            proto_event.mutable_state()->set_is_running(evt.is_running);
            proto_event.mutable_state()->set_cycle_count(evt.cycle_count);
            proto_event.mutable_state()->set_sequence(evt.sequence);
            proto_event.mutable_state()->set_halt_reason(evt.halt_reason);
            proto_event.set_message(evt.halt_reason);
            if (evt.has_watchpoint_hit) {
                *proto_event.mutable_watchpoint_hit() = evt.watchpoint_hit;
            }
            if (!writer->Write(proto_event)) {
                remove_subscriber(subscriber);
                return grpc::Status::OK;
            }
        }
    }

    remove_subscriber(subscriber);

    return grpc::Status::OK;
}

//////////////////////////////////////////////////////////////////////////////
// CPU State - DebuggerControlServiceImpl
//////////////////////////////////////////////////////////////////////////////

uint64_t DebuggerControlServiceImpl::reg_value_by_name(std::string_view name) const {
    auto it = register_index_.find(std::string(name));
    if (it == register_index_.end()) return 0;
    return machine_.register_value(it->second);
}

grpc::Status DebuggerControlServiceImpl::GetCpuDescriptor(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    ::beebium::CpuDescriptor* response) {

    response->set_family(descriptor_.family);
    response->set_address_bits(descriptor_.address_bits);
    response->set_little_endian(descriptor_.little_endian);
    for (const auto& reg : descriptor_.registers) {
        auto* pb_reg = response->add_registers();
        pb_reg->set_name(reg.name);
        pb_reg->set_width_bits(reg.width_bits);
        pb_reg->set_role(static_cast<::beebium::RegisterRole>(reg.role));
        for (const auto& flag : reg.flag_names) {
            pb_reg->add_flag_names(flag);
        }
    }
    for (const auto& signal : descriptor_.signals) {
        response->add_signals(signal);
    }
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::GetCpuState(
    grpc::ServerContext* /*context*/,
    const Empty* /*request*/,
    CpuState* response) {

    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < descriptor_.registers.size(); ++i) {
        auto* rv = response->add_registers();
        rv->set_name(descriptor_.registers[i].name);
        rv->set_value(machine_.register_value(i));
    }
    for (size_t i = 0; i < descriptor_.signals.size(); ++i) {
        cpu::SignalStateValue s = machine_.signal_state(i);
        auto* ss = response->add_signals();
        ss->set_name(descriptor_.signals[i]);
        ss->set_asserted(s.asserted);
        ss->set_pending(s.pending);
        ss->set_in_handler(s.in_handler);
    }
    response->set_cycle_count(machine_.cycle_count());
    return grpc::Status::OK;
}

grpc::Status DebuggerControlServiceImpl::SetCpuState(
    grpc::ServerContext* /*context*/,
    const CpuState* request,
    CpuState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Reject any register name the CPU does not have, naming the offenders, so
    // a typo fails loudly rather than being silently ignored.
    std::string unknown;
    for (const auto& rv : request->registers()) {
        if (register_index_.find(rv.name()) == register_index_.end()) {
            if (!unknown.empty()) unknown += ", ";
            unknown += rv.name();
        }
    }
    if (!unknown.empty()) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "unknown register(s): " + unknown);
    }

    // Apply the given subset by name.
    for (const auto& rv : request->registers()) {
        machine_.set_register_value(register_index_.at(rv.name()), rv.value());
    }

    // Read back the resulting full state under the same lock, so the write and
    // the returned snapshot are atomic (no instruction can execute in between).
    for (size_t i = 0; i < descriptor_.registers.size(); ++i) {
        auto* out = response->add_registers();
        out->set_name(descriptor_.registers[i].name);
        out->set_value(machine_.register_value(i));
    }
    for (size_t i = 0; i < descriptor_.signals.size(); ++i) {
        cpu::SignalStateValue s = machine_.signal_state(i);
        auto* ss = response->add_signals();
        ss->set_name(descriptor_.signals[i]);
        ss->set_asserted(s.asserted);
        ss->set_pending(s.pending);
        ss->set_in_handler(s.in_handler);
    }
    response->set_cycle_count(machine_.cycle_count());
    return grpc::Status::OK;
}

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
