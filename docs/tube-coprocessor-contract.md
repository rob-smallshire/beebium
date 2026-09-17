# Tube Coprocessor Contract

This document specifies the contract between the emulation core and a Tube
coprocessor extension. It is the first step of the Tube architecture
programme described at the end of `tube-architecture-evolution.md`: separate
the *simulation model* (two free-running clock domains that meet only at the
Tube ULA) from the *execution strategy* (how many threads run it), so that
coprocessors can be added entirely as extensions and the execution strategy
can change later without touching them.

Status: specification for implementation. Sections marked *Step 1* are in
scope now. Later steps are noted so that Step 1 does not preclude them.


## Vocabulary

| Term | Meaning |
|------|---------|
| Host | The BBC Micro side: `Machine<Hardware>`, its 6502 and peripherals, clocked at 2 MHz. |
| Coprocessor | Everything on the far side of the Tube cable: bridging hardware (the Tube ULA for Acorn designs), a CPU, memory, boot ROM. Supplied by an extension. |
| Parasite | Acorn's name for the coprocessor's CPU. Not used in Beebium's APIs or new code: "coprocessor" is the project's word for everything on the far side of the cable, including its CPU. Existing `Parasite*` class names are renamed in Step 1d. |
| Host cycle | One tick of the host's 2 MHz clock. `Machine` counts these in `state_.cycle_count`. |
| Host time | The host's cumulative host-cycle count. This is the time unit of the contract. |
| Coprocessor cycle | One tick of the coprocessor's own clock (3 MHz for the 65C02 second processor, other rates for other designs). |
| Clock ratio | Coprocessor cycles per host cycle, as an exact rational `numerator/denominator` (3/2 for the 65C02 second processor). |


## Model

The host and the coprocessor are independent clock domains. The only
interaction between them is through the Tube ULA registers and the ULA's
interrupt outputs. An exact simulation applies every register access from
both sides in host-time order, and lets each side observe the ULA's
interrupt lines as they stood at that side's own time.

The single-threaded lockstep in `Machine::step()` is one exact execution of
that model. The contract below describes the coprocessor's obligations in a
way that any exact execution strategy can drive.

The contract is independent of the coprocessor's CPU family. Acorn and
third parties shipped 6502, 65C102, Z80, 6809, NS32016, 80186 and 80286
second processors, and the core must be able to host any of them by adding
an extension. Nothing the core, the server or the service layer calls on a
coprocessor may assume a 6502: register names, a 16-bit address space, an
`M6502` structure, or interrupt-handler tracking. Where a family-specific
surface is unavoidable today, which is only the gRPC debugger, it lives
behind a family-agnostic base and the server asks for the family it can
serve.


## The contract (Step 1)

### `Coprocessor` interface

A new header `src/core/include/beebium/tube/Coprocessor.hpp` replaces
`ParasiteTickable.hpp`:

```cpp
namespace beebium {

class Coprocessor {
public:
    virtual ~Coprocessor() = default;

    // Execute every coprocessor cycle due at or before host_cycle that has
    // not yet executed. Returns when the coprocessor's clock has reached
    // the point equivalent to host_cycle. See "Time" below.
    virtual void run_until(uint64_t host_cycle) = 0;

    // Debugger stop and resume, and the current state. See "Pause".
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool is_paused() const = 0;

    // Hardware reset, propagated from the host's reset line through the
    // Tube cable. Restarts the CPU at its reset vector and rebases time.
    virtual void reset() = 0;

    // Exact clock ratio, coprocessor cycles per host cycle.
    virtual ClockRatio clock_ratio() const = 0;
};
```

There is no program-counter accessor on this interface: a PC is a
family-specific notion (16-bit on a 6502, 24-bit on a 32016, segmented on
an 80186) and belongs on the family's debug target. Step 1 shipped a
`diag_pc()` for a stretch diagnostic that Step 1b deletes; it is removed
with it.

```cpp

struct ClockRatio {
    uint32_t numerator;    // coprocessor cycles
    uint32_t denominator;  // per this many host cycles
};

}
```

The extension owns the coprocessor object and keeps it alive while it is
installed in the socket, as it does today for the `ParasiteTickable`.

**Update (issue #70, 2026-09-17):** `clock_ratio()` is replaced by
`virtual BoardTiming board_timing() const = 0`. A 6502 Second Processor board
is slower than its nominal clock for two board reasons -- a DRAM refresh cycle
stolen at the opcode fetch periodically, and (on the 3 MHz wedge) a one-tick
stretch on every write cycle -- so the coprocessor's clock is counted in the
board's crystal ticks, not CPU cycles. `BoardTiming`
(`beebium/tube/Coprocessor.hpp`) carries `ticks_per_host_cycle` (still an exact
`ClockRatio`: 6/1 wedge, 2/1 65C102), the tick cost of a read and a write cycle,
and the refresh period and hold. `CoprocessorRunner` converts host cycles to
ticks with `CoprocessorClock` as before, then spends the tick budget cycle by
cycle, holding the CPU for a refresh at the next `M6502_IsAboutToExecute`.
`cycle_count()` stays "CPU cycles executed". A board with `{ratio, 1, 1, 0, 1}`
reproduces the old plain-ratio behaviour. The skew bound is unchanged (the
budget stays within one cycle's cost of due time). See
`docs/discussion/tube-coprocessor-board-timing.md`.

### Time

- `host_cycle` is the host's cumulative cycle count, `state_.cycle_count`.
- Between resets, successive `run_until` arguments are non-decreasing. A
  call with a smaller value than the previous call is a contract violation;
  implementations assert it in debug builds. In a release build the clock
  must not underflow: it treats the smaller value as an implicit rebase (a
  new origin, zero cycles due), the same effect as an explicit reset. The
  socket upholds the contract from its side too: `run_coprocessor_until`
  keeps the stored host time at least the furthest cycle the coprocessor has
  been run to, so a later register-access sync never targets an earlier time.
- The number of coprocessor cycles due at host time `t`, measured from the
  time origin `t0`, is exactly `floor((t - t0) * numerator / denominator)`.
  Implementations must compute this without floating point and without
  drift: the remainder carries across calls, so that over any interval the
  coprocessor runs exactly the cycles the ratio says and never one more or
  one fewer.
- The arithmetic must not overflow for `t - t0 < 2^62` with numerator and
  denominator below 2^16.
- `run_until` may be called with the same value more than once; the second
  call runs nothing.

### Pause

- While `is_paused()` is true, `run_until(t)` executes no cycles but still
  advances the coprocessor's record of host time to `t`. Cycles that fall
  in a paused interval are lost, not deferred. A stopped processor does not
  catch up on a running one when it resumes; this is the existing
  behaviour of `TubeSocket::tick_parasite()` and must be preserved exactly.
- The socket therefore does not need to test `is_paused()` before calling
  `run_until`; the coprocessor handles it. `pause()`, `resume()` and
  `is_paused()` are on the interface because the debugger's cross-processor
  stop logic uses them.

### Debugger entry mutation

- The executing thread iterates the breakpoint and watchpoint entry vectors
  continuously -- breakpoints at each instruction boundary, watchpoints at
  each bus access -- so those vectors are mutated only while that thread is
  idle. `CpuDebugTarget::with_execution_stopped(fn)` provides the guarantee:
  it halts the CPU's executing thread, runs `fn` (the mutation), and restores
  the prior run/pause state. The host implements it by pausing its emulation
  loop.
- A coprocessor executes only on the host emulation thread (the thread that
  calls `run_until` from `Machine::step`), so halting it means pausing the
  host. The coprocessor cannot reach the host itself; the server injects a
  quiescer (`set_execution_quiescer`) that pauses the host emulation loop,
  wired exactly as the cross-processor stop is. Hence the invariant: a
  coprocessor's debug entries are mutated only while the host emulation loop
  is idle.

#### The host quiesce primitive

`with_execution_stopped` on the host is `Machine::with_emulation_paused`, the
one primitive every caller uses to touch state the emulation thread owns. It is
hardened so it is correct for every caller, not only the debugger:

- **Logical pause is separate from quiescing.** A debugger client's pause is
  `user_paused_` (set by `pause()`, cleared by `resume()`); a quiesce raises
  `quiesce_depth_` for the duration of the body. The emulation loop reads the
  derived flag `paused_ == user_paused_ || quiesce_depth_ > 0`, recomputed under
  `debug_mutex_` whenever either source changes. `is_paused()` reports the
  logical state only. Keeping them apart closes two windows: a `Stop` that lands
  while another caller is quiescing is no longer discarded (it sets a distinct
  source, which the quiesce's unwind preserves), and a transient quiesce never
  makes a running machine look stopped to a client, nor does `resume()` release
  a machine another caller is still quiescing.
- **Quiescing callers are mutually exclusive.** `with_emulation_paused` holds a
  quiesce mutex across the body, so only one body runs against machine state at
  a time. The mutex is **recursive by necessity**: a quiescing body may call a
  Machine mutator that itself quiesces on the same thread (a `DebuggerService`
  `with_execution_change` handler calling `Machine::set_watchpoint_entries`,
  itself a `with_emulation_paused`). A plain mutex would self-deadlock on that
  nesting.
- **The idle guarantee covers everything the emulation thread does, not just
  `run()`.** The server loop enters a `Machine::EmulationBusyScope` around all
  per-iteration work outside `run()` -- ticking disc drives, adjusting emulation
  speed -- as well as `run()` itself; `wait_until_idle()` waits for that scope
  and for `run()`. The scope re-tests the pause under `debug_mutex_` at entry,
  so a quiesce that lands in the gap after `wait_if_paused` returns makes the
  scope inactive and the loop re-parks rather than racing. The loop's `on_wake`
  housekeeping (which ticks drives while the machine is parked) stands down
  whenever `quiesce_depth_ > 0`, so a quiescing body mutating a device never
  races the tick of that same device.
- **All state-mutating debugger RPCs route through it.** `WriteMemory`,
  `WriteRegion` and `SetCpuState` halt the executing thread across the write
  exactly as the entry mutators do. `ListBreakpoints`/`ListWatchpoints` read
  live hit counts with the thread halted (the counts stay plain integers on the
  hot path; the halt gives the read a happens-before with the callback's last
  write, which observing the pause flag alone would not).

This is the contract for **any** service or extension that mutates state the
emulation thread touches: wrap the mutation in `with_emulation_paused` (or, for
a plugin dispatcher, the server-supplied quiescer). The TSan witnesses in
`tests/test_pause_quiesce.cpp` enforce it.

### Origin and reset

- A coprocessor has no time base until its first `run_until(t)`, which
  establishes `t` as the origin `t0` with zero cycles due. The socket
  makes that call at install, with the current host time, so a
  coprocessor starts exactly when it is installed: it never runs a
  catch-up burst from host time zero, and under batching (Step 3) it does
  not start late by up to Δ waiting for the first deferred batch. Its
  cycle count over any run from install is exactly the ratio times the
  host cycles.
- `reset()` restarts the coprocessor and discards its time base, so the
  next `run_until(t)` establishes a new origin exactly as at construction.
  This is required because a hard host reset zeroes `state_.cycle_count`,
  so host time legitimately goes backwards across a reset.

### `TubeSocket` changes

`TubeSocket` stops owning any notion of the clock ratio or the fractional
phase. It becomes:

```cpp
void install_coprocessor(Coprocessor* coprocessor);   // replaces install_parasite
void remove_coprocessor();                            // replaces remove_parasite
void run_coprocessor_until(uint64_t host_cycle);      // replaces tick_parasite and tick_parasite_stretch
```

(`diag_parasite_pc()` existed in Step 1 and is removed in Step 1b together
with the stretch diagnostic that used it.)

`set_parasite_clock_ratio`, `tick_parasite`, `tick_parasite_stretch` and the
`parasite_phase_` / `parasite_clock_num_` / `parasite_clock_den_` members
are removed. `run_coprocessor_until` is a no-op when nothing is installed.
`TubeSocket::reset()` continues to reset the backend and the installed
coprocessor.

### `Machine::step()` changes

`Machine::step()` has three paths: a Tube bus-stretch cycle (host CPU
halted by the ULA), a 1MHz bus-stretch cycle (host CPU halted waiting for
a 1MHz peripheral), and a normal cycle. Today the first calls
`tick_parasite_stretch()`, the third calls `tick_parasite()`, and the
second does not tick the parasite at all. On the step where a Tube stretch
completes, the first path falls through into the third and the parasite is
advanced twice for one host cycle.

Both of those are defects in the current code, not behaviour to preserve.
On the hardware the coprocessor's clock runs continuously whatever the
host's bus is doing, and one host cycle is one host cycle. The contract
therefore makes a single call, as the first action of `step()`, before any
stretch handling, on every path:

```cpp
void step() {
    state_.memory.tube_socket.run_coprocessor_until(state_.cycle_count);
    ...
}
```

Because `cycle_count` advances by exactly one per `step()` on every path,
the coprocessor sees every host time exactly once, and the cycles it runs
per call follow the 1, 2, 1, 2 pattern of the 3:2 ratio without gaps or
doubling.

Consequences, stated so they are recognised rather than discovered:

- During a 1MHz stretch the coprocessor now runs, where before it was
  frozen and those cycles were lost. 1MHz accesses are frequent (VIAs,
  sound, the 1MHz bus), so the coprocessor gains real time relative to
  the host across a session. This is the hardware behaviour.
- On the step that completes a Tube stretch the coprocessor advances once,
  not twice.

Step 1's acceptance is therefore behavioural identity *except* for these
two documented changes, whose only permitted effect is that the coprocessor
runs continuously. Every boot, register, vector and scenario test must
still pass. If a test outcome changes, that is a defect in the migration
or a latent dependency on the frozen-parasite behaviour, and either way it
is to be understood before merge, not accepted.

**Update (issue #71):** the Tube bus-stretch path has since been removed
entirely -- the ULA has no way to stall the host, so a write to a full
register store-or-drops and completes in its own cycle (see
`docs/discussion/tube-ula-full-register-writes.md`). `Machine::step()` now has
two paths, the 1MHz bus-stretch cycle and the normal cycle; there is no
Tube-stretch path and nothing that advances the coprocessor twice for one host
cycle. The single `run_coprocessor_until` call at the top of `step()` is
unchanged.

### `CoprocessorClock` helper

So that every extension does not reimplement the rational arithmetic, core
provides a small value type, `src/core/include/beebium/tube/CoprocessorClock.hpp`:

```cpp
class CoprocessorClock {
public:
    explicit CoprocessorClock(ClockRatio ratio);

    // Discard the time base: the next cycles_due() call defines the origin
    // and returns zero. A newly constructed clock is in this state.
    void rebase();

    // Number of coprocessor cycles that became due since the previous call,
    // for the given host time. Exact, carries the remainder, never drifts.
    // Asserts host_cycle is not less than the previous host_cycle (unless
    // rebased).
    uint64_t cycles_due(uint64_t host_cycle);

    ClockRatio ratio() const;
};
```

`ParasiteRunner` implements `Coprocessor` using a `CoprocessorClock` and its
existing per-cycle `tick()`. The 65C02 extension constructs the runner with
`ClockRatio{3, 2}` and installs it with `install_coprocessor`. The ratio
leaves the socket and the extension's `init()` and lives with the runner.


## Out of scope for Step 1

- No change to how often `Machine::step()` calls the coprocessor. Batching
  cycles into larger quanta is Step 3 and depends on the skew bound Δ
  defined in Step 2.
- No threads, no atomics, no futexes.
- No change to `TubeUla` or to any register semantics. The vector suite is
  a fixed oracle.
- No change to the gRPC protos, the Python client or the TypeScript client.
- No change to the debugger's pause/resume or breakpoint behaviour beyond
  keeping it working.


## Tests

Test-first, as the project requires. New Catch2 tests:

1. `tests/test_coprocessor_clock.cpp`
   - Exactness for ratios 3/2, 5/1, 3/1, 1/1, 6/4 (must behave as 3/2), and
     an unreduced large ratio such as 10000/6667: over one million host
     cycles the total cycles returned equals `floor(N * num / den)` exactly.
   - Remainder carry: for 3/2 the per-call sequence for consecutive host
     cycles is 1, 2, 1, 2, ...; for 5/1 it is 5 every call; for 3/1, 3.
   - Repeated calls with the same host time return zero.
   - Non-monotonic host time without rebase triggers the debug assertion
     (test with `REQUIRE_THROWS`/death test pattern the project already uses,
     or omit if the project has no such pattern; note which in the PR).
   - `rebase()` then a smaller host time is accepted and returns zero.
2. `tests/test_tube_socket.cpp` (extend)
   - A recording stub `Coprocessor` that logs the argument of every
     `run_until` call: `run_coprocessor_until` passes host time through
     unchanged and is a no-op when nothing is installed.
   - `reset()` on the socket calls `reset()` on the installed coprocessor.
3. `tests/test_parasite_runner.cpp` (extend)
   - Equivalence oracle: drive a `ParasiteRunner` with `run_until(t)` for
     `t = 1..N` and count `tick()` executions per call (a counting hook or
     the cycle counter); the sequence must equal what the removed
     accumulator produced: 1, 2, 1, 2, ... for 3/2. Encode the old
     algorithm inline as the oracle so the equivalence is explicit.
   - Pause: with the runner paused, `run_until` advances time and runs
     nothing; after unpausing, the next call runs only the cycles due for
     the new interval, with no catch-up.
   - Reset: after `reset()`, a `run_until` with a smaller host time is
     accepted and runs nothing; subsequent calls run from that origin.

Existing tests to migrate to the new API, with no change in what they
assert: `test_boot_tube.cpp`, `test_tube_extension.cpp`,
`test_econet_tx_with_tube.cpp`, `test_tube_inprocess.cpp`.


## Acceptance

All of the following green, run locally before handing back:

- `ctest -R 'tube|Tube|parasite|Parasite'` in a Release build, including
  `test_tube_ula_vectors` and `test_boot_tube`.
- The scenario suites, from their own directories with `uv run pytest -m
  slow`: `integration_tests/wfsinit`, `integration_tests/tube-save`,
  `integration_tests/l3fs`, against the rebuilt `beebium-model-b-romram`.
- A CE2023 run if `test_tube_ce2023_trace` is part of the ctest set above
  (it is), since it is the sharpest interleaving-sensitive test we have.

Behavioural identity, apart from the two changes documented under
`Machine::step()`, is the acceptance criterion for Step 1. If any scenario
or boot test changes outcome, the change is a defect in the migration or a
latent dependency on the old behaviour, and is to be understood before
merge, not accepted as a side effect.


## Step 1b: the coprocessor as a plugin

### Goal

`acorn-65c02-coprocessor` becomes a plugin loaded from `<exe-dir>/extensions/`
like every other peripheral, and the server retains no link-time knowledge
of any concrete coprocessor type. After this step a new coprocessor is
added by adding a directory under `src/extensions/`, nothing else.

### Why it is not one today

`BuiltinExtensions.hpp` lists it as a built-in for two reasons, both in
`ServerMain`: it does `dynamic_cast<SecondProcessor65C02Extension*>` to
obtain the parasite debugger implementation for the `ParasiteDebuggerControl`
gRPC service and to wire cross-processor breakpoint stops, and the
extension's static library PUBLIC-links `beebium_service`. Two further
concrete-type dependencies sit behind `TubeSocket::tube_ula()`, which
`dynamic_cast`s the installed backend to `TubeUla`: `Machine::step()` uses
it to complete a bus stretch and for a stretch diagnostic, and
`DeviceInspectionService::GetTubeState` uses it to read ULA state. A plugin
carries its own copy of `TubeUla` and the parasite classes, so none of those
casts can succeed across the boundary.

### Design rule

The server, the service layer and `Machine` talk to a coprocessor only
through abstract interfaces exported from the extension API library
(`beebium_extension_api`, marked `BEEBIUM_EXT_API`). `dynamic_cast` to such
an exported interface is permitted, matching how `ServerMain` already finds
`EconetTransportExtension` and `PeripheralExtension`. `dynamic_cast` to a
concrete class defined outside the API library is not.

### Interfaces to add (extension API library)

1. `beebium/extension/CoprocessorExtension.hpp`

```cpp
class BEEBIUM_EXT_API CoprocessorExtension : public PeripheralExtension {
public:
    // The coprocessor to install in the TubeSocket. Valid after init().
    virtual Coprocessor* coprocessor() = 0;

    // The host-facing bridge (Tube ULA or other) to install as the socket
    // backend. Valid after init().
    virtual TubeHostBackend* tube_backend() = 0;

    // Debugger access, or nullptr if this coprocessor offers none. The
    // returned object is family-agnostic; the server asks it for the
    // families it can serve (see Cpu6502DebugTarget).
    virtual CoprocessorDebugTarget* debug_target() { return nullptr; }
};
```

   Cross-processor stop needs nothing on this class. Breakpoint detection
   for both processors lives in the two `DebuggerControlServiceImpl`
   instances, and after this step the server owns both, so it wires both
   directions itself: the host impl's counterpart callback calls
   `coprocessor()->pause()`, the coprocessor impl's counterpart callback
   calls `machine.pause()`. For that, `pause()` and `resume()` move onto
   the `Coprocessor` interface beside `is_paused()`, which is where the
   Step 1 contract already said pause belonged; `ParasiteRunner` has both.

   Whether the extension installs its backend and coprocessor into the
   socket itself in `init()` (as today) or the server does it from these
   accessors is the developer's choice; either way `init()` must leave the
   socket populated and `shutdown()` must leave it empty.

2. `beebium/extension/CoprocessorDebugTarget.hpp`

   The family-agnostic base every coprocessor's debug target derives from:

```cpp
class BEEBIUM_EXT_API CoprocessorDebugTarget {
public:
    virtual ~CoprocessorDebugTarget();
    // The CPU family, e.g. "6502", "z80", "6809", "ns32016", "80186".
    virtual std::string_view cpu_family() const = 0;
};
```

   It carries no registers, no address width and no memory access, because
   those differ per family and the only consumer today is a 6502-specific
   gRPC service. The server obtains a family it can serve by
   `dynamic_cast` to that family's exported interface; for a family it
   cannot serve it logs that no debugger is available for the coprocessor
   and continues, so a Z80 or 32016 coprocessor runs without a debugger
   until its family is supported (see Step 1d below).

   `beebium/extension/Cpu6502DebugTarget.hpp`

   `Cpu6502DebugTarget : CoprocessorDebugTarget`, the 6502 family, and
   the only one served today. An abstract class exposing, as virtual
   functions, exactly the members
   that `service::DebuggerControlServiceImpl<T>` requires of its `T`:
   execution control (`cycle_count`, `sequence`, `is_paused`, `pause`,
   `resume`, `reset`, `step`, `step_instruction`, `prepare_for_step`,
   `wait_until_idle`), flat memory access (`read`, `peek`, `write`),
   registers and their setters (`a`, `x`, `y`, `sp`, `pc`, `p`),
   `in_nmi_handler`, `in_irq_handler`, `cpu()` returning `const M6502&`,
   breakpoint and watchpoint entries, setters and hit callbacks, and
   `memory()` returning an abstract memory-region model with
   `get_memory_regions`, `peek_region`, `read_region`, `write_region` and
   `machine_type()`. The memory model deliberately has no PC-aware
   read/write: the parasite's map has none today, so the template's
   PC-unaware path is the existing behaviour and stays so.

   The template's one static-member use, `memory().MACHINE_TYPE`, becomes
   a `machine_type()` accessor: a one-line member on each host memory
   policy returning its existing `MACHINE_TYPE` constant, and a pure
   virtual on the abstract memory model. The constants remain the single
   source of truth.

   The server instantiates `DebuggerControlServiceImpl<Cpu6502DebugTarget>`
   once against this interface and wraps it in `ParasiteDebuggerAdapter`,
   which moves out of the extension into the server. `ParasiteRunner`
   implements the interface in the plugin; it already has every method,
   so this is adding `override`s.

3. `TubeHostBackend` gains what the server needs so that no code outside
   the plugin touches `TubeUla`:
   - `virtual bool try_complete_stretch()` (default: return true).
   - `virtual const TubeInspection* inspection() const` (default: nullptr).
     `TubeInspection`, in `beebium/tube/TubeInspection.hpp` alongside the
     `Counters` and `TraceEntry` types moved out of `TubeUla`, is the
     read-only diagnostic surface `DeviceInspectionService::GetTubeState`
     needs: `control_flags`, `host_peek`, `parasite_peek`, `hirq`, `pirq`,
     `pnmi`, `counters`, `trace_snapshot`. `TubeUla` implements it.
     Grouping the diagnostics behind one accessor keeps the register-access
     interface that `Machine` calls every cycle small.
   - `GetTubeState` fills from `inspection()` whenever the backend offers
     one, so its output for the 65C02 is identical before and after this
     step. Add a test asserting exactly that: the same sequence of register
     traffic through an installed backend and through the socket's owned
     ULA yields the same `GetTubeState` response.

   `TubeSocket::tube_ula()` then returns the socket's *owned* in-process
   `TubeUla` only (the `enable()` path used by tests) and never casts an
   installed backend. `TubeSocket::try_complete_tube_stretch()` calls the
   backend virtual. The `STRETCH-INFO` `fprintf` diagnostic in
   `Machine::step()`, with its static counters, is deleted rather than
   virtualised; it is a dead investigation aid.

### Server changes

- Both `dynamic_cast<SecondProcessor65C02Extension*>` sites become
  `dynamic_cast<CoprocessorExtension*>` and use the interface. If more than
  one `CoprocessorExtension` attaches to `tube`, the server refuses to
  start with a clear message: there is one Tube socket.
- `#include "SecondProcessor65C02Extension.hpp"` disappears from
  `src/server` and `src/service`. The extension is removed from
  `BuiltinExtensions.hpp` and from the server's link list.
- `read_stretch_parasite_ticks` stays untouched (proto dependency), as noted.

### Plugin build

- Add a SHARED plugin target in `src/extensions/acorn-65c02-coprocessor`
  following `test-scratch-ram`: `plugin_entry.cpp` exporting
  `beebium_create_extension`, a `manifest.json` carrying what the built-in
  manifest carried (name, display name, description, cli `tube-65c02`,
  `attaches_to: ["tube"]`, the `rom` parameter), and
  `beebium_finalize_plugin(NAME acorn-65c02-coprocessor)`.
- The plugin needs the Tube bridge, parasite runner, parasite CPU and the
  6502 core. Link `beebium_core` and `6502_lib` into the shared object,
  enabling `POSITION_INDEPENDENT_CODE` on those static libraries if it is
  not already on. Set `CXX_VISIBILITY_PRESET hidden` on the plugin so its
  private copies of those classes never interpose on the server's own.
- Keep the static library target for the tests that link the extension
  directly (`test_tube_extension`, `test_boot_tube` and friends), as
  `test-scratch-ram` does.
- The `beebium-servers` aggregate must build the plugin so that every
  artifact, package and the macOS app bundle (which copies `extensions/`
  when present) picks it up without further change.
- ROM discovery must still find `acorn-tube-6502_1_10.rom` from the plugin;
  check the lookup does not assume the built-in's location.

### Tests

Test-first. New:

- `tests/test_coprocessor_extension.cpp`: a stub `CoprocessorExtension`
  installed through the same path the server uses, verifying the server-
  side wiring end to end without the 65C02: the coprocessor and backend
  land in the socket, a host breakpoint with `stop_counterpart` pauses the
  coprocessor, and a coprocessor breakpoint with `stop_counterpart` pauses
  the host.
- A test that `DebuggerControlServiceImpl<Cpu6502DebugTarget>` drives a
  `ParasiteRunner` through the interface: read and write registers and
  memory, step, breakpoint hit. If `test_tube_inprocess.cpp` already covers
  cross-processor stop through the concrete type, migrate it to the
  interface rather than duplicating.

Existing tests to update, with intent preserved:

- `test_extension_subcommands.cpp`: `tube-65c02` is now listed as a plugin
  from the default extensions directory, like `acorn-rtc`, and its
  `attaches_to` still reports `tube`. The built-in assertions move to the
  plugin group; the negative assertion in the `--attaches-to serial-port`
  test is unchanged.
- `test_extension_resolver.cpp`: unaffected in substance; adjust wording.
- Any test that constructed the server with a synthetic `argv[0]` and
  expected `--tube-65c02` to resolve without a plugin directory must be
  given one, as the plugin tests already do.

### Acceptance

- Everything in Step 1's acceptance list, unchanged in outcome, with the
  server built with the plugin and no built-in coprocessor.
- `beebium-model-b-romram list-extensions` shows `tube-65c02` sourced from
  the extensions directory; `describe-extension tube-65c02` still shows
  the `rom` parameter.
- The wfsinit file-load tests, which read parasite memory through
  `connect_parasite()`, pass: that is the `ParasiteDebuggerControl` service
  working through the new interface.
- `grep -rn SecondProcessor65C02Extension src/server src/service` finds
  nothing.
- The static extension library and its tests link only `beebium_core`, so
  they are buildable with `-DBEEBIUM_BUILD_SERVICE=OFF`. (That configure
  currently fails earlier, in other extensions that call
  `beebium_compile_proto` unguarded; a pre-existing defect outside the
  Tube work, recorded under follow-ups.)

### Follow-ups recorded during Step 1b

Found while implementing; none is a Tube defect and none blocks the step.

- `DebuggerControlServiceImpl`'s breakpoint-hit callback re-locks the
  service mutex, so a service method such as `StepCycle` that holds the
  lock while stepping deadlocks when the step hits a breakpoint. The
  server never does this because emulation runs on its own thread; a
  single-threaded in-process driver does. `test_coprocessor_extension`
  steps the runner directly for that reason.
- The plugin includes `beebium/server/RomPaths.hpp` from the server's
  include directory to find its ROM, as the built-in did. ROM lookup is a
  facility every coprocessor plugin needs and belongs in the extension
  API. Addressed by Step 1e.
- `-DBEEBIUM_BUILD_SERVICE=OFF` fails to configure because
  `src/extensions/CMakeLists.txt` adds extensions whose CMake calls
  `beebium_compile_proto` unconditionally while the helper is only
  included under `BEEBIUM_BUILD_SERVICE`.

### Consequences to state

- A server started from a build tree without the plugin built, or an
  installed tree missing `extensions/acorn-65c02-coprocessor/`, no longer
  has a Tube. This is the same situation as every other plugin today and
  the artifacts already ship the tree; it is worth a sentence in
  `docs/deployment.md`.


## Step 1c: the 65C102 4 MHz second processor

### Goal

A second coprocessor plugin, `acorn-65c102-coprocessor`, CLI `tube-65c102`,
built from the same source as the 65C02 plugin, so that the programme has
two coprocessor instances exercising the contract and the server proves
it hosts a coprocessor it has never heard of.

### What it is

Acorn's 65C102 second processor (the Master Turbo module used the same
part) is a 65C02-family CPU at 4 MHz with 64 KB of RAM and the same 4 KB
Tube client ROM (its own v1.20 build). From the software's point of view it is the 65C02 second
processor with a faster clock; the differences are in the oscillator and
board, which the emulation does not model. Everything except the clock
ratio and the identity is shared.

### Design

- Parameterise `SecondProcessor65C02Extension` on what differs: the
  `ClockRatio` (3/2 or 2/1) and the display identity. One class, two
  constructions; no subclass and no duplicated logic.
- Two plugin directories under `src/extensions/`, each with its own
  `manifest.json` and `plugin_entry.cpp`, both linking the existing static
  library so the code exists once: the 65C102's entry constructs the
  extension with `ClockRatio{2, 1}`. The loader's one-manifest-per-directory
  rule is why there are two directories rather than one library exporting
  two manifests.
- The 65C102 manifest: name `acorn-65c102-coprocessor`, display name
  "Acorn 65C102 Co-processor", description "Acorn 65C102 4 MHz second
  processor", cli `tube-65c102`, `attaches_to: ["tube"]`, the same `rom`
  parameter. The server's single-socket rule already rejects loading both
  at once with a clear message; add a test for that message.
- `CoprocessorClock` already handles 2/1 exactly; the existing clock tests
  cover integer ratios.

### Tests

- The 65C102 plugin boots the Tube banner and reaches the BASIC prompt
  (a `test_boot_tube` case constructed with the 4 MHz ratio), and the
  parasite runs exactly twice the host's cycles over the boot: the cycle
  counters, not wall time.
- `list-extensions` shows both `tube-65c02` and `tube-65c102` from the
  extensions directory; `describe-extension tube-65c102` shows the `rom`
  parameter.
- Starting with both `--tube-65c02` and `--tube-65c102` fails with the
  single-socket message.
- One scenario-level check: the wfsinit ADFS-select test parametrised over
  both coprocessors, or a copy of it for the 65C102, since it is the
  heaviest R2/R3/R4 protocol exercise we have.

### Acceptance

Step 1b's acceptance list, plus the tests above, plus the packaging
consequence: the `beebium-servers` aggregate builds the new plugin so it
ships in every artifact without further change.


## Step 1e: coprocessor ROMs packaged with the extension

### Goal

A coprocessor's firmware belongs to the coprocessor. Each coprocessor
plugin ships the ROM images it needs inside its own plugin directory,
declares them in its manifest, and loads them from there. The server's
shared `roms/` directory carries host ROMs only, and no plugin includes a
server header to find a file.

### Design

- **Manifest declares ROMs.** `manifest.json` gains an optional `roms`
  array; each entry has `key`, `filename`, `size` in bytes, and
  `description`. For the 65C02 and 65C102 plugins:

```json
"roms": [
    {
        "key": "client",
        "filename": "acorn-tube-6502_1_10.rom",
        "size": 4096,
        "description": "Acorn Tube 6502 client ROM v1.10"
    }
]
```

  and for the 65C102 plugin the same entry with filename
  `acorn-tube-65c102_1_20.rom` and description "Acorn Tube 65C102 client
  ROM v1.20". `ExtensionManifest` parses it; `describe-extension` lists
  the entries; `list-extensions` is unchanged.

- **Files live beside the manifest.** In the source tree a plugin's ROMs
  are under `src/extensions/<name>/roms/`. `beebium_finalize_plugin`
  deploys that directory to `<exe-dir>/extensions/<name>/roms/` and
  installs it to `bin/extensions/<name>/roms/`, next to the library and
  manifest, so every artifact, package and the macOS app bundle (which
  copies `extensions/` whole) carries them without further change. Each
  plugin ships its own firmware, the full 4 KB 2732 device image: the 65C02
  plugin the 6502 Tube client v1.10 (`acorn-tube-6502_1_10.rom`, 4096 bytes,
  CRC32 `98b5fe42`, MD5 `8c3b9252ac812c892aa21b9252abf94c`), the 65C102 plugin
  the 65C102 Tube client v1.20 (`acorn-tube-65c102_1_20.rom`, 4096 bytes, CRC32
  `1462f0f7`, MD5 `f77689f677e625f87f42985532fef8b9`), a different build with
  its own banner, "Acorn TUBE 65C102 Co-Processor". Both are the full-EPROM
  dumps whose checksums match MAME's `6502tube_110.rom` and `65c102_boot_120.rom`;
  in each the lower 2 KB is all `&FF` (Acorn's firmware is the upper 2 KB) and
  the upper 2 KB is byte-for-byte the 2 KB image previously shipped (MD5
  `cd6ba85e22adec70b6d863de4c053db7` for the 6502,
  `83d73e0e78693bb4b43e7cb18e58d556` for the 65C102). No plugin references
  another plugin's directory. The 65C102 upper half is the image B2 and B-Em
  ship. A separate 2 KB image circulates as "65C102 TUBE 1.20" (MD5
  `f0555114f7a18f727e9ca14effebcc95`) that Toby Lobster's library annotates as
  saved from a RAM copy after self-modification: its startup RTS at &F85E has
  become TYA and its NMI vector has been rewritten, so booted as a ROM it goes
  straight to the "*" supervisor and never prints the banner. Only the image
  from the chip boots.

- **Resolution is the extension API's job.** Add to `Extension` (or
  `ExtensionContext`, developer's choice, say which) a
  `rom_filepath(std::string_view key)` that returns the path of the
  declared ROM resolved against the manifest's `manifest_dirpath`, and a
  `load_rom(key, span)` convenience that reads it and checks the declared
  size. Both report a clear error naming the expected path when the file
  is missing or the wrong size. An explicit `rom` configuration parameter
  still overrides the packaged file, for users supplying a different
  client ROM. The declared size is 4096: the full contents of the board's
  4 KB 2732, mapped at &F000-&FFFF. The image must be exactly 4096 bytes,
  with no content rule for either half (the lower half is genuine ROM address
  space -- a ReCo6502 client executes from it; Acorn's dumps leave it &FF).
  There is NO half-size or padded-dump acceptance: a 2 KB file -- the
  upper-half-only image other emulators ship -- is a fragment, and the model
  does not synthesise the missing half. A `rom=` override of any other size
  fails at load with a one-sentence message naming the device and the
  expectation, e.g. "Tube client ROM must be the full 4096-byte 2732 image
  (F000-FFFF); got 2048 bytes. The 2 kB images shipped by other emulators are
  the upper half only." `SecondProcessor65C02Extension::load_rom` uses these
  and drops its include of `beebium/server/RomPaths.hpp`; the plugin no longer
  needs the server include directory at all.

- **Boot-mode decode is `&F000-&FFFF`, mirroring unverified.** The map answers
  ROM only at &F000-&FFFF, as MAME's `tube_6502` does. The 6502 Second
  Processor Service Manual (s5.1-5.3; IC3 is a 2732, "4K, top 2K used")
  describes the boot latch as disabling CAS on every read while set, which
  would mirror the 2732 across the whole address space in boot mode. Whether
  the hardware decodes that broadly is UNVERIFIED pending the schematic's
  chip-select logic, so the model implements MAME's narrow decode and does not
  guess at mirroring.

- **Load-time check.** When a plugin with declared ROMs is loaded, the
  loader (or the extension's `init()`, developer's choice, say which)
  verifies every declared ROM is present at its resolved path and fails
  the load with a message naming the plugin and the path. A missing
  firmware image is a broken installation, and the error should say so
  before the machine boots without a coprocessor.

- **The shared ROM directory sheds the Tube ROM.** Remove
  `acorn-tube-6502_1_10.rom` from the server's build-time copy list, from
  the repository's top-level `roms/`, and from `tests/assets/roms/` if it
  is a duplicate; the C++ tests that load the Tube client ROM take it from
  the 65C02 plugin's source `roms/` directory through a compile-time
  definition, as they take host ROMs from `BEEBIUM_ROM_DIR` today. Check
  the macOS app and the packaging install lists for any explicit mention
  of the file.

- **Built-ins.** A built-in extension has no manifest directory; the
  `roms` feature is defined for plugins only, and the two remaining
  built-ins declare none.

### Tests

- Manifest parsing: `roms` present, absent, and malformed.
- `describe-extension tube-65c02` and `tube-65c102` list the client ROM
  entry with its filename and size.
- `rom_filepath`/`load_rom`: resolves beside the manifest; the explicit
  `rom` parameter overrides; a missing file and a wrong-size file each
  produce the specified error; the exact 4096-byte device image is accepted
  whatever either half holds, and a 2048-byte (upper-half-only) file is
  rejected with the device-naming message. A synthetic 4 KB image whose reset
  vector points into the lower half (&F000-&F7FF) executes from there through
  the real extension, proving the lower half is mapped ROM.
- Load-time check: a plugin directory whose declared ROM is absent fails
  to load with a message naming the plugin and the path (use
  `test-scratch-ram` or a temporary manifest copy).
- Proof that the packaged ROM is what gets used: a server started with
  `BEEBIUM_ROM_DIR` pointing at a directory holding only the host ROMs
  boots the Tube banner with `--tube-65c02`, and with `--tube-65c102`
  boots the 65C102's own banner, "Acorn TUBE 65C102 Co-Processor". The
  Step 1c boot test and the wfsinit parametrised test are updated to
  expect that banner for the 65C102 rather than the 6502 one.

### Acceptance

Step 1c's acceptance list, unchanged in outcome, plus the tests above,
plus `grep -rn 'beebium/server' src/extensions/acorn-65c02-coprocessor
src/extensions/acorn-65c102-coprocessor` finding nothing.


## Step 2: the skew bound

### Goal

Name the maximum permitted divergence between host time and coprocessor
time, define what stays exact and what may lag by up to that bound, and
test that the execution strategy honours it. This step changes no
behaviour: the current strategy runs the coprocessor to the host's cycle
on every step, so its achieved skew is zero. It exists so that Step 3 can
raise the call interval against a stated, tested contract rather than a
description of the one strategy we have.

### Definitions

- **Host time** `H`: `state_.cycle_count` at the moment the host does
  something.
- **Coprocessor time** `C`: the host time the coprocessor has been run to,
  that is the argument of the last completed `run_until`. In any
  single-threaded strategy `C <= H` always; the coprocessor is never ahead.
- **Skew**: `H - C`, in host cycles.

### The contract

1. **Register accesses are exact.** Immediately before the host reads or
   writes any Tube register at host time `H`, `C == H`. The coprocessor has
   therefore executed every cycle due before the access and none due after
   it, so the ULA's state is what the hardware would present at that bus
   cycle. The coprocessor's own register accesses are exact by construction
   in a single-threaded strategy, because it runs only inside `run_until`
   and sees every host access at times below its own.
2. **Everything else may lag by at most Δ.** At any host cycle that is not
   a Tube register access, `H - C <= MAX_COPROCESSOR_SKEW`. The only
   observable consequence is interrupt latency: the host samples HIRQ from
   ULA state as of `C`, so a HIRQ the coprocessor raises at coprocessor time
   `c` reaches the host's IRQ input within Δ host cycles; symmetrically a
   PIRQ or PNMI raised by a host write is seen by the coprocessor when it
   next runs, which is within Δ. On the hardware the same latencies exist
   and are the CPUs' own interrupt recognition times.
3. **Reset and pause do not break the bound.** After `reset()` the
   coprocessor's time base is re-established by the first `run_until` and
   the bound holds from there. While paused the coprocessor's time still
   advances with `run_until` calls (Step 1 semantics), so the bound holds
   trivially.

### The value

`MAX_COPROCESSOR_SKEW` is 8 host cycles, 4 microseconds at 2 MHz, declared
as a named constant in `TubeSocket`. Rationale: the tightest open-loop
timing in the Tube protocol is the type 0 to 3 NMI transfer, where the host
touches R3 every 24 us per byte (26 us per pair) and expects the parasite's
NMI handler, a few dozen 3 MHz cycles, to have run in between. The
coprocessor's interrupt latency under this contract is at most Δ plus its
own recognition time; at 4 us that leaves the handler well over half the
window. A larger Δ is a Step 3 decision to be made against measurements,
not a Step 2 one; 8 is safe and the constant is the only place to change.

### What is built

- `TubeSocket` gains the constant and records the argument of the last
  `run_coprocessor_until` as `coprocessor_time()`. It gains an optional
  observer hook for tests: a callback invoked with `(host_time, offset,
  is_write)` immediately before each host register access, where
  `host_time` is supplied by `Machine` (the socket does not know the host
  clock otherwise). Zero cost when unset; not for production use.
- `Machine` passes `state_.cycle_count` to the socket for that hook.
- The contract text above is added to the class comment of
  `TubeSocket` in condensed form, present tense, so the code carries it.

### Tests

- `tests/test_coprocessor_skew.cpp`: a `SkewObserver` that installs the
  hook and wraps the coprocessor, and asserts across a whole 65C02 boot to
  the BASIC prompt, and across the CE2023 load, that (a) at every host
  register access `coprocessor_time() == host_time`, and (b) at every
  `run_coprocessor_until(H)` call `H - previous_C <= MAX_COPROCESSOR_SKEW`.
  With the current strategy (b) observes 1 everywhere; the test asserts
  the bound, not the observed value, so it keeps passing as Step 3 raises
  the interval.
- A test that deliberately violates the bound through the hook (a stub
  strategy that calls `run_coprocessor_until` every 9 cycles) is caught by
  the observer, proving the observer can fail.
- A test that `coprocessor_time()` is re-established after `reset()`.

### Acceptance

Step 1e's acceptance list unchanged in outcome, plus the tests above.
Behavioural identity is the criterion: this step changes nothing the
emulated machines can observe.


## Step 3: batching

### Goal

Stop calling the coprocessor every host cycle. Run it in batches of up to
`MAX_COPROCESSOR_SKEW` host cycles, and exactly to the host's cycle before
any host Tube register access, so that the contract of Step 2 is honoured
with the cheapest strategy that honours it. This is the mechanism a fast
coprocessor core needs: inside one `run_until` call it may now execute a
whole batch without returning.

### Strategy

- `Machine::step()` no longer calls `run_coprocessor_until` on every
  cycle. It calls `tube_socket.host_cycle(state_.cycle_count)` as its
  first action on every path. That stores host time `H` in the socket
  (one store per cycle, replacing the Step 2 observer-only store) and
  runs the coprocessor to `H` only when `H - coprocessor_time() >=
  MAX_COPROCESSOR_SKEW`. Between those points the coprocessor lags by
  fewer than Δ cycles, which Step 2 permits.
- `TubeSocket::read()` and `write()` run the coprocessor to the stored
  `H` before performing the access, so every host register access is
  exact. Because `host_cycle(H)` runs before the host CPU's tick in the
  same `step()`, the stored `H` is the cycle of the access.
- (Issue #71 removed the Tube bus-stretch path: the ULA cannot stall the
  host, so there is no stretch cycle to keep in step.)
- Whenever the host stops, on a breakpoint or watchpoint hit and at the
  end of every `run()` chunk, `Machine` syncs the coprocessor to `H` so
  that a stopped machine presents both processors at the same time to the
  debugger and to `GetTubeState`. `pause()` is called from an RPC thread
  while `run()` may be executing on the emulation thread, so it syncs
  only when the machine is not running; a running machine syncs itself on
  the exit from `run()` that the pause causes. The emulation thread owns
  the coprocessor while `run()` executes and nothing else touches it.
- The debugger's single-step RPCs sync exactly: the debug-target
  contract gains `finish_step()`, a no-op hook symmetric with
  `prepare_for_step()`, which `DebuggerControlServiceImpl` calls after
  both `StepInstruction` and `StepCycle`; the host `Machine` runs the
  coprocessor to `H` in it, the parasite target leaves it empty. Single
  stepping the host therefore leaves the coprocessor at the host's cycle.
- Interrupt lines are unchanged in mechanism: the host's IRQ aggregator
  polls HIRQ every cycle and sees ULA state as of `C`; the coprocessor
  sees PIRQ and PNMI when it next runs. Both latencies are bounded by Δ,
  4 us, per Step 2.
- `MAX_COPROCESSOR_SKEW` stays at 8 in this step. Raising it is a later
  decision made against the measurements this step produces.

### What is removed

`TubeSocket::set_observer_host_time` and `observer_host_time_`: the socket
now always holds host time, and the Step 2 observer reads it.

### Tests

- The Step 2 skew tests pass unchanged: they assert the bound, and now
  observe intervals of up to Δ rather than 1.
- A test that the strategy actually batches: across a 65C02 boot the
  observer's `max_interval()` equals `MAX_COPROCESSOR_SKEW`, so the
  optimisation cannot be silently lost.
- Interrupt latency tests at the register level, driven through a
  `Machine` with the in-process ULA and a stub coprocessor: a coprocessor
  write to R4 with Q set is reflected on the host's IRQ input within Δ
  host cycles; a host write to R1 with I set is seen as PIRQ by the
  coprocessor on its next `run_until`, which arrives within Δ cycles.
- A test that pausing the host syncs the coprocessor to `H`: after
  `machine.pause()`, `coprocessor_time() == cycle_count`.
- Debugger single-step of the host keeps `coprocessor_time()` within one
  cycle of `cycle_count`.

### Measurement

Before and after, on the same build type and machine, report the
emulation thread's busy fraction and the parasite's share of it for the
idle-at-BASIC-prompt case and for a CE2023 run, using the method in
`docs/discussion/cross-emulator-tube-analysis.md` (macOS `sample`, five
seconds) or `perf` on Linux. Numbers, not a target; they decide whether
raising Δ is worth anything and they are the baseline for a fast core.

### Acceptance

Every suite in Step 2's list passes, including CE2023 and all three
scenario suites, against the batching strategy. This step changes
interrupt latency by up to 4 us of emulated time and nothing else; if any
test outcome changes, the cause is found before merge.


## Step 3b: an instruction-level coprocessor core (design only, not scheduled)

This section records a design that is deliberately not being implemented.
A non-historical turbo mode is not a goal for Beebium: the coprocessors run
at their historical clocks, and the project's view is that running the
machine at anything other than real speed stops being emulation. At real
speed the cycle-stepped 65C02 core is a few percent of one core after
Step 3, so a faster core would buy little for the 65C02 family. The design
is kept because it is the basis for the heavier CPUs that are planned
(NS32016, 80186, 80286, and to a lesser degree the Z80 and 6809), whose
cores will be instruction-level from the start and must meet the same
contract, and because it is the option to reach for if a turbo mode ever
does become a goal.

### What the parasite core does today

The 65C02 plugin runs the `M6502` library, a cycle-stepped model: each
coprocessor cycle is one call through a function pointer, followed by a
memory dispatch through `ParasiteMemoryMap` with its Tube-window check,
watch and trace checks, a PIRQ sample, NMI-handler tracking and a PNMI
sample. Roughly 11 ns per cycle at 3 million cycles a second. It is the
reference core and stays so.

### What an instruction-level core is

An interpreter that executes one whole instruction per dispatch, inside
the plugin, behind the unchanged `Coprocessor` and `Cpu6502DebugTarget`
interfaces, selectable per plugin configuration alongside the reference
core. It keeps four things exact and drops everything else.

Kept exact:

- **Cycle counts.** A per-opcode cycle table with the branch-taken and
  page-cross penalties, so the coprocessor's clock advances exactly as the
  cycle-stepped core's does. The `CoprocessorClock` budget is unchanged;
  the core runs instructions until the batch's cycles are spent.
- **Tube register accesses in host-time order** (contract clause 1). One
  rule: an instruction that touches the window at &FEF8-&FEFF executes
  only if it completes within the batch's budget; otherwise the batch
  stops before it. Instructions that do not touch the window may
  overshoot the budget by at most one instruction, the overshoot being
  deducted from the next batch. Overshoot is invisible to the host,
  whose only shared state with the coprocessor is the Tube registers and
  the interrupt lines.
- **Interrupt recognition at instruction boundaries.** PIRQ and PNMI are
  sampled once per instruction, which is when the CPU recognises them;
  the NMI-handler tracking that prevents nested NMIs carries over.
- **The boot-ROM overlay and its unmapping on the first Tube access.**

Dropped:

- **Bus-cycle simulation.** Memory is a flat 64 KB array with a flag for
  the ROM overlay and a shift-and-compare for the Tube window, in the
  manner of PiTubeDirect's fast core (`cross-emulator-tube-analysis.md`).
- **Dummy reads.** The page-cross and read-modify-write dummy accesses
  matter only when they land on the Tube window, and the reference core
  routes those through a side-effect-free peek whose result the CPU
  discards. A core that never issues them is identical at the ULA; this
  is why PiTubeDirect's cores are safe and why the CE2023 case stays
  correct.

### Consequences

- `Cpu6502DebugTarget::cpu()` returns the `M6502` library struct, and the
  state RPC reads interrupt flags from it. A core that does not use that
  struct must keep a shadow copy, or the interface must expose the program
  counter and interrupt state directly. The latter is the right fix and
  belongs with Step 1d. Watchpoints remain supported by checking accesses
  only when any are set.
- Acceptance would be differential: both cores run the same programs (the
  Klaus functional test, the Tube boot, CE2023) and must produce identical
  cycle counts and identical Tube register traces at every access.

### What it would buy

At real speed, under one percent of a core for the 65C02 against about
three now. Its value is for the heavy cores, where instruction level is
the only practical choice, and, if ever wanted, for a turbo coprocessor
where a 100:1 ratio (about 200 MHz-equivalent) would be a quarter of a
core rather than several cores. The Tube protocol tolerates a faster
parasite: every parasite-paced path is closed-loop, and the host-paced R3
transfers require only that the parasite be fast enough.


## Step 1d: a family-agnostic coprocessor debugger

### Goal

The debugger works for any coprocessor CPU family without the protos,
the server or the clients naming a family. The coprocessor describes its
CPU; the server serves the description; the clients render it. The same
description serves the host's own 6502, so there is one register model
end to end. The word "parasite" leaves the APIs and the code.

### What is already family-agnostic and stays

Memory addresses (32-bit), memory regions, breakpoints and watchpoints
as address ranges, execution control, stepping, the execution-state event
stream, and the coupled run and stop primitives of
`docs/discussion/debugger-requirements.md`. None of these change.

### The register model

```protobuf
message CpuDescriptor {
    string family = 1;                 // "6502", "z80", "6809", "ns32016", "80186"
    uint32 address_bits = 2;           // 16, 24, 32
    bool little_endian = 3;
    repeated RegisterDescriptor registers = 4;   // in display order
    repeated string signals = 5;       // interrupt line names, e.g. "IRQ", "NMI"
}

message RegisterDescriptor {
    string name = 1;                   // "A", "PC", "HL", "SP", "P"
    uint32 width_bits = 2;
    RegisterRole role = 3;             // NONE, PROGRAM_COUNTER, STACK_POINTER, FLAGS
    repeated string flag_names = 4;    // for FLAGS: bit 0 first, "" for unused
}

message CpuState {
    repeated RegisterValue registers = 1;   // same order as the descriptor
    repeated SignalState signals = 2;       // same order as descriptor.signals
    uint64 cycle_count = 3;
}

message RegisterValue { string name = 1; uint64 value = 2; }
message SignalState  { string name = 1; bool asserted = 2; bool pending = 3; bool in_handler = 4; }
```

Roles are the minimum clients need: which register is the program
counter, which the stack pointer, which the flags. More roles (segment
registers, banked sets) are added when a family that needs them arrives,
driven by a concrete example, not before.

For the 6502 family the descriptor lists A, X, Y, SP, PC and P, with P's
flag names N V - B D I Z C, and the signals IRQ and NMI. `in_handler` on
NMI carries the existing NMI-handler tracking; `pending` on NMI is the
latched edge, on IRQ the asserted-and-unmasked condition. The device
flag masks in today's `Cpu6502State` are Beebium-internal aggregator
state, not CPU state, and are dropped from the wire.

### RPC changes

In `debugger.proto`:

- `Get6502State` and `Set6502State`, and `Cpu6502State`, are removed.
- `GetCpuDescriptor(Empty) returns (CpuDescriptor)` is added.
- `GetCpuState(Empty) returns (CpuState)` and `SetCpuState(CpuState)
  returns (CpuState)` are added; `SetCpuState` accepts any subset of
  registers by name and returns the full state.
- The service `ParasiteDebuggerControl` is renamed
  `CoprocessorDebuggerControl`. `DebuggerControl` is unchanged in name and
  gains the same three RPCs in place of the 6502 pair.

Everything else in the file is unchanged. The protocol fingerprint is
resynchronised and all clients regenerated, released together, per the
project's no-backward-compatibility rule.

### Extension API

`Cpu6502DebugTarget` and `Cpu6502MemoryModel` are removed. The debug
contract becomes one interface, `CpuDebugTarget` (renamed from
`CoprocessorDebugTarget` because the host implements it too): the
descriptor (`cpu_descriptor()`, a plain C++ struct crossing the extension
API like `MemoryRegionDescriptor`, translated to proto in the service
layer), register access by index (`register_value(i)`,
`set_register_value(i, v)`), signal state by index, and the existing
execution control, flat memory access, region model with `machine_type()`
(the machine's identity such as "model-b-romram" or "Tube65C02", a
different concept from the descriptor's CPU family and kept separate),
breakpoint and watchpoint surface, `prepare_for_step` and `finish_step`.
It carries no `M6502` reference and no 6502 register names, and no
16-bit address assumption: every address and program-counter parameter
on the interface is 32 bits, `BreakpointEntry` and `WatchpointEntry`
hold 32-bit ranges, and the service bounds addresses by the descriptor's
`address_bits` rather than by `0xFFFF`. A 16-bit family's implementation
narrows inside itself.

`DebuggerControlServiceImpl` stops being a template: it is one concrete
service over a `CpuDebugTarget&`, and the two generated gRPC service
bases (`DebuggerControl`, `CoprocessorDebuggerControl`) each delegate to
an instance of it. The host side is a thin `HostDebugTarget` adapter that
delegates to `Machine`, so the hot `Machine` template carries no debug
vtable; the coprocessor side is the runner. A shared 6502 descriptor
helper serves both. The server no longer casts to a family; the "no
debugger for family X" path from Step 1b becomes unreachable and is
removed, as is the dead `ParasiteServer.hpp` from the multi-process era.

### Naming

"Parasite" is retired from all names: `ParasiteRunner`, `ParasiteCpu`,
`ParasiteMemoryMap`, `ParasiteDebuggerAdapter`, `TubeParasiteBackend`,
`parasite_read`/`parasite_write`/`parasite_peek` on the ULA, the Python
`connect_parasite()`, the `TubeSystem` parasite naming, the TypeScript
equivalents, and comments. The replacement word is "coprocessor"
(`CoprocessorRunner`, `CoprocessorCpu`, `coprocessor_read`, ...). The
Tube ULA's two sides are "host" and "coprocessor". Acorn's documents
still say parasite and the references to them may quote it. The rename
covers the protos too (`tube.proto`'s parasite fields,
`TubeParasiteStatus`). `econet.proto`'s `read_stretch_parasite_ticks`
and the socket counter behind it are a dead diagnostic and are removed
rather than renamed. The rename is one mechanical commit at the end of
the step, after the functional changes, so the review can see each
separately. Documentation prose is the architect's pass, after merge. The vector test's BASIC
transliteration keeps the pin names the original program uses.

### Clients

- **Python.** `cpu.registers` returns an ordered mapping of register name
  to value with attribute access, built from the descriptor, so existing
  6502 code reading `.a`, `.x`, `.pc` keeps working and a Z80's `.hl`
  appears with no client change. `cpu.descriptor` exposes the descriptor;
  `cpu.signals` the interrupt lines. `Registers` as a fixed dataclass
  goes. `cpu.registers.status` is built from the FLAGS-role register and
  its `flag_names`: flags are read by name (`status.flag("C")`,
  `status["C"]`), rendered by name, and the 6502 property names (`carry`,
  `zero`, ...) are kept as aliases that exist only when the descriptor
  carries the corresponding flag names. `connect_parasite()` becomes `connect_coprocessor()`. The
  disassembler stays 6502-only and client-side; other families bring
  their own later.
- **TypeScript.** The same shape.
- **macOS.** Stub regeneration only; the app does not use the CPU state
  RPCs.
- The generated READMEs are regenerated and their snippets re-run.

### Tests

- Proto and server: descriptor and state round trips for the host 6502
  and the coprocessor 65C02 through both services; `SetCpuState` with a
  subset; unknown register names rejected with a message naming them.
- The stub-family test from Step 1b becomes its opposite: a stub
  coprocessor with a made-up family and register set is fully served,
  descriptor, state and set, with no server code knowing its names.
- Existing debugger tests (host and coprocessor breakpoints, watchpoints,
  stepping, cross-processor stop) pass with the 6502 pair removed.
- Python and TypeScript client unit tests for the mapping and attribute
  access, and the existing integration tests that read registers through
  `connect_parasite()` migrated to `connect_coprocessor()`.

### Acceptance

Every suite in Step 3's list unchanged in outcome; the Python and
TypeScript client suites; the README snippet regeneration check; the
fingerprint check; `grep -rni parasite src clients/beebium-python-client/src
clients/beebium-typescript-client/src` finding only quotations of Acorn
documents and the vector test's pin names.


## Later steps (for orientation, not for implementation now)

- **Step 1c, the 65C102 4 MHz second processor.** Specified below.
- **Step 1d, family-agnostic coprocessor debugger.** Specified above.
- **Step 2, skew contract.** Specified above.
- **Step 3, batching.** Specified above.
- **Step 3b, an instruction-level coprocessor core.** Designed above,
  deliberately not scheduled; the design basis for the heavy CPU cores.
- **Step 4, execution strategies.** A second implementation of the same
  contract that runs the coprocessor on a worker thread, spinning during
  the host's pacing burst and parked while the host sleeps, for
  coprocessors expensive enough to justify it. Measured against the
  single-threaded strategy before adoption.
