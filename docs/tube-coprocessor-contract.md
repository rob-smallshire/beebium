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

    // True while the debugger has stopped this coprocessor. See "Pause".
    virtual bool is_paused() const = 0;

    // Hardware reset, propagated from the host's reset line through the
    // Tube cable. Restarts the CPU at its reset vector and rebases time.
    virtual void reset() = 0;

    // Exact clock ratio, coprocessor cycles per host cycle.
    virtual ClockRatio clock_ratio() const = 0;

    // Diagnostic: current parasite PC, or 0xFFFF if not applicable.
    virtual uint16_t diag_pc() const { return 0xFFFF; }
};

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
  `run_until`; the coprocessor handles it. The method stays on the
  interface because the debugger's cross-processor stop logic uses it.

### Reset

- `reset()` restarts the coprocessor and discards its time base. The next
  `run_until(t)` establishes `t` as the new origin `t0` with zero cycles
  due. This is required because a hard host reset zeroes
  `state_.cycle_count`, so host time legitimately goes backwards across a
  reset.

### `TubeSocket` changes

`TubeSocket` stops owning any notion of the clock ratio or the fractional
phase. It becomes:

```cpp
void install_coprocessor(Coprocessor* coprocessor);   // replaces install_parasite
void remove_coprocessor();                            // replaces remove_parasite
void run_coprocessor_until(uint64_t host_cycle);      // replaces tick_parasite and tick_parasite_stretch
uint16_t diag_parasite_pc() const;                    // unchanged
```

`set_parasite_clock_ratio`, `tick_parasite`, `tick_parasite_stretch` and the
`parasite_phase_` / `parasite_clock_num_` / `parasite_clock_den_` members
are removed. `run_coprocessor_until` is a no-op when nothing is installed.
`TubeSocket::reset()` continues to reset the backend and the installed
coprocessor.

### `Machine::step()` changes

Today `Machine::step()` calls `tick_parasite()` before the host CPU tick on
a normal cycle and `tick_parasite_stretch()` on a bus-stretched cycle. Both
become a single call, in the same place on both paths:

```cpp
state_.memory.tube_socket.run_coprocessor_until(state_.cycle_count);
```

Because `cycle_count` advances by one per `step()` on both paths, the
coprocessor sees exactly the sequence of host times it sees today, and the
cycles it runs per call are exactly the 1, 2, 1, 2 pattern the current 3:2
accumulator produces. Step 1 must be behaviourally identical to the current
code: same interleaving, same results.

### `CoprocessorClock` helper

So that every extension does not reimplement the rational arithmetic, core
provides a small value type, `src/core/include/beebium/tube/CoprocessorClock.hpp`:

```cpp
class CoprocessorClock {
public:
    explicit CoprocessorClock(ClockRatio ratio);

    // Discard the time base: the next cycles_due() call defines the origin.
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

Behavioural identity is the acceptance criterion for Step 1. If any
scenario or boot test changes outcome, the change is a defect in the
migration, not an acceptable side effect.


## Later steps (for orientation, not for implementation now)

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
