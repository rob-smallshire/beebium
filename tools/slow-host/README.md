# Slow-host tools

Tools for reproducing, on a fast development machine, the timing failures that
show up on slow or heavily loaded CI runners. Each has a header describing
what it measures; this page says which to reach for and how.

| Tool | What it does |
|------|--------------|
| `slow_host_plugin.py` | pytest plugin: serves every 1x speed request at `SLOW_HOST_SPEED` (default 0.3) |
| `renice_servers_plugin.py` | pytest plugin: renices every server a test launches to +20 |
| `noisy_load.py` | CPU hogs whose number changes at random every 0.2-2 s |
| `pacing_under_noise.py` | stop/resume trials under noisy load, printing the rate ratio beside the server's pacing deficit (#137) |
| `plain_break_loop.sh` | runs one test repeatedly on a simulated slow host, keeping each failure's screen (#132) |
| `break_timing_sweep.py` | presses BREAK at exact emulated delays after the Tube banner (#132) |

## A slow host versus a loaded host

There are two different ways a CI runner is slow, and they break different
tests:

- **A slow host** runs the machine below real time throughout. Anything that
  holds a key or waits for a fixed wall-clock time sees fewer emulated cycles
  than on a fast host (#125). `slow_host_plugin.py` simulates this exactly:
  the tests' real-time phases run at, say, 0.3x.
- **A loaded host** runs the machine at a rate that changes from second to
  second as other work comes and goes. Anything that compares two short
  wall-clock windows sees swings both ways that are nothing to do with the
  emulator (#137). Reproduce this with `noisy_load.py` and
  `renice_servers_plugin.py` together.

**On an M-series Mac, CPU hogs alone barely slow a server**: its performance
cores absorb them. Renice the server below the hogs (`renice_servers_plugin.py`
for tests, or `renice +20 -p <pid>`) and the load bites, reproducing ratio
swings like those on the macOS x86_64 runner.

## Usage

Run pytest from `clients/beebium-python-client`, with this directory on
`PYTHONPATH` for the plugins.

A slow host, for one test file:

```bash
SLOW_HOST_SPEED=0.3 PYTHONPATH=../../tools/slow-host \
    uv run --group test python -m pytest tests/test_autoboot.py -p slow_host_plugin -s
```

A loaded host: start the noisy load, run the tests with the servers reniced,
then stop the load (it kills its hogs by PID on exit):

```bash
python ../../tools/slow-host/noisy_load.py & LOAD=$!
PYTHONPATH=../../tools/slow-host \
    uv run --group test python -m pytest tests/test_idle_pacing.py -p renice_servers_plugin
kill $LOAD
```

The two plugins combine: `-p renice_servers_plugin -p slow_host_plugin`.

To see what the server's pacing does across a stop under load, from the
repository root:

```bash
uv run --project clients/beebium-python-client \
    python tools/slow-host/pacing_under_noise.py --trials 12
```

To measure how often a test fails at a given speed, keeping each failure's
screen (from anywhere):

```bash
tools/slow-host/plain_break_loop.sh 0.4 30 \
    tests/test_autoboot.py::test_auto_boot_link_makes_plain_break_boot
```

## Measuring timing properties robustly

When a test checks a timing property, prefer the server's own accounting over
wall-clock throughput. GetPacingStats reports the pacing controller's deficit
(`controller_drift`: target minus actual cycles, positive when behind) and the
pacing timer's tick count (`ticks_executed`), and neither is disturbed by host
load. `tests/test_idle_pacing.py` shows the pattern.
