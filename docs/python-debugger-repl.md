# Debugging a running machine from a Python REPL

The Python client is a complete debugger for a Beebium server, and a Python
REPL is a usable command line for it: connect to a machine that is already
running, stop and start it, step, set conditional breakpoints and watchpoints,
read and poke memory and registers, and let it go again. A GUI client attached
to the same server keeps showing the screen throughout, and shows the machine
as stopped while you hold it.

Every transcript line below was taken from a session against a Master 128
server running the `master-128` preset. The API reference is the docstrings in
`clients/beebium-python-client/src/beebium/client/` (`debugger.py`, `memory.py`,
`cpu.py`), and the design discussion is `docs/discussion/memory-interface-design.md`.

## Setting up

Start a server on a known port, or use one a GUI client already started:

```bash
beebium-master-128 start --preset master-128 --port 48899
```

Then open a REPL where the `beebium` package is installed. In a checkout:

```bash
cd clients/beebium-python-client
uv run python
```

A server started by the macOS or Linux client listens on a port of its own and
advertises itself over mDNS. `beebium.client.discovery.browse()` lists what it
finds, with each machine's host and port. Discovery needs the `discovery`
extra, which brings in `zeroconf`; quote the extra, since the brackets are a
glob to zsh:

```bash
pip install 'beebium[discovery]'
```

```python
>>> from beebium.client.discovery import browse
>>> for m in browse(timeout=3.0): print(m.name, m.model, m.host, m.port)
```

## Connecting and looking around

```python
>>> from beebium.client import Beebium
>>> bbc = Beebium.connect("localhost:48899")       # the default target is localhost:48875
>>> bbc.debugger.get_state()
ExecutionState(is_running=True, cycle_count=51021725, halt_reason='', sequence=51021727, at_instruction_boundary=True)
>>> bbc.memory.machine_type
'master-128'
```

`connect()` leaves the machine as it found it. Nothing below changes anything
until you ask.

## Stopping, inspecting the CPU, disassembling

```python
>>> bbc.debugger.stop()
ExecutionState(is_running=False, cycle_count=55029218, halt_reason='stopped by debugger', sequence=55029221, at_instruction_boundary=True)
>>> bbc.cpu.registers
A=1B X=00 Y=0A SP=F1 PC=EAFD P=32 [nv-BdiZc]
>>> bbc.cpu.pc, bbc.cpu.a, bbc.cpu.registers.status.flag("Z")
(60157, 27, True)
```

The disassembler works on bytes you have read, so it reads whatever the CPU
would see at that address through the current paging:

```python
>>> from beebium.client.disassemble import disassemble
>>> pc = bbc.cpu.pc
>>> print("\n".join(disassemble(bbc.memory.address.peek[pc:pc + 16], start=pc)[:4]))
$EAFD: 2C 5F 02  BIT $025F
$EB00: 10 05     BPL $EB07
$EB02: A9 06     LDA #$06
$EB04: 6C 24 02  JMP ($0224)
```

Registers are writable properties:

```python
>>> bbc.cpu.a = 0
>>> bbc.cpu.update(x=1, y=2)
```

## Inspecting and poking memory

Two accessors cover the 16-bit address space. `peek` reads without side
effects and is safe on hardware registers; `bus` is a CPU-visible read or
write, which is what you want to poke RAM or kick a device.

```python
>>> bbc.memory.address.peek[0:16].hex(" ")
'02 0e 02 0e 00 7c 00 7c 00 00 00 00 07 41 52 57'
>>> bbc.memory.address.peek[0xFE4D]                        # System VIA IFR, no flags cleared
>>> hex(bbc.memory.address.peek.cast("<H")[0x0202])        # BRKV, 16-bit little-endian
'0xb278'
>>> bbc.memory.address.bus[0x70] = 0x99                    # poke one byte
>>> bbc.memory.address.bus[0x7C00:0x7C28] = b"Hello" + b" " * 35   # or a range
```

Named regions reach memory that the paging may be hiding, such as shadow RAM
or a sideways bank that is not currently selected:

```python
>>> [r.name for r in bbc.memory.regions]
['main_ram', 'lynne', 'hazel', 'andy', 'mos_rom', 'bank_0', 'bank_1', ..., 'bank_15']
>>> bbc.memory.region("hazel").peek[0xC000:0xC008]         # HAZEL whatever ACCCON says
>>> bbc.memory.region("bank_12").peek[0x8000:0x8010]       # BASIC's header, unpaged
```

Accessors also load and save files and fill ranges:

```python
>>> bbc.memory.address.bus.load(0x1900, "program.bin")
>>> bbc.memory.address.peek.save(0x3000, 0x5000, "screen.bin")   # 0x5000 bytes from bbc.memory.address.peek.save(0x3000, 0x5000, "screen.bin")3000
>>> bbc.memory.address.bus.fill(0x0E00, 0x1900, 0)
```

## Stepping

```python
>>> r = bbc.debugger.step(3)
>>> r.instructions_executed, r.cycles_executed, hex(bbc.cpu.pc)
(3, 11, '0xeb0a')
>>> bbc.debugger.step_cycles(1)                            # one bus cycle; may stop mid-instruction
```

## Conditional breakpoints

A breakpoint is evaluated when the program counter reaches an address in its
range at an instruction boundary, and stops the machine when its condition is
true. Conditions are C-like expressions over the registers `A X Y SP PC P`,
the flags `C Z I D V N`, `cycles`, `hits`, `mem[addr]`, and the names this
machine's hardware adds. Ask the machine what it accepts:

```python
>>> [v.name for v in bbc.debugger.condition_variables()]
['A', 'X', 'Y', 'SP', 'PC', 'P', 'C', 'Z', 'I', 'D', 'V', 'N', 'cycles', 'hits',
 'bank', 'romsel', 'acccon', 'andy', 'accd', 'acce', 'accx', 'accy', 'accitu',
 'accifj', 'acctst', 'accirr', 'shadow_access', 'crtc_row', 'crtc_column',
 'raster', 'cycles_since_vsync', 'display_enabled', 'in_vsync', 'odd_field',
 'field', 'addr', 'value', 'write']
```

The same list comes from the server with `beebium-master-128 describe-conditions`.

Stop in OSWRCH only when it is about to print an `H`:

```python
>>> bp = bbc.debugger.add_breakpoint(0xFFEE, condition="A == 0x48")
>>> bbc.debugger.run()
>>> bbc.basic.type_and_enter('PRINT "Hello"')
>>> ev = bbc.debugger.wait_for_stop(timeout=10)
>>> ev.message, hex(bbc.cpu.pc), chr(bbc.cpu.a)
('breakpoint at $FFEE', '0xffee', 'H')
>>> bbc.debugger.list_breakpoints()
[Breakpoint(id=1, address=65518, end_address=65519, condition='A == 0x48', stop_counterpart=False, hit_count=8, enabled=True)]
>>> bp.disable(); bp.enable(); bp.remove()
```

`hit_count` counts every time the condition was evaluated, not only the stops.
Other useful conditions:

```python
"hits == 5"                        # the fifth time through
"X > 0 && mem[0x70] == 0xFF"       # a register and a memory byte together
"acce && !accx"                    # Master: ACCCON's E set and X clear
"romsel == 12"                     # with BASIC paged in
"raster == 100"                    # on a particular scanline
```

`run()` behaves like a debugger's continue: a breakpoint on the instruction you
are stopped at is stepped over, not reported again.

## Watchpoints

A watchpoint fires on a read, a write or either, of an address in its range.
Its condition can also name the access: `addr`, `value`, `write` (1 for a
write) and, on a machine with shadow RAM, `shadow_access`.

```python
>>> wp = bbc.debugger.add_watchpoint(0x0070, 0x0071, "write", condition="value == 42")
>>> bbc.debugger.run()
>>> bbc.basic.type_and_enter("?&70=42")
>>> ev = bbc.debugger.wait_for_stop(timeout=15)
>>> ev.message, hex(bbc.cpu.pc)
('write watchpoint at $0070 = $2A', '0xb34b')
>>> wp.remove()
```

A condition of `"false"` makes a recording-only watchpoint that counts hits but
never stops.

## Running for a while, or to an address

"Run for N cycles" is a whole-address-space breakpoint with a `cycles`
condition. The `breakpoint()` and `watchpoint()` context managers remove
theirs on exit, and `run_and_wait_for_stop()` resumes and waits in one call:

```python
>>> target = bbc.debugger.cycle_count + 2_000_000
>>> with bbc.debugger.breakpoint(0, end_address=0x10000, condition=f"cycles >= {target}"):
...     ev = bbc.debugger.run_and_wait_for_stop(timeout=10)
...
>>> ev.message, bbc.debugger.cycle_count
('breakpoint at $E7E4', 153773691)
```

Shorthands built on the same pieces:

```python
>>> bbc.debugger.run_to(0xFFF4)                            # run until PC reaches OSBYTE
>>> bbc.debugger.measure(0xFFF4, 0xFFF7)                   # cycles between two PCs, with samples
>>> bbc.run_for_emulated_seconds(0.5)
>>> bbc.run_fields(2)                                      # two video fields
```

And when you are done:

```python
>>> bbc.debugger.run()
>>> bbc.close()
```

## Things to know

- `is_running`, `is_stopped` and `cycle_count` on the debugger are properties,
  not methods.
- Pairing `run()` with a later `wait_for_stop()` races a breakpoint that fires
  before the wait subscribes. Prefer `run_and_wait_for_stop()` when nothing has
  to happen in between; the split form above works because `wait_for_stop`
  compares sequence numbers against its first snapshot.
- Stopping, stepping and poking are visible to every client on the server. A
  GUI client shows the machine as stopped and its screen frozen until you run
  it again.
- `bbc.debugger.reset()` is a cold power-on reset that leaves the CPU stopped.
  For a Break, hold the key: `bbc.basic.send_break()`.
- With a Tube coprocessor fitted, `bbc.coprocessor()` yields a second client
  whose `debugger` and `memory` are the coprocessor's, and
  `stop_counterpart=True` on a breakpoint stops both sides together.
- For scripted, repeatable debugging the same calls work under pytest. The
  Python client's README and `readme/snippets/` show the launch-and-test form.
