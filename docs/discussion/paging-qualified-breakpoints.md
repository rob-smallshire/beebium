# Paging-qualified breakpoints and watchpoints (#186)

Status: implemented: machine and access variables (#186), beam variables
(#196). Raised by a downstream game project that keeps code and data in
sideways RAM and switches shadow and private RAM.

## Problem

A breakpoint or watchpoint is keyed on a CPU address alone. With code in
sideways RAM, a breakpoint at &A123 fires in whichever bank is paged in; a
watchpoint on &3000 fires whether the access reached main RAM or shadow RAM.
The condition language cannot help today, because the paging state is not
visible to it:

- ROMSEL (&FE30) is write-only on every variant, so `mem[0xFE30]` reads &FF.
  Users fall back on the MOS copy at `mem[0xF4]`, which lags the latch and is
  wrong whenever code writes &FE30 directly.
- On the B+ `mem[0xFE34]` does read ACCCON, because the hardware makes it
  readable. On the Integra-B the ROMSEL extension bits (PRVEN, MEMSEL) and
  RAMSEL (SHEN, PRVS1/4/8) are write-only and cannot be tested at all.
- A watchpoint condition cannot see the address or the value of the access
  that triggered it.

## Ruling

Do not add a first-class `bank=` qualifier yet. Extend the condition language
with variables that read the actual latches, supplied by the hardware policy,
and give watchpoint conditions the access itself. A qualifier can be sugar
over these later if the long form proves clumsy.

Three reasons. The condition language already exists and is compiled and
evaluated server-side, so the cost is a table of names, not a new mechanism.
The debugger stays variant-agnostic: it asks the hardware policy for its
variables and never learns what ACCCON is. And the same vocabulary serves
breakpoints, watchpoints and future tracepoints alike.

## Design

### Machine variables

Each hardware policy supplies a table of named, read-only, side-effect-free
integer variables:

```cpp
struct DebugVariable {
    std::string_view name;         // identifier in the condition language
    std::string_view description;  // one line, for DescribeConditionVariables
    std::function<uint64_t()> read;
};
std::vector<DebugVariable> debug_variables() const;
```

The core names, present on every variant:

| Name | Meaning |
|------|---------|
| `bank` | The sideways bank currently selected: ROMSEL bits 0-3 as last written. |
| `romsel` | The full ROMSEL latch as last written (all bits the hardware keeps). |

Variant-specific names, present only where the hardware has the latch:

| Variant | Names |
|---------|-------|
| B+, B+ 128K | `acccon` (the latch; bit 7 = VDUSEL), `andy` (ROMSEL bit 7, 0 or 1), `vdusel` (ACCCON bit 7, 0 or 1: the display is in shadow RAM and VDU code sees it), `vdu` (the PAL's VDU-driver flag latched at the last opcode fetch, 0 or 1; see #185) |
| Integra-B | `ramsel` (the RAMSEL latch), `shen`, `prven`, `memsel`, `prvs1`, `prvs4`, `prvs8` (each 0 or 1) |
| ATPL Sidewise, Watford, ROM/RAM board | none beyond the core pair |

The derived booleans exist because `acccon & 0x80` in every condition is the
kind of thing users get wrong. They read the same latch as the raw name, and
they take the hardware's own names: the B+ bit is called VDUSEL in the
service manual, and it is not called `shadow` here because "shadow" means
different things on the two shadow machines (on the B+ it is a property of
the display and of VDU code; on the Integra-B SHEN with MEMSEL clear is a
property of every CPU access). The variant-neutral question, "did this access
go to shadow RAM", is answered exactly by `shadow_access` below, not by a
latch bit.

`vdu` is the latch from #185: in a breakpoint condition it is the flag the
instruction at the breakpoint was fetched under, which is exactly what one
wants when debugging shadow access near the &A000, &B000, &C000 and &E000
boundaries.

Names are lower case and never collide with the upper-case registers; the
parser resolves an identifier against the register set first, then the
machine table. An unknown name is a compile error at AddBreakpoint time,
returned as `INVALID_ARGUMENT` with the message naming the variable and the
machine ("no variable 'acccon' on model-b-integra-b; available: bank, romsel,
ramsel, ..."), so a client cannot silently test a latch the machine lacks.

### Access variables (watchpoints only)

A watchpoint condition may also use:

| Name | Meaning |
|------|---------|
| `addr` | The CPU address of the access that triggered the watchpoint. |
| `value` | The byte read or written. |
| `write` | 1 for a write, 0 for a read. |
| `shadow_access` | 1 if the access was routed to the `shadow_ram` region, 0 if to `main_ram`. Only on machines that have a `shadow_ram` region (B+, B+ 128K, Integra-B); unknown elsewhere. |

`shadow_access` is the same question on every shadow machine, answered by
the hardware policy that did the routing (on the B+ from VDUSEL and the VDU
latch, on the Integra-B from SHEN and MEMSEL), so one condition such as
`shadow_access && value == 0` works unchanged across variants.

These are undefined in a breakpoint condition and rejected at compile time
with the same `INVALID_ARGUMENT` shape. The watchpoint hit callback already
receives address, value and direction; they go into the evaluation context
alongside the CPU registers.

### Beam variables (#196)

The CRTC is common to every variant, so the machine, not the hardware
policy, supplies a second table, usable in breakpoint and watchpoint
conditions alike. The names follow the fields of `CrtcState`
(GetCrtcState), which clients already know:

| Name | Meaning |
|------|---------|
| `crtc_row` | Character row being scanned (0 to R4). |
| `crtc_column` | Character column being scanned (0 to R0). |
| `raster` | Scanline within the character row (0 to R9). |
| `cycles_since_vsync` | CPU cycles since the field started. |
| `display_enabled` | DISPEN, 0 or 1. |
| `in_vsync` | 0 or 1. |
| `odd_field` | 1 during the odd (first) field of an interlaced frame. |
| `field` | Machine-wide count of vsyncs since power-on. Not reset by Break. |

The values are those in effect for the cycle of the access being judged,
not those of the last CRTC tick before some later point; the implementation
must establish the order of CRTC tick and CPU access in CpuBinding and test
it against a known mode's timing. `field` is the counter #190 will use to
run for N fields and that a captured frame should carry, so the three agree.

A `row_of(addr)` function is out of scope: the address-to-row mapping
depends on R12/R13, R1, the mode and wraparound, and #195 will report the
screen's region and range so a test can compute its own bounds.

Example, Stratagem's beam-race assertion for a game that draws newly exposed
columns just after vsync, as a watchpoint over its column buffer:

```
write && shadow_access && addr >= 0x5800 && addr < 0x5A80 && crtc_row > 4
```

### Names reserved for the Master 128 (#189)

So that conditions stay portable when the Master lands, its ACCCON bits take
the register's own bit names with the `acc` prefix, 0 or 1 each: `accd`,
`acce`, `accx`, `accy`, `accitu`, `accifj`, `acctst`, `accirr`. The raw
`acccon` latch is present as on the B+. `shadow_access` covers the Master
with its own rule (LYNNE via E and Y, the &C000-&DFFF code window). These
are not implemented until #189; a condition using them on another machine
is rejected like any unknown name.

### ROMSEL bit 7

`andy` on the B+ and `memsel` on the Integra-B are both "ROMSEL bit 7 is
set", with different meanings. There is no neutral alias: `romsel & 0x80`
reads the real latch on every variant, and the documentation says so. The
meaning of the bit belongs to the machine, so the name does too.

### Discoverability

The server is the single source of truth for the vocabulary. `CpuDescriptor`
(GetCpuDescriptor) gains the list of condition variables the machine
supports, each with its one-line description, split into the always-present
set, the machine set and the watchpoint-only set. The Python and TypeScript
clients expose it (`debugger.condition_variables()`) and do not hard-code the
names; the CLI's `describe-machine` or a `describe-conditions` subcommand
prints the same table so documentation can be generated rather than written.

### Examples

```
# Stop at &A123 only when sideways bank 12 is paged in
add_breakpoint(0xA123, condition="bank == 12")

# Stop at &A123 only when ANDY, not a sideways bank, is at &A000
add_breakpoint(0xA123, condition="andy")

# Catch a write to the screen range that lands in main RAM while the display
# is in shadow RAM (B+): a program writing where nothing will be seen
add_watchpoint(0x3000, 0x7FFF, WRITE, condition="vdusel && !shadow_access")

# Stop in a boundary-straddling routine only when it was fetched as VDU code
add_breakpoint(0xDFFD, condition="vdu")

# Catch any write of zero to the Integra-B private RAM while PRVS8 is in
add_watchpoint(0x9000, 0xAFFF, WRITE, condition="prvs8 && value == 0")
```

`vdu` and `shadow_access` depend on #185 modelling the PAL's latch per
instruction, which is why #185 lands first.

### Out of scope

- A `bank=` qualifier or any new field on AddBreakpointRequest.
- Making write-only latches readable through `mem[]`; the hardware is
  write-only and `mem[]` stays faithful.
- Coprocessor debuggers: the Tube coprocessors have no paging latches today;
  their `debug_variables()` is empty and the mechanism applies unchanged.
  The beam table is a host-machine table and does not apply to them.
- `row_of(addr)` and any other function over the beam variables.

## Testing

Per variant: a breakpoint with `bank == N` fires only after a write to &FE30
selects N (not after writing &F4 alone), which also pins the "latch, not MOS
copy" promise. On the B+: `vdusel` and `andy` follow &FE34 bit 7 and &FE30
bit 7, and `vdu` is 1 at a breakpoint in &C000-&DFFF and 0 at one in
&E000-&FFFF while VDUSEL is set. `shadow_access` is 1 for a VDU-code write
to &3000-&7FFF on the B+ with VDUSEL set and for a CPU write there on the
Integra-B with SHEN set and MEMSEL clear, 0 otherwise, and unknown on the
Model B. On the Integra-B: each boolean follows its bit. A watchpoint with
`value == 0x42` fires on the matching write only; `write` distinguishes a
read from a write to the same address; an unknown name and `value` in a
breakpoint condition both return `INVALID_ARGUMENT` naming the variable.
GetCpuDescriptor lists exactly the variables the condition compiler accepts
(a test compiles each listed name and rejects a made-up one).

Beam variables: a whole-address-space breakpoint with `in_vsync` stops at
the first instruction boundary in the next vsync; `field` advances by one
per vsync and by about 50 per emulated second in a 50 Hz mode; a watchpoint
on a screen address with `crtc_row == N` fires for a write made while the
CRTC is on that row, checked against the known timing of MODE 7 (a write
timed by cycles_since_vsync to land on a chosen row); `display_enabled` is 0
during the write made in the vertical blank. Each value is checked against
GetCrtcState read at the same stop.
