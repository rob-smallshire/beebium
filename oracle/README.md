# Beebium Oracle -- differential testing against jsbeeb

A development harness that runs the same workload on Beebium and on
[jsbeeb](https://github.com/mattgodbolt/jsbeeb), a third-party BBC Micro
emulator, and compares their machine state. jsbeeb plays the *test oracle*: the
assumed source of truth. It is not part of the build, is not run in CI, and is
not a distributable -- it is a bug-hunting instrument, picked up when a defect
resists ordinary debugging and put down again afterwards.

**Status: dormant, preserved deliberately.** It was driven hard in March 2026
against the Chuckie Egg 2023 Tube hang and earned its keep (see *What it
found*). Nothing has needed it since. It is kept working rather than deleted
because the next hard bug may want it.

## What it compares

Per instruction, via `DiffRunner.stepBoth(n)`: step jsbeeb n instructions, step
Beebium n instructions, diff the result.

| Component | Fields |
|---|---|
| CPU | `a`, `x`, `y`, `sp`, `pc`, `p` |
| System VIA, User VIA | 19 each: `ora/orb/ira/irb`, `ddra/ddrb`, `t1c/t1l/t2c/t2l`, `acr/pcr/ifr/ier/sr`, `ca1/ca2/cb1/cb2` |
| CRTC | R0-R17 plus the address register |
| Video ULA | control byte and all 16 palette entries |
| Memory | opt-in `AddressRange[]`, byte by byte |
| Cycles | gathered, **never asserted** -- see below |

Not compared: framebuffer pixels, sound, FDC state, and the Tube parasite
(jsbeeb-side extraction exists, `jsbeeb-oracle.ts`, but has no Beebium
counterpart in this client).

**Lockstep is by instruction count, not by cycle**, deliberately. Cycle counts
legitimately differ between the two emulators for reasons that are not defects
on either side, so asserting on them produces noise, not signal. See
`CYCLE_DIFFERENCE_INVESTIGATION.md`.

Making the two comparable at all needs normalisations, which are the subtlest
part of the harness and the first thing to check if a comparison looks wrong:

- **PC**: Beebium's points at the *next byte to fetch*; the client subtracts one
  on read and adds one when setting breakpoints.
- **P**: bits 4-5 (B and unused) masked off both sides -- `P_STATUS_MASK`.
- **VIA timers**: jsbeeb keeps doubled internal counters; the wrapper converts
  to Beebium's `effective_t1()` semantics. T2's latch is masked to 8 bits
  because the 6522 does not latch T2CH. Rationale in `src/types.ts`.
- **SP after reset**: the oracle *patches jsbeeb*, forcing `s = 0xFD`, because
  jsbeeb has no reset sequence. Applied in `reset()` only, not `initialize()`.

## Architecture

| File | Role |
|---|---|
| `src/types.ts` | Shared state interfaces, `P_STATUS_MASK`, and the timer-normalisation rationale |
| `src/jsbeeb-oracle.ts` | Wraps jsbeeb: model selection, Tube, disc loading, `runCycles`, `type()`, `runUntilParasiteAddress` |
| `src/beebium-client.ts` | Hand-rolled gRPC client (see below) |
| `src/diff-runner.ts` | The comparison engine and bisection diagnostics |
| `src/server-fixture.ts` | Spawns a real server on `--port 0`, scrapes the port from stdout, reaps strays on exit |
| `src/index.ts` | Barrel re-export |

Two pieces of coupling are invisible from the source and will be the first
things to break:

**jsbeeb is not an npm dependency.** It is imported by relative path from a
sibling checkout (`../../../jsbeeb`), behind `@ts-expect-error`. Two consequences
follow. `package.json` lists `argparse`, `event-emitter-es6`, `fflate`, `pako`,
`smoothie` and `underscore`, none of which any file here imports -- they are
*jsbeeb's* dependencies, duplicated so its bare-specifier imports resolve out of
`oracle/node_modules`. And `public` is a symlink to `../../jsbeeb/public` so
jsbeeb finds its ROMs relative to cwd.

**The Beebium client is hand-rolled and does not use `@beebium/client`.** It
`protoLoader.loadSync`s `debugger.proto` and `system.proto` straight out of the
C++ tree and talks to `beebium.DebuggerControl` and `beebium.SystemService`. It
predates the published client and reaches debugger surfaces the published client
does not expose. (`@beebium/client` is declared as a dependency in
`package.json` but never imported -- vestigial.)

## Running it

Prerequisites, all of which are positional and none of which are checked:

- a jsbeeb checkout at `../../jsbeeb` relative to the repo root
- a built server (`build/src/server/beebium-model-b*`)
- ROMs in `roms/`
- `npm install` here (which also pulls jsbeeb's transitive deps)

```bash
cd oracle
npm test                       # vitest, tests/**/*.test.ts
npx vitest run tests/convergence.test.ts
```

**Paths are hardcoded to a developer machine.** `server-fixture.ts` returns a
literal `/Users/rjs/Code/beebium/build/src/server/...`, and the ROM directory is
copy-pasted into the server tests. This is why it cannot run in CI, and the
first thing to fix if that ever becomes desirable. It is not desirable today.

## What the tests actually cover

Honest summary -- of seven test files, **one** asserts that the two emulators
agree:

| File | What it really does |
|---|---|
| `convergence.test.ts` | **The genuine differential test.** Runs both to BASIC entry `$8000`; asserts 0 CPU divergences and 0/256 zero-page byte differences. Cycle difference logged only. |
| `basic.test.ts` | Smoke tests both sides. Its two "differential" cases assert only that a result came back and that `divergences` is an array -- **never that the states agree**; `result.match` merely gates a `console.log`, so they pass on any divergence. Not carelessness: the comment records that reset legitimately diverges (jsbeeb sets PC in 0 cycles, Beebium takes 7), so equality could not be asserted as written. Asserting the *expected* divergence set, and failing on anything else, is the obvious improvement if this is revived. |
| `boot-check.test.ts` | Beebium only: boots 3s, scrapes screen RAM, asserts `BBC` appears. |
| `breakpoint-debug.test.ts` | Beebium only: asserts a breakpoint reports the exact requested PC (guards the +/-1 normalisation). |
| `jsbeeb-sp-bug.test.ts` | Asserts jsbeeb's SP is `0x00` after reset -- i.e. **asserts the bug**. Inverted tripwire: it fails when jsbeeb is fixed. |
| `server-status.test.ts` | Infrastructure: `WatchServerStatus` yields READY, `waitForReady` works. |
| `video-ula-clock.test.ts` | Diagnostic, not a test: measures first CA1 in jsbeeb@2MHz / jsbeeb@1MHz / Beebium. Passes state between cases via `globalThis`, so it is order-dependent. |

Roughly half of `DiffRunner` and the clients is **dead code**:
`findCycleDifferenceChanges`, `findFirstPcDivergence`, `synchronizeAfterReset`,
`peekRegion`, `stepCycle`, `runUntilAddress`, `loadDisc`, `getParasiteCpuState`,
`runUntilParasiteAddress`. These are the CE2023 scaffolding's remains: the tests
that drove them were deleted in `af1474d8` once the investigation concluded, and
the machinery was left for the next one. Treat it as a toolbox, not as an
API with users. Note `synchronizeAfterReset` is also **unsound** -- it decides
which side to advance by comparing PCs numerically, and PC ordering says nothing
about execution progress. Latent, since nothing calls it.

## What it found

- **`bus_stretch_cancel` data loss** on Tube R1/R3/R4 host writes (`47955495`) --
  real, fixed, verified: *"All deltas zero. Every byte written was read."*
- **A genuine defect in b2**, found while cross-checking the CE2023 hang, sent
  upstream as tom-seddon/b2 PR #569.
- **The limits of the oracle premise** -- see `CYCLE_DIFFERENCE_INVESTIGATION.md`.
  The 6845 clock rate at reset differs between the two. Chasing it down ended at
  the circuit diagram: **the Video ULA has no reset pin** -- all 28 are power,
  A0, chip select, the data bus, the clock divider, RGB and four control signals
  -- so &FE20 comes up holding whatever its latches settled to, and no manual
  gives it a reset value because there is none to give. Five emulators model a
  defined power-on state for a device that has none, and split two-two over it.
  jsbeeb was never ground truth here; there was nothing to be right about.

The pattern is worth carrying forward: **the harness pays off where the two
disagree about something the hardware defines, and misleads where it does not.**
Prefer it for data-path questions (was the byte delivered?) over timing-and-
initial-state questions.

## Related documentation

- `CYCLE_DIFFERENCE_INVESTIGATION.md` -- the cycle-divergence log, and the
  verdict on which of the findings are defects (none) versus undefined-behaviour
  divergences (all of them).
- `docs/debugging-cookbook.md` -- *Cross-Emulator Differential Testing* documents
  the technique and the jsbeeb API this harness drives. Written when the CE2023
  scaffolding was deleted, so it is the distilled method that outlived the code.
- `docs/discussion/chuckie-egg-2023-tube-hang.md` -- the investigation this was
  built for, including the disproven-hypothesis list.
- `docs/discussion/cross-emulator-tube-analysis.md`
