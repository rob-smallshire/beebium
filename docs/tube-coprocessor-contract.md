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
part) is a 65C02-family CPU at 4 MHz with 64 KB of RAM and the same 2 KB
Tube client ROM. From the software's point of view it is the 65C02 second
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
        "size": 2048,
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
  plugin ships its own firmware: the 65C02 plugin the 6502 Tube client
  v1.10 (`acorn-tube-6502_1_10.rom`, 2048 bytes, MD5
  `cd6ba85e22adec70b6d863de4c053db7`), the 65C102 plugin the 65C102 Tube
  client v1.20 (`acorn-tube-65c102_1_20.rom`, 2048 bytes, MD5
  `83d73e0e78693bb4b43e7cb18e58d556`), which is a different build with its
  own banner, "Acorn TUBE 65C102 Co-Processor". No plugin references
  another plugin's directory. The 65C102 image is the one B2 and B-Em ship
  and matches the upper half of the full 4 KB EPROM dump in Toby Lobster's
  ROM library. Another 2 KB image circulates as "65C102 TUBE 1.20" (MD5
  `f0555114f7a18f727e9ca14effebcc95`) that the library annotates as saved
  from a RAM copy after self-modification: its startup RTS at &F85E has
  become TYA and its NMI vector has been rewritten, so booted as a ROM it
  goes straight to the "*" supervisor and never prints the banner. Only
  the image from the chip boots.

- **Resolution is the extension API's job.** Add to `Extension` (or
  `ExtensionContext`, developer's choice, say which) a
  `rom_filepath(std::string_view key)` that returns the path of the
  declared ROM resolved against the manifest's `manifest_dirpath`, and a
  `load_rom(key, span)` convenience that reads it and checks the declared
  size. Both report a clear error naming the expected path when the file
  is missing or the wrong size. An explicit `rom` configuration parameter
  still overrides the packaged file, for users supplying a different
  client ROM. For the 6502 second processor the declared size is 2048,
  the mapped upper half of the board's 4 KB EPROM; a 4096-byte full dump
  whose first 2048 bytes are all &FF is accepted and its upper half used,
  with a log line saying which form was found, since such dumps circulate.
  Any other size, or a 4096-byte file whose lower half is not blank, is
  rejected. `SecondProcessor65C02Extension::load_rom` uses these and
  drops its include of `beebium/server/RomPaths.hpp`; the plugin no
  longer needs the server include directory at all.

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
  produce the specified error; a 4096-byte image with a blank lower half
  loads to the same 2048 bytes as the canonical file, and one with a
  non-blank lower half is rejected.
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


## Later steps (for orientation, not for implementation now)

- **Step 1c, the 65C102 4 MHz second processor.** Specified below.
- **Step 1d, family-agnostic coprocessor debugger.** `ParasiteDebuggerControl`
  is the 6502 proto under another name: `Cpu6502State`, 16-bit addresses.
  Serving the other families needs a debugger surface described in terms
  of a family descriptor (register names, widths and values; address
  width; memory regions) rather than a fixed 6502 shape, on the server, in
  the protos and in the Python, TypeScript and macOS clients. That is a
  design of its own, building on `docs/discussion/debugger-requirements.md`;
  `CoprocessorDebugTarget` is the seam it plugs into.
- **Step 2, skew contract.** Specified above.
- **Step 3, batching.** `Machine::step()` calls `run_coprocessor_until` at
  most every Δ host cycles, or immediately before any host access to the
  Tube registers, whichever comes first. This is where a heavy coprocessor
  core gets to run a tight loop.
- **Step 4, execution strategies.** A second implementation of the same
  contract that runs the coprocessor on a worker thread, spinning during
  the host's pacing burst and parked while the host sleeps, for
  coprocessors expensive enough to justify it. Measured against the
  single-threaded strategy before adoption.
