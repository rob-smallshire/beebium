# Beebium Python Client

Python client for the Beebium BBC Micro emulator. Designed for pytest-based testing of BBC Micro software.

## Installation

### From a checkout (recommended: uv)

This repository is a [uv](https://docs.astral.sh/uv/) project (`pyproject.toml`
+ `uv.lock`). `uv` manages an isolated environment for you, so it works on
distributions that enforce [PEP 668](https://peps.python.org/pep-0668/) (Arch,
recent Debian/Ubuntu, Fedora) where a system-wide `pip install` is blocked with
`error: externally-managed-environment`.

```bash
cd clients/beebium-python-client

# Run anything in the project environment (uv creates it on first use):
uv run python examples/serial_demo.py --port 50071

# Run the test suite. Development tooling (pytest, grpcio-tools) lives in the
# `dev` dependency-group, which uv installs by default -- no extra flags:
uv run python -m pytest

# Build the wheel and run tests against it in a fresh venv. With no arguments
# only the packaging assertions in tests_packaging/ run; pass pytest arguments
# to run any other tests against the installed wheel (CI runs the whole suite
# this way, so packaging faults are caught rather than passed over by a
# source-tree run). The plugin finds roms/ and the checkout's server build from
# pytest's rootdir, so no env vars are needed in-repo:
bash scripts/test-wheel.sh
bash scripts/test-wheel.sh tests/test_keyboard.py -q
# BEEBIUM_ROM_DIR / BEEBIUM_SERVER still override the in-checkout discovery.

# Or materialise a .venv you can activate (the dev group is included by default):
uv sync
```

No `pip` is required, and nothing is installed into your system Python.

### From a checkout (plain virtualenv)

PEP 668 only blocks the *system* environment, so `pip` works fine inside a
virtualenv:

```bash
cd clients/beebium-python-client
python -m venv .venv
. .venv/bin/activate
pip install -e .                 # the client
pip install --group dev          # dev tooling (pip >= 25.1); or just use uv
```

### From PyPI

```bash
pip install beebium            # core client
pip install beebium[imaging]   # + Pillow for image capture
pip install beebium[discovery] # + zeroconf for mDNS server discovery
```

Development tooling is not a published extra -- contributors get it from the
`dev` dependency-group via uv (above).

On a PEP 668 system, run these inside a virtualenv (above) or via
`uv pip install` / `pipx`.

## Running the examples

The `examples/` scripts need a running `beebium-server`. Build the servers
first (see the top-level build docs); they appear at
`build/src/server/beebium-model-b` (and `-plus`, `-plus-128k`, `-romram`).

Either attach to a server you start yourself:

```bash
# terminal 1
./build/src/server/beebium-model-b --mos roms/acorn-mos_1_20.rom --port 50071
# terminal 2
cd clients/beebium-python-client
uv run python examples/serial_demo.py --port 50071
```

…or let an example launch (and stop) its own server. Point it at the server
binary via `--server` / `$BEEBIUM_SERVER` and a MOS ROM via `--mos`:

```bash
cd clients/beebium-python-client
export BEEBIUM_SERVER="$PWD/../../build/src/server/beebium-model-b"
uv run python examples/serial_demo.py --mos ../../roms/acorn-mos_1_20.rom
```

`serial_demo.py` defaults `--mos` to `<repo>/roms/acorn-mos_1_20.rom` when that
file exists, so from a full checkout `uv run python examples/serial_demo.py`
just works.

## Quick Start

```python
from beebium.client import Beebium

# Connect to an existing server (default port 48875 / 0xBEEB)
with Beebium.connect() as bbc:
    bbc.debugger.stop()
    print(f"PC = ${bbc.cpu.pc:04X}")

# Launch and manage a server
with Beebium.launch(mos_filepath="acorn-mos_1_20.rom", basic_filepath="bbc-basic_2.rom") as bbc:
    bbc.keyboard.type("PRINT 42")
    bbc.keyboard.press_return()
```

## Features

### Server Management

```python
# Auto-allocate a free port
with Beebium.launch(mos_filepath="acorn-mos_1_20.rom") as bbc:
    print(f"Server running on {bbc.target}")

# Specify a port
with Beebium.launch(mos_filepath="acorn-mos_1_20.rom", port=48876) as bbc:
    ...
```

### Debugger Control

```python
# Execution control
bbc.debugger.stop()           # Pause execution
bbc.debugger.run()            # Resume execution
bbc.debugger.reset()          # Reset the machine
bbc.debugger.step(10)         # Step 10 instructions
bbc.debugger.step_cycles(100) # Step 100 cycles

# Run until a (temporary) breakpoint fires -- supports a condition
state = bbc.debugger.run_to(0xC000, condition="A == 0x42")

# add_breakpoint returns a Breakpoint you can act on
bp = bbc.debugger.add_breakpoint(0xC000, condition="A == 0x42")
bp.disable()                    # toggle without removing
bp.enable()
bp.remove()                     # or bbc.debugger.remove_breakpoint(bp)
bbc.debugger.clear_breakpoints()

# ...or scope it to a block (removed automatically on exit)
with bbc.debugger.breakpoint(0xC000, condition="hits == 3") as bp:
    # race-free: subscribes before resuming, unlike run() + wait_for_stop()
    bbc.debugger.run_and_wait_for_stop()

# State queries
state = bbc.debugger.get_state()
print(f"Running: {state.is_running}, Cycles: {state.cycle_count}")
```

### CPU Register Access

```python
# Read all registers as one coherent snapshot (one request)
regs = bbc.cpu.registers
print(regs)  # A=.. X=.. Y=.. SP=.. PC=.. P=.. [flags]

# Convenience single-register access (each read is its own snapshot)
if bbc.cpu.a == 0:
    print("Accumulator is zero")

# Atomic partial write; returns the complete new register snapshot
new = bbc.cpu.update(pc=0xC000, a=0x42)

# The individual setters route through update()
bbc.cpu.pc = 0xC000

# Status flags live on the decoded status register
if regs.status.zero:
    print("Zero flag is set")
```

### Memory Access

Memory access is explicit about side effects and supports both flat address space and named region access.

#### Address Space Access (16-bit flat addressing)

```python
# Side-effecting access (through memory bus like real hardware)
value = bbc.memory.address.bus[0x1000]
bbc.memory.address.bus[0x2000] = 0x42

# Range read (returns bytes)
data = bbc.memory.address.bus[0x1000:0x1010]

# Range write
bbc.memory.address.bus[0x2000:0x2010] = bytes([0x00] * 16)

# Side-effect-free peek (for I/O addresses)
value = bbc.memory.address.peek[0xFE4D]
data = bbc.memory.address.peek[0xFE40:0xFE50]

# Sequential read/write
data = bbc.memory.address.bus.read(0x1000, 16)
bbc.memory.address.bus.write(0x2000, b"HELLO")

# 16-bit little-endian words -- follow a 6502 pointer or vector
oswrch = bbc.memory.address.peek.word(0x020E)          # read the WRCHV vector
bbc.memory.address.bus.set_word(0x0070, 0x1234)        # write a 16-bit pointer

# Typed access using struct format strings (other widths / byte orders)
word = bbc.memory.address.bus.cast("<H")[0x0070]       # Read 16-bit little-endian
bbc.memory.address.bus.cast("<H")[0x0070] = 0x1234     # Write 16-bit little-endian
words = bbc.memory.address.bus.cast("<H")[0x70:0x78]   # Read 4 words as tuple

# Load/save binary files
bbc.memory.address.bus.load(0x1900, "mygame.bin")
bbc.memory.address.bus.save(0x1900, 0x1000, "dump.bin")

# Fill memory range
bbc.memory.address.bus.fill(0x1000, 0x2000, 0x00)
```

#### PC-Context Access (for B+ shadow RAM)

On BBC Model B+, memory access at 0x3000-0x7FFF is routed based on the program counter of the executing code. Use `with_pc()` to query what code at a given PC would see:

```python
# What MOS code (at 0xD000) would see at address 0x5000
mos_view = bbc.memory.address.peek.with_pc(0xD000)[0x5000]

# What user code (at 0x1000) would see at the same address
user_view = bbc.memory.address.peek.with_pc(0x1000)[0x5000]

# Works with both bus and peek access modes
bbc.memory.address.bus.with_pc(0xD000)[0x5000]   # Side-effecting
bbc.memory.address.peek.with_pc(0xD000)[0x5000]  # Side-effect-free

# Typed access is supported
word = bbc.memory.address.peek.with_pc(0xD000).cast("<H")[0x5000]
```

On Model B (no shadow RAM), `with_pc()` has no effect.

#### Region-Based Access (named memory regions)

Access memory regions directly, bypassing bank switching:

```python
# Access main RAM
main = bbc.memory.region("main_ram")
value = main.bus[0x1234]

# Access sideways banks (uses absolute addresses at 0x8000)
bank4 = bbc.memory.region("bank_4")
data = bank4.peek[0x8000:0x8100]

# Access Model B+ shadow RAM
shadow = bbc.memory.region("shadow_ram")
shadow.bus[0x3000:0x3100] = bytes(256)

# Region discovery
for region in bbc.memory.regions:
    print(f"{region.name}: base=0x{region.base_address:04X}, size={region.size}")

print(f"Machine: {bbc.memory.machine_type}")
```


### Keyboard Input

```python
# Type text (handles shift automatically)
bbc.keyboard.type("PRINT 42")
bbc.keyboard.press_return()

# Individual key control
bbc.keyboard.key_down('A')
bbc.keyboard.key_up('A')

# Special keys
bbc.keyboard.press_escape()
bbc.keyboard.press_delete()

# Matrix-level access
bbc.keyboard.matrix_down(row=4, column=1)  # 'A' key
bbc.keyboard.matrix_up(row=4, column=1)
```

### Video Capture

```python
# Get video config
config = bbc.video.config
print(f"Resolution: {config.width}x{config.height} @ {config.framerate_hz}Hz")

# Capture a single frame
frame = bbc.video.capture_frame()
frame.save_png("screenshot.png")  # Requires Pillow

# Stream frames
for frame in bbc.video.stream_frames(max_frames=100):
    process(frame.pixels)  # BGRA32 format
```

### BBC BASIC Helpers

```python
# Wait for BASIC prompt
bbc.basic.wait_for_prompt()

# Wait for specific text
bbc.basic.wait_for_text("BBC Computer 32K")

# Read screen text (Mode 7)
text = bbc.basic.read_screen_text(0, 5)

# Run a BASIC program
bbc.basic.run_program("""
10 PRINT "HELLO"
20 END
""")

# Get BASIC memory layout
print(f"PAGE={bbc.basic.get_page():04X}")
print(f"TOP={bbc.basic.get_top():04X}")
print(f"HIMEM={bbc.basic.get_himem():04X}")
```

### Reading the MODE 7 Screen

`beebium.screen` reads what is actually displayed in MODE 7 (teletext),
correcting for hardware scrolling: the BBC scrolls by moving the CRTC display
start, so a fixed read of `0x7C00` returns a rotated grid once the screen has
scrolled. These helpers anchor the read at the CRTC screen-start, so they take
the client (not just `bbc.memory`).

```python
from beebium.client.screen import (
    read_mode7_screen, screen_contains, find, dump_screen,
)

# 25 rows of 40 characters, in display order
rows = read_mode7_screen(bbc)

# Is some text on screen? (tolerant of words wrapped across lines)
if screen_contains(bbc, "BBC Computer 32K"):
    ...

# Where is it? -> (row, column), or None
location = find(bbc, ">")

# Formatted dump (with the screen-start address) for diagnostics
print(dump_screen(bbc))
```

Lower-level building blocks compose for custom needs: `read_mode7_cells(bbc)`
returns the raw 25x40 byte grid, `cells_to_text(cells)` renders it as text, and
`linearise(rows, separator)` flattens it for searching (with separator
strategies `spaced`, `lined`, `no_separator`, and `dewrapped`). Read the screen
while the machine is paused or settled, so the screen-start and memory describe
one frame.

## pytest Integration

The beebium pytest fixtures are auto-registered. After installing beebium, they're available in your tests:

```python
def test_basic_print(bbc):
    """Test runs with a fresh BBC Micro instance."""
    bbc.basic.wait_for_prompt()
    bbc.keyboard.type("PRINT 42")
    bbc.keyboard.press_return()
    bbc.basic.wait_for_text("42")

def test_memory_access(stopped_bbc):
    """Test starts with emulator stopped."""
    stopped_bbc.memory.address.bus[0x1000] = 0x42
    assert stopped_bbc.memory.address.bus[0x1000] == 0x42
```

### Available Fixtures

| Fixture | Scope | Description |
|---------|-------|-------------|
| `bbc` | function | Fresh BBC Micro for each test |
| `bbc_shared` | module | Shared instance across tests in a module |
| `stopped_bbc` | function | BBC Micro starting in stopped state |
| `mos_filepath` | session | Path to MOS ROM |
| `basic_filepath` | session | Path to BASIC ROM (or None) |

### Configuration

Set ROM paths via environment variables or command line:

```bash
# Environment variables
export BEEBIUM_ROM_DIR=/path/to/roms
export BEEBIUM_SERVER=/path/to/beebium-server

# Command line options
pytest --beebium-rom-dir=/path/to/roms --beebium-server=/path/to/beebium-server
```

## Requirements

- Python 3.12+
- A built beebium-server executable
- ROM files (MOS, optionally BASIC)

## License

GPL-3.0-or-later
