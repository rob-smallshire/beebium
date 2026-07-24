# games-soak

Soak the emulator the way it is actually used: playing games, for a long time,
with the real macOS frontend attached. Built to reproduce the intermittent
"emulator freezes after a long session" failures, which fall into two modes:

- **Mode B (server/core stall)** — the emulated cycle counter stops advancing.
  gRPC usually stays responsive, so `cycle_count` (not gRPC liveness) is the
  signal. This harness watches it directly and captures on a stall.
- **Mode A (frontend-only stall)** — the emulator keeps advancing but the
  attached frontend stops rendering. Cannot be auto-detected from outside, so
  the harness attaches a real frontend (a human watching sees the freeze) and,
  on any captured stall, samples the frontend process too.

The frontend is attached deterministically with the `beebium://` URL scheme,
pointed at the server's exact ephemeral port — no mDNS required.

## Running

```bash
# Non-Tube games (Revs, ...), headless (Mode B only):
uv run --directory integration_tests/games-soak games-soak

# With the macOS frontend attached (needed to observe Mode A):
uv run --directory integration_tests/games-soak games-soak \
    --macos-app ~/Library/Developer/Xcode/DerivedData/Beebium-*/Build/Products/Debug/Beebium.app

# Tube games (Elite, Chuckie Egg):
uv run --directory integration_tests/games-soak games-soak --tube --macos-app <path>
```

Runs indefinitely by default (`--max-minutes 0`), cycling discs through one
long-lived server. On a stall it writes a report to `reports/` — gRPC liveness
probes plus `sample` and `lldb thread backtrace all` for **both** the server and
the frontend — then holds the frozen processes for `--hold-minutes` so they can
be inspected by hand. Exit code 1 = a stall was reproduced, 0 = clean run.

## Adding a game

Boot recipes live in `src/games_soak/games.py`, each copied from the matching
python-client integration test (`test_revs_timing.py`,
`test_tube_chuckie_egg.py`, `test_tube_elite.py`). To add a game, work out its
boot sequence by hand — reading landmark text off the screen — and append a
`Game` record. Tube and non-Tube games run in separate invocations (`--tube`).

## Requirements

- Built servers: `cmake --build build --target beebium-servers`.
- For frontend attach: a built `Beebium.app` with the `beebium://` URL scheme
  (any recent build registers it).
- Game discs under `discs/games/` and `tests/assets/discs/` (the recipes point
  at specific filenames).
