# Paging-qualified breakpoints and watchpoints (#186)

Status: design, 2026-10-18. Raised by a downstream game project that keeps
code and data in sideways RAM and switches shadow and private RAM.

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
| B+, B+ 128K | `acccon` (bit 7 = shadow), `andy` (ROMSEL bit 7, 0 or 1), `shadow` (ACCCON bit 7, 0 or 1) |
| Integra-B | `ramsel` (the RAMSEL latch), `shen`, `prven`, `memsel`, `prvs1`, `prvs4`, `prvs8` (each 0 or 1) |
| ATPL Sidewise, Watford, ROM/RAM board | none beyond the core pair |

The derived booleans exist because `acccon & 0x80` in every condition is the
kind of thing users get wrong. They read the same latch as the raw name.

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

These are undefined in a breakpoint condition and rejected at compile time
with the same `INVALID_ARGUMENT` shape. The watchpoint hit callback already
receives address, value and direction; they go into the evaluation context
alongside the CPU registers.

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

# Catch a write to the screen that lands in main RAM while shadow is on (B+)
add_watchpoint(0x3000, 0x7FFF, WRITE, condition="shadow && !(PC >= 0xC000 && PC < 0xE000)")

# Catch any write of zero to the Integra-B private RAM while PRVS8 is in
add_watchpoint(0x9000, 0xAFFF, WRITE, condition="prvs8 && value == 0")
```

The third example also shows the limit of what the variables express: the
B+ shadow decision is made at the opcode fetch (#185), so a condition on
`PC` approximates it. A future `shadow_access` variable (1 when the access
was routed to shadow RAM) would be exact and comes for free once #185 makes
the hardware policy latch VDU status per instruction.

### Out of scope

- A `bank=` qualifier or any new field on AddBreakpointRequest.
- Making write-only latches readable through `mem[]`; the hardware is
  write-only and `mem[]` stays faithful.
- Coprocessor debuggers: the Tube coprocessors have no paging latches today;
  their `debug_variables()` is empty and the mechanism applies unchanged.

## Testing

Per variant: a breakpoint with `bank == N` fires only after a write to &FE30
selects N (not after writing &F4 alone), which also pins the "latch, not MOS
copy" promise. On the B+: `shadow` and `andy` follow &FE34 bit 7 and &FE30
bit 7. On the Integra-B: each boolean follows its bit. A watchpoint with
`value == 0x42` fires on the matching write only; `write` distinguishes a
read from a write to the same address; an unknown name and `value` in a
breakpoint condition both return `INVALID_ARGUMENT` naming the variable.
GetCpuDescriptor lists exactly the variables the condition compiler accepts
(a test compiles each listed name and rejects a made-up one).
