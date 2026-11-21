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

// Why a step is refused on a running CPU, and what to do about it. A step
// never stops the CPU itself: whether to is the caller's decision.
inline constexpr const char* kStepWhileRunning =
    "machine is running; call debugger.stop() before stepping";

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
        const RunRequest* request,
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
    bool at_instruction_boundary() const;
    void note_step_end();
    void enqueue_event(StopReason reason, const WatchpointHitInfo& watchpoint_hit);
    void signal_counterpart_stop();
    void resume_locked(bool stop_at_current);

public:
    // Set a callback invoked when a breakpoint/watchpoint with
    // stop_counterpart fires. The callback should pause the counterpart
    // processor. Used by the Tube extension to coordinate host and coprocessor.
    using CounterpartStopCallback = std::function<void()>;
    void set_counterpart_stop_callback(CounterpartStopCallback cb) {
        counterpart_stop_cb_ = std::move(cb);
    }

    // Set a callback a Run with run_counterpart invokes, before it resumes
    // this processor, to run the counterpart. It returns whether the
    // counterpart was stopped and now runs. The server wires the host's to
    // the coprocessor debugger's run_as_counterpart; with none set, a Run
    // with run_counterpart is refused.
    using CounterpartRunCallback = std::function<bool(bool stop_at_current)>;
    void set_counterpart_run_callback(CounterpartRunCallback cb) {
        counterpart_run_cb_ = std::move(cb);
    }

    // Run this processor if it is stopped, as Run does, on behalf of its
    // counterpart's Run with run_counterpart. Returns whether it was stopped.
    bool run_as_counterpart(bool stop_at_current);

    // Launch-path coordination for --station auto (issue #67). The station
    // selection (an mDNS browse of up to a few seconds) runs AFTER the gRPC
    // server is listening, so a client -- the macOS app -- can connect and call
    // Run() while it is still in progress. Run() waits (bounded) for the
    // selection to finish before resuming, so that early Run is honoured and
    // the machine starts once Econet is enabled on the chosen number, rather
    // than being rejected as "already running" or lost. begin/end bracket the
    // selection on the launch thread.
    void begin_startup_selection() {
        startup_selection_pending_.store(true, std::memory_order_release);
    }
    void end_startup_selection() {
        {
            std::lock_guard<std::mutex> lock(startup_mutex_);
            startup_selection_pending_.store(false, std::memory_order_release);
        }
        startup_cv_.notify_all();
    }

private:
    CpuDebugTarget& machine_;
    // The target's CPU description, cached once at construction, and a
    // register-name -> index map over it. Conditional breakpoints read 6502
    // registers by name (6502-only, like the disassembler); reg_value_by_name
    // returns 0 for a name the CPU does not have.
    cpu::CpuDescriptor descriptor_;
    std::unordered_map<std::string, size_t> register_index_;

    // The target's condition variables (DebugVariable), read once, and the
    // scopes conditions compile in: a breakpoint names the machine's; a
    // watchpoint those, the machine's watchpoint-only ones (shadow_access)
    // and the access it judges (addr, value, write).
    std::vector<DebugVariable> variables_;
    beebium::ExprScope breakpoint_scope_;
    beebium::ExprScope watchpoint_scope_;
    static constexpr uint32_t SLOT_ADDR = 0xFFFF0000u;
    static constexpr uint32_t SLOT_VALUE = 0xFFFF0001u;
    static constexpr uint32_t SLOT_WRITE = 0xFFFF0002u;

    // What a condition's variables read: the target's, and the access a
    // watchpoint is judging.
    struct ConditionContext {
        const std::vector<DebugVariable>* variables;
        uint32_t addr = 0;
        uint8_t value = 0;
        bool write = false;
    };
    static uint64_t read_condition_variable(void* context, uint32_t slot);
    void build_condition_scopes();
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
    CounterpartRunCallback counterpart_run_cb_;

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

    // --station auto launch coordination (see begin/end_startup_selection).
    std::atomic<bool> startup_selection_pending_{false};
    std::mutex startup_mutex_;
    std::condition_variable startup_cv_;
    std::vector<BreakpointRecord> breakpoints_;
    std::atomic<uint32_t> next_breakpoint_id_{1};
    std::string halt_reason_;

    // The machine sequence number at which a step left the CPU part-way
    // through an instruction, or kNoMidInstruction. See note_step_end().
    static constexpr uint64_t kNoMidInstruction = ~uint64_t{0};
    std::atomic<uint64_t> mid_instruction_sequence_{kNoMidInstruction};

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
        bool at_instruction_boundary = true;
        WatchpointHitInfo watchpoint_hit;
        bool has_watchpoint_hit = false;
    };

    // Per-subscriber state
    struct Subscriber {
        std::queue<ExecutionEvent> queue;
        std::mutex mutex;
        std::condition_variable cv;
        // New events are waiting in event_queue_ to be drained. Set under
        // `mutex` before notifying `cv`, so the waiter's predicate sees it:
        // `queue` alone stays empty until a subscriber drains, which it only
        // does after waking.
        bool events_pending = false;
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
            {
                std::lock_guard<std::mutex> sub_lock(sub->mutex);
                sub->events_pending = true;
            }
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

} // namespace beebium::service

#endif // BEEBIUM_SERVICE_DEBUGGER_SERVICE_HPP
