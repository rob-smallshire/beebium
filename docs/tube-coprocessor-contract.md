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
| Parasite | Acorn's name for the coprocessor's CPU. Kept for the CPU and its runner (`ParasiteRunner`, `ParasiteCpu`). |
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

### Time

- `host_cycle` is the host's cumulative cycle count, `state_.cycle_count`.
- Between resets, successive `run_until` arguments are non-decreasing. A
  call with a smaller value than the previous call is a contract violation;
  implementations assert it in debug builds.
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

### Origin and reset

- A coprocessor has no time base until its first `run_until(t)`, which
  establishes `t` as the origin `t0` with zero cycles due. A coprocessor
  installed into a machine that has already been running for a long time
  therefore starts from the host's current time; it never runs a catch-up
  burst from host time zero.
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
- A build with `-DBEEBIUM_BUILD_SERVICE=OFF` still configures and builds
  the static extension library and its tests.

### Consequences to state

- A server started from a build tree without the plugin built, or an
  installed tree missing `extensions/acorn-65c02-coprocessor/`, no longer
  has a Tube. This is the same situation as every other plugin today and
  the artifacts already ship the tree; it is worth a sentence in
  `docs/deployment.md`.


## Later steps (for orientation, not for implementation now)

- **Step 1c, the 65C102 4 MHz second processor.** Once the plugin exists,
  a second manifest and entry point in the same source directory, ratio
  2/1, CLI `tube-65c102`. Software-identical to the 65C02 second processor;
  only the clock differs. Gives the programme two coprocessor instances to
  exercise the contract with.
- **Step 1d, family-agnostic coprocessor debugger.** `ParasiteDebuggerControl`
  is the 6502 proto under another name: `Cpu6502State`, 16-bit addresses.
  Serving the other families needs a debugger surface described in terms
  of a family descriptor (register names, widths and values; address
  width; memory regions) rather than a fixed 6502 shape, on the server, in
  the protos and in the Python, TypeScript and macOS clients. That is a
  design of its own, building on `docs/discussion/debugger-requirements.md`;
  `CoprocessorDebugTarget` is the seam it plugs into.
- **Step 2, skew contract.** Name the maximum permitted skew Δ, in host
  cycles, between the host's time and the coprocessor's. Register accesses
  remain exact; interrupt-line delivery is permitted up to Δ of latency.
  Add a test that asserts the socket never lets the two diverge by more
  than Δ.
- **Step 3, batching.** `Machine::step()` calls `run_coprocessor_until` at
  most every Δ host cycles, or immediately before any host access to the
  Tube registers, whichever comes first. This is where a heavy coprocessor
  core gets to run a tight loop.
- **Step 4, execution strategies.** A second implementation of the same
  contract that runs the coprocessor on a worker thread, spinning during
  the host's pacing burst and parked while the host sleeps, for
  coprocessors expensive enough to justify it. Measured against the
  single-threaded strategy before adoption.
