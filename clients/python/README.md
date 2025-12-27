# Beebium Python Client

Python client for the Beebium BBC Micro emulator. Designed for pytest-based testing of BBC Micro software.

## Installation

```bash
pip install beebium
```

For development (includes grpcio-tools for proto generation):
```bash
pip install beebium[dev]
```

For image capture support (requires Pillow):
```bash
pip install beebium[imaging]
```

## Quick Start

```python
from beebium import Beebium

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

# Breakpoints
bp_id = bbc.debugger.add_breakpoint(0xC000)
bbc.debugger.run_until(0xC000)  # Run until address
bbc.debugger.remove_breakpoint(bp_id)
bbc.debugger.clear_breakpoints()

# State queries
state = bbc.debugger.get_state()
print(f"Running: {state.is_running}, Cycles: {state.cycle_count}")
```

### CPU Register Access

```python
# Read registers
regs = bbc.cpu.registers
print(f"A={regs.a:02X} X={regs.x:02X} Y={regs.y:02X}")
print(f"PC={regs.pc:04X} SP={regs.sp:02X} P={regs.p:02X}")

# Individual register access
if bbc.cpu.a == 0:
    print("Accumulator is zero")

# Write registers
bbc.cpu.pc = 0xC000
bbc.cpu.a = 0x42

# Flag access
if regs.zero:
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

# Typed access using struct format strings
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
