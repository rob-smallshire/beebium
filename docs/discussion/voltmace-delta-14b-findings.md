# The Voltmace Delta 14B and the Delta 14B/1 adapter: research findings (#98)

Answers to the nine questions in `voltmace-delta-14b-research-brief.md`. Each answer carries its
source. Where a statement is an inference from the sources rather than a reading of one, it is marked
`Inference` and the round 5 specification must treat it as a `Decision` to be confirmed on hardware.

## Sources

- **[M]** Voltmace, *Delta 14/B Handset System: Instructions for Use*, ten pages. Cited as M p.N.
- **[S]** The adapter circuit diagram on M p.10. A faint hand-drawn scan; only block-level connections
  are legible, not the net-by-net wiring.
- **[P]** The handset's DA-15 pinout as recorded by the SPItFIRE project, a joystick interface for
  the Master Compact that drives real Delta handsets, and **[H]** SPItFIRE's bench measurements of
  real Delta 14B and 3B units, which confirm [P].
- **[A]** Annotated disassemblies of Voltmace's own driver programs at
  `acornaeology.uk/voltmace-delta14b-driver/` (secondary; not yet checked against the box).
- **[E]** Mark Moxon's Delta 14B build of Elite-A, the matrix-scanning routine at
  `elite.bbcelite.com/elite-a/flight/subroutine/b_14.html` (secondary).

## 1. The keypad matrix through the adapter

Settled from [M p.4-5].

- `DDRB = &F0`: PB7 and PB4 to PB6 are outputs, PB0 to PB3 inputs. The manual's own idiom is
  `*FX151,98,240`.
- Three columns, strobed by driving one of PB4, PB5, PB6 low with the others high. Four rows read on
  PB0 to PB3, 1 at rest and 0 where a key in the strobed column is pressed. Active low.
- Key number = 3 × row + column, with column 0 on PB4 and row 0 the bottom row (keys 0, 1, 2).

| Strobe (ORB, PB7 = 1, side socket) | Strobe (PB7 = 0, rear socket) | Column | Keys |
|---|---|---|---|
| &E0 (PB4 low) | &60 | 0 | 0, 3, 6, 9 |
| &D0 (PB5 low) | &50 | 1 | 1, 4, 7, 10 |
| &B0 (PB6 low) | &30 | 2 | 2, 5, 8, 11 |

| Row bits PB3..PB0 | Row | Keys |
|---|---|---|
| %0111 (PB3 low) | 0 | 0, 1, 2 |
| %1011 (PB2 low) | 1 | 3, 4, 5 |
| %1101 (PB1 low) | 2 | 6, 7, 8 |
| %1110 (PB0 low) | 3 | 9, 10, 11 |
| %1111 | | none |

Several keys in one column are distinguishable in a single read; keys in different columns need a
scan of every column [M p.5]. Pull-ups: neither [M] nor [S] says where they are; the rows reach the
VIA through 74LS157 outputs, so the VIA sees TTL levels. `Inference`: the emulation returns 1 on an
unpressed row.

## 2. Twelve logical keys, fourteen buttons

Settled from [M p.10], confirmed by [H].

- Keys 0 to 11 on a 3 × 4 keypad, numbered bottom row upwards: 0 1 2 / 3 4 5 / 6 7 8 / 9 10 11.
- Two extra buttons, drawn above the keypad's top corners on M p.10, are wired in parallel with key
  10: "the buttons marked 10 are always common and cannot be separated". M p.8 calls the three
  commoned buttons "the three red buttons". [H] confirms both extras read as key 10 on two boards.
- The keys carry no printed legend: the handset ships with blank overlays for the user to write on.

`Decision` for the specification: control ids `handset1.key0` to `handset1.key11` and the same for
handset 2, display names "Key 0" to "Key 11", groups "Handset 1" and "Handset 2". The two extra
fire buttons are not separate controls, because the hardware cannot separate them. The sticks are
`handset1.x`, `handset1.y`.

## 3. The handset select

Settled for the normal case from [M p.4]: PB7 = 0 reads the **rear** socket, PB7 = 1 the **side**
socket. The specification should call the rear socket handset 1 and the side socket handset 2, since
single-joystick use is on the rear socket and the rear socket is on channels 0 and 1 (question 5).

Open: what the box presents when PB7 is an input. Port B of the 6522 has no pull-ups, so the line
floats; a floating LS157 select input normally reads high, which would pick the side socket
(`Inference`), but M p.1 tells single-joystick users to set `DDRB = &70` (PB7 an input) and still use
the rear socket, which argues the other way. M p.8 notes that programs using the user VIA's T1 in PB7
output mode toggle the select, and tells those users to use the side socket.

## 4. The fire lines through the adapter

Partly settled from [M p.1-2]; the routing is open.

- With the box fitted, the fire button does not work until the user port columns are driven low:
  `DDRB = &70`, `ORB = 0` (`*FX151,98,112` then `*FX151,96,0`). A single-joystick game then uses the
  rear socket. For a game written for two ordinary joysticks, a handset goes in each socket with the
  same commands [M p.2 section 1c].
- `Inference`: the box replaces the handset's grounded column lines with user port PB4 to PB6, while
  the row lines still reach the analogue port's fire inputs, so a row only reads as fire when its
  column is driven low from the user port. Which rows reach connector PB0 and PB1 for each socket
  cannot be traced from [S]. If it matches the direct connection (question 9), rear keys 9 to 11 give
  fire 0 and rear 6 to 8 fire 1; section 1c suggests each socket gets one fire line, but which is
  unconfirmed.

## 5. The pots

Settled from [M p.2-4], confirmed by [H].

- Through the box: rear socket X and Y on channels 0 and 1 (`ADVAL(1)`, `ADVAL(2)`), side socket on
  channels 2 and 3. Straight into the analogue port: channels 0 and 1.
- Direction is Acorn's: X reads high at hard left and low at hard right; Y reads high at the top.
  M p.3 and p.7 say a decreasing reading is needed for an increasing screen coordinate, so the
  manual's BASIC uses `1280 - X` and leaves Y alone. [H] measured X = 0 hard right and 1023 hard
  left on 10-bit hardware, Y high at the top.
- Handset DA-15: X on pin 15 (CH0), Y on pin 7 (CH1), VREF pin 14, analogue ground pin 8 [P, H].

## 6. CB1 and CB2

Open. [S] shows CB2 carried on the ribbon cable but not where it goes; [M] never mentions CB1 or CB2.
The box does route the analogue port's LPSTB to the side socket [M p.5]. `Decision` for the
specification unless hardware says otherwise: the device leaves the user port's CB1 and CB2 alone.

## 7. Software and a test vector

- Voltmace's own driver programs: KEYPAD (puts keys in the keyboard buffer every vsync; needs the box)
  and JOYSTICK (compiles an `INKEY(-n)` driver into &A00 to &AFF) [M p.7-9], disassembled at [A].
- Elite-A's Delta 14B build scans the matrix directly, one key per call [E].
- The cleanest test vector is the manual's own Bowling demo, PROCPB, rear socket [M p.6-7]: `DDRB =
  &F0`; for each column write ORB &60, &50, &30 (PB7 = 0) and read IRB; the low nibble is the rows;
  15 means no key; else key = (column − 1) + 3 × row with %1110 → row 3 (+8 after the column's +1),
  %1101 → row 2, %1011 → row 1, %0111 → row 0. Pressing key 7 gives %1101 on the &50 strobe. M p.4
  also has a six-strobe scan with `DATA &E0,&D0,&B0,&60,&50,&30`, side then rear.

The specification's MOS-independent test vector is a disc-loaded BASIC program in PROCPB's shape run
on both sockets, with the Elite-A routine as a second vector if its disassembly is confirmed.

## 8. Prior art in emulators

Settled: there is none to read. MAME's `src/devices/bus/bbc/userport/` holds no Voltmace or keypad
device, and its `bus/bbc/analogue/joystick.cpp` models only the Voltmace Delta 3B twin joysticks
(checked against the MAME repository listing). B-Em and BeebEm have nothing either. The round 5
model is written from [M] and the test vectors alone.

Elite-A's Delta 14B build ([E], and the deep dive at
`elite.bbcelite.com/deep_dives/elite-a_delta_14b_joystick_support.html`) independently confirms
question 1: PB4 to PB7 outputs, PB0 to PB3 inputs, a clear bit in PB4 to PB6 selects the column, bit 7
set is the side socket, a clear row bit is the pressed row, %1111 is no key. Its `b_14` routine scans
the side socket and then the rear because it does not know which holds the handset, and reads the
fire buttons through the keypad, not the analogue port. The Elite Compendium's Delta 14B notes tell
players to use the rear socket and leave the side socket empty.

## 9. A handset straight into the analogue port

Settled from [M p.1-2], explained by [P].

- Keys 9, 10, 11 and the two extra key-10 buttons read as fire 0 (connector PB0, System VIA PB4).
  Keys 6, 7, 8 read as fire 1 (connector PB1, System VIA PB5). Keys 0 to 5 do nothing.
  `ADVAL(0) AND 3` is 1 for the top row and 2 for the next.
- Why: the handset's column pins (DA-15 pins 2, 3, 5) sit on 0 V and analogue ground on a BBC, so
  every column is grounded and each row line is a plain button. Row 3 is on pin 13 (PB0) and row 2 on
  pin 10 (PB1); row 1 lands on pin 6 (0 V) and row 0 on pin 4 (CH3), so the bottom six keys are lost.

This confirms the round 4 statement that a bare handset is the plain joystick with two logical keys,
and it also says which keys: the round 4 documentation can name them.

## The logical wiring, as far as it is known

| User port line | Direction | Role | Handset line | DA-15 pin |
|---|---|---|---|---|
| PB0 | in | 74LS157 Y output | Row 3 (keys 9, 10, 11) | 13 |
| PB1 | in | 74LS157 Y output | Row 2 (keys 6, 7, 8) | 10 |
| PB2 | in | 74LS157 Y output | Row 1 (keys 3, 4, 5) | 6 |
| PB3 | in | 74LS157 Y output | Row 0 (keys 0, 1, 2) | 4 |
| PB4 | out | Column 0 strobe, both sockets | Column 0 (0, 3, 6, 9) | 5 |
| PB5 | out | Column 1 strobe, both sockets | Column 1 (1, 4, 7, 10) | 3 |
| PB6 | out | Column 2 strobe, both sockets | Column 2 (2, 5, 8, 11) | 2 |
| PB7 | out | 74LS157 select: 0 = rear, 1 = side | | |

M p.5 confirms the block structure: PB0 to PB3 pass through the select chip, PB4 to PB6 are common to
both sockets. Which socket feeds the chip's A inputs and which the B inputs is not established, but it
does not matter for behaviour once PB7 = 0 → rear is fixed. The DA-15 pin column assumes the box
passes each socket's pins straight through, which it cannot do entirely, since the rows sit on pins
the BBC port defines as fire, 0 V and CH3; the box must break some of those. [S] shows the sockets
and the analogue port heavily cross-wired.

## What the round 5 specification can fix, and what stays open

Fixed by the sources: the matrix (question 1), the twelve keys and their ids (2), the select in the
normal case (3), channel assignment and direction (5), the test vector (7), the bare handset (9).

Open, to be written as `Decision`s and confirmed on hardware or from a clearer schematic or board
photographs: the fire-line routing through the box when the columns are driven low from the user
port (4), the select with PB7 as an input (3), and CB2 (6). No emulator models the box (8), so
these can only be settled by a clearer schematic, board photographs, or a measurement.
