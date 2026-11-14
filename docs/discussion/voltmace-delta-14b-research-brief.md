# The Voltmace Delta 14B and the Delta 14B/1 adapter: round 5 research brief (#98)

Round 4 built the controller seam, `ControllerService` and the plain joystick. Round 5 adds the first
controller that uses two ports at once: the Voltmace Delta 14B handset through the Delta 14B/1 adapter
box, which plugs into the analogue port and the user port and lets two handsets be used together. This
document is the research brief; the implementation specification follows once the questions below are
answered from sources, with each answer cited.

## What we know

- A Delta 14B handset is a joystick with 14 physical buttons that present as 12 logical keys. Plugged
  straight into the analogue port it is electrically the round 4 joystick: two pots on two channels and
  two logical keys on the two fire lines. Round 4 covers that case and nothing here changes it.
- The Delta 14B/1 adapter connects to the analogue port and the user port. The pots of both handsets
  reach the uPD7002 through the analogue port (handset 1 on channels 0 and 1, handset 2 on 2 and 3, to
  be confirmed). The keypads reach the user port VIA. A 74LS157 quad 2-to-1 multiplexer inside the box
  selects which handset's keypad lines reach the VIA, and user port PB7 drives the select line.
- Software strobes the keypad matrix through the user VIA (`&FE60` ORB, `&FE62` DDRB), reads the pots
  with OSBYTE &80 / `ADVAL`, and picks the handset with the top bit of the port B value.

## What the specification needs to know

Answer each from a primary source where one exists (the Voltmace manual or leaflet, a schematic, a
photograph of the board, a disassembly of software that drives it), and from a secondary source
(MAME, B-Em, BeebEm, stardot threads) where it does not. Record the source beside each answer. Where
sources disagree, record both and say which the specification should follow and why.

1. **Keypad matrix.** The user port has eight data lines, PB0 to PB7, with PB7 taken by the select.
   Which of PB0 to PB6 are driven as strobes (VIA outputs) and which are read (VIA inputs)? How many
   rows and columns, and which key sits at each intersection? Are the key lines active low with
   pull-ups, as the fire lines are?
2. **The 14 to 12 mapping.** Which two pairs of physical buttons share a logical key? What are the
   printed labels on the handset, and what should the twelve control ids and display names be? The
   ids must be stable, 7-bit ASCII and dotted (`handset1.key7`, or by label if the labels are stable
   and short); the display names are the printed labels.
3. **Select polarity.** Does PB7 high select handset 1 or handset 2? What does the adapter present
   when PB7 is an input (DDR bit 7 clear) and so floats or is pulled?
4. **The fire lines through the adapter.** When a handset is plugged into the adapter rather than the
   port, do any of its keys still reach the analogue port's PB0 and PB1 (System VIA PB4 and PB5)? If
   so, which keys, and for which handset?
5. **Channel assignment.** Confirm handset 1 on channels 0 and 1 and handset 2 on channels 2 and 3,
   and the stick direction convention (which way is 0, which way is 65535) for X and Y. The round 4
   joystick follows the Acorn convention in `analogue-port.md`; say whether the Delta is the same.
6. **CB1 and CB2.** Does the adapter use the user port's CB1 or CB2 lines for anything?
7. **Software as test vectors.** Name at least two programs known to support the Delta 14B through
   the adapter, and for one of them find or produce a disassembly of its keypad-reading routine: the
   DDRB value it writes, the strobe sequence, the mask it reads and the select handling. The routine is
   the test vector that the Catch2 tests drive, as MOS 1.20 was for the converter.
8. **Prior art.** Does MAME model this adapter (the `bbc_userport` or `bbc_analogue` slot devices)?
   If so, read its implementation and record how it answers questions 1 to 6; do not copy its code,
   and treat it as a secondary source to be checked against the primary ones.
9. **The handset alone.** Confirm that a handset plugged straight into the analogue port presents two
   logical keys on PB0 and PB1, and which keys they are, so `docs/analogue-port.md` can say so.

## Constraints on the eventual design

These follow from rounds 3 and 4 and are not open:

- One extension, `voltmace-delta-14b-1` (name open to a better one), with `attaches_to` listing
  both `analogue-port` and `user-port`, implementing `AnaloguePortDevice`, `UserPortDevice` and
  `Controller`. The framework already allows a manifest to attach to several points.
- It publishes two handsets as groups ("Handset 1", "Handset 2"), each a stick (two axes) and twelve
  keys, in the device's own vocabulary. The service and the clients do not change.
- The user port side sees the VIA's port B outputs and DDR through `update_port_b(output, ddr)` and
  answers the input lines from atomics, so the emulation thread never waits on a lock.
- Both handsets' values persist across Break and power-on.
- No MOS dependence; the test vectors are a disc-loaded program and the game routine from question 7.

## Deliverable

A short findings document, `docs/discussion/voltmace-delta-14b-findings.md`, answering the nine
questions with citations, plus any reference material worth keeping under `docs/` (a transcribed
manual page, a redrawn schematic as text or a table; never a copied scan of a copyrighted document).
Then stop; the architect writes the round 5 specification from the findings.

Same rules as the earlier rounds: `git commit --only`, never amend, never push, no people or host
names, forum threads summarised and not quoted at length.
