# The Voltmace Delta 14B/1 adapter: round 5 specification (#98)

Round 4 built the controller seam, `ControllerService` and the plain joystick. This round adds the
first controller that occupies two ports: the Voltmace Delta 14B/1 adapter box with two Delta 14B
handsets, in the analogue port and the user port together. It is written from
`voltmace-delta-14b-findings.md`; every `Decision` below that the findings leave open is marked
`Decision (unverified)` and listed at the end for checking on a real box. Fidelity on those points is
not to hold up shipping: Voltmace's own drivers depend on none of them.

## Principles

Those of rounds 2 to 4 apply. In addition:

- **The device is the box plus two handsets.** The sticks, the keypads and the red buttons are all
  controls of one controller. A handset on its own in the analogue port is the round 4 joystick and
  is not this device.
- **The matrix is computed, never stored.** The row lines are a function of the keys held and the
  column and select lines the user VIA is driving at that instant, computed on every
  `update_port_b` call from atomics, so a strobe written to the VIA is visible on the very next read.
- **No ghosting.** The real matrix has no diodes and can phantom a fourth key from three in an L. The
  model does not reproduce that (`Decision`), and nothing in the design prevents adding it.

## The hardware as modelled

Sockets: the **rear** socket is handset 1 and the **side** socket is handset 2. Single-handset use is
on the rear socket, and the rear socket is on channels 0 and 1.

| Line | Role |
|---|---|
| Analogue CH0, CH1 | Handset 1 X and Y |
| Analogue CH2, CH3 | Handset 2 X and Y |
| User port PB4, PB5, PB6 | Column 0, 1, 2 strobes, common to both handsets, active low |
| User port PB0, PB1, PB2, PB3 | Row 3, 2, 1, 0 of the selected handset, active low, 1 at rest |
| User port PB7 | 74LS157 select: 0 = rear (handset 1), 1 = side (handset 2) |
| Analogue connector PB0 (System VIA PB4) | Fire 0: handset 1 row 3 keys, gated by the columns (see below) |
| Analogue connector PB1 (System VIA PB5) | Fire 1: handset 2 row 3 keys, gated the same way |
| Analogue LPSTB, user port CB1, CB2 | Untouched by the device |

Keys: 0 to 11 on a 3 × 4 pad numbered from the bottom row, key = 3 × row + column. Row 0 (PB3) is
keys 0, 1, 2; row 3 (PB0) is keys 9, 10, 11. Column 0 (PB4) is keys 0, 3, 6, 9. The two red buttons
are key 10 and are not separate controls.

Stick direction is Acorn's: X high at hard left, low at hard right; Y high at the top.

### Reading a row

A column is **driven low** when its user VIA DDRB bit is an output and its ORB bit is 0. Any other
state of a column (output and 1, or input and therefore floating high into the box's TTL inputs) is
not driven. Row r of the selected handset reads 0 when any key at (r, c) is held with column c driven
low; otherwise 1. The selected handset is 2 when PB7 is an output and 1, or an input
(`Decision (unverified)`: a floating LS157 select input reads high); handset 1 when PB7 is an output
and 0.

### The fire lines

`Decision (unverified)`: with the box fitted, a row 3 key (9, 10, 11 or a red button) on handset 1
grounds analogue connector PB0, and a row 3 key on handset 2 grounds connector PB1, each only while
that key's column is driven low from the user port. No other row reaches a fire line. This fits M p.1
(fire does not work until the columns are grounded with `DDRB = &70`, `ORB = 0`) and M p.2 section 1c
(a handset in each socket serves a two-joystick game, each with its fire). Driver A reads these
through `ADVAL(0)` and never writes the user port, so it works only after the user's set-up commands,
on the emulator as on the hardware.

## The controller

Controls, in this order, group "Handset 1 (rear)" then "Handset 2 (side)":

| Id | Display name | Kind |
|---|---|---|
| `handset1.x`, `handset1.y` | "X", "Y" | axis |
| `handset1.key0` to `handset1.key11` | "Key 0" to "Key 11" | button |
| `handset2.x`, `handset2.y`, `handset2.key0` to `handset2.key11` | the same | |

Twenty-eight controls. Axes start at 32768, buttons released, nothing resets. The two red buttons
are `key10`.

## The extension

Built-in, as the joystick is, static library under `src/extensions/voltmace-delta-14b/`:

- Manifest: `name = "voltmace-delta-14b"`, `display_name = "Voltmace Delta 14B/1 adapter"`,
  `cli_name = "voltmace-delta-14b"`, `description = "Voltmace's Delta 14B/1 adapter box with two
  Delta 14B handsets: a joystick and a 12-key keypad each, in the analogue port and the user port
  together."`, `attaches_to = {"analogue-port", "user-port"}`, no parameters.
- Implements `AnaloguePortDevice` (`channel_level` from the four axes; `button_lines` from the fire
  rule above, which needs the last column and DDR state the user VIA gave it; `light_pen_strobe` 0),
  `UserPortDevice` (`update_port_b(output, ddr)` records the column, DDR and select state and returns
  the four row bits in PB0 to PB3 with PB4 to PB7 as 1; `update_control_lines` leaves cb1 and cb2
  alone) and `Controller`.
- Storage: four `std::atomic<uint16_t>` axes, two `std::atomic<uint16_t>` key masks (bit k = key k
  held), and the user VIA's last `output` and `ddr` bytes in atomics written by `update_port_b` on the
  emulation thread and read by `button_lines` on the same thread; no lock anywhere.
- `attaches_to` has two points, so occupancy of both `analogue-port` and `user-port` becomes 1; the
  attachment plan already supports this, and a second device on either port is refused by the plan.
- Not attached by any stock preset.

## Documentation

- `docs/analogue-port.md`: a "Voltmace Delta 14B/1 adapter" section (the box, the matrix, the select,
  the fire rule, the three unverified decisions), and in the joystick section a sentence that a Delta
  14B handset straight into the port is the joystick with keys 9 to 11 and the red buttons as fire 0
  and keys 6 to 8 as fire 1.
- `docs/peripheral-extension-framework.md`: the built-in list.
- `docs/client-specification.md` chapter 14: no change in rules; the scenario list gains "attach the
  Delta 14B/1 adapter: one controller with two groups of fourteen controls".

## Tests

### Catch2, `tests/test_voltmace_delta_14b.cpp`, tag `[voltmace_delta_14b]`, 7-bit ASCII names

1. The manifest is in the built-in entries with both attachment points; `--voltmace-delta-14b`
   attaches it to a Model B and both `analogue-port` and `user-port` report occupied; attaching the
   joystick or a user port device as well is refused with a sentence.
2. The controls list is the twenty-eight above in that order, kinds and groups.
3. `handset1.x` reaches channel 0, `handset1.y` channel 1, `handset2.x` channel 2, `handset2.y`
   channel 3 (a 12-bit conversion of each).
4. Matrix: with `DDRB = &F0`, for each handset h and each key k: the strobe byte for k's column with
   PB7 for h gives a read of &FE60 with k's row bit clear and the other three set, plus PB4 to PB7 as
   written; the other two columns' strobes give %1111; the other handset's strobe gives %1111.
5. Immediate visibility: a 6502 program loaded from a disc does `STA &FE60` then `LDA &FE60` for
   each of KEYPAD's six strobes and stores the six reads; with `handset2.key7` held the stored byte
   for strobe &D0 is &D0 | %1101 and the other five are strobe | %1111.
6. KEYPAD's pre-probe: `ORB = &00` with any handset 1 key held reads a low nibble other than &F;
   `ORB = &80` with handset 1 keys held and handset 2 idle reads &8F.
7. Select as input: `DDRB = &70` with a handset 2 key held and its column driven low reads that
   row clear; the same key on handset 1 reads %1111 (the unverified decision, so one test, named as
   such).
8. Columns not driven: `DDRB = &00` with any key held reads %1111 on the rows, and the System VIA fire
   bits read 1.
9. Fire through the box: `DDRB = &70`, `ORB = 0`; `handset1.key9`, `key10`, `key11` each make System
   VIA port B bit 4 read 0 and bit 5 read 1; `handset2.key10` makes bit 5 read 0 and bit 4 read 1;
   `handset1.key6` changes neither. With `DDRB = &F0` and `ORB = &50` (column 1 only),
   `handset1.key10` fires and `handset1.key9` does not.
10. CB1 and CB2 of the user VIA, and IFR bits 3 and 4, never change over a second of emulated time
    with keys held and strobes written.
11. Values survive `soft_reset()` and `Hardware::reset()`.
12. The joystick's `[joystick]` tests and the `[analogue_port]` tests still pass.

### Catch2, Model B with MOS 1.20 as the test vector

13. A disc-loaded BASIC program in the shape of the manual's PROCPB (`*FX151,98,240`; strobes &60,
    &50, &30 through OSBYTE &97 and reads through OSBYTE &96) with `handset1.key7` held prints 7, and
    with no key prints 0; the manual's six-strobe scan with `handset2.key4` held prints the side-socket
    key. Read through the screen text library.
14. `ADVAL(1)` to `ADVAL(4)` report the four axes, and after `*FX151,98,112` and `*FX151,96,0`,
    `ADVAL(0) AND 3` is 1 with `handset1.key10` held and 2 with `handset2.key10` held.

### gRPC and Python

15. `ListControllers` on a machine with the adapter returns one controller, display name "Voltmace
    Delta 14B/1 adapter", twenty-eight controls in two groups (in `tests/test_grpc_controller.cpp`).
16. Python: `machine.controllers.set("voltmace-delta-14b", {"handset2.key4": True})` then the
    six-strobe BASIC scan from a disc prints the key; `handset1.x` set to `&F000` makes `ADVAL(1)`
    print a value in `&F000..&F0FF`.

## Delivery

1. The extension with `AnaloguePortDevice`, `UserPortDevice` and `Controller`, tests 1 to 12.
2. The MOS test vectors 13 and 14, the service test 15, the Python test 16, documentation.

Full gate from a separate worktree Release build before reporting the round done. Same rules as the
earlier rounds: `git commit --only`, never amend, never push, no people or host names, 7-bit ASCII
in test names and tags, stop and ask if a `Decision` proves wrong in the code.

## To verify on a real Delta 14B/1 when one is to hand

1. Fire routing: which rows of which socket reach analogue connector PB0 and PB1, and whether they
   are gated by the column lines as modelled.
2. The select with PB7 as an input: does the box read the side socket (as modelled) or the rear?
3. Whether CB2 on the user port cable does anything inside the box.

Each is one `Decision (unverified)` above and one test (7, 9, 10); correcting the model is a change
to one function and one test each.
