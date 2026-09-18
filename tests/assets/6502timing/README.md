# dp111 6502 timing suite -- vector builds

Vector-table builds of Dominic Plunkett's (dp111) 6502 instruction timing test
suite, from https://github.com/dp111/6502Timing. GPL-3.0 (the same licence as
Beebium), version 0.24, pinned upstream commit
`2cb005d7c04eee011c43e0954d9243dcfc4f5ffb`.

- `6502timing.6502` -- 11848 bytes, SHA-256
  `95478ce8a1534ea1490d48426e87d78fe16acc61f8e6446cda35380ed86fd958`. NMOS 6502
  target, no 1 MHz stretching (timed addresses in normal RAM at &08FE).
- `65C02timing.6502` -- 11048 bytes, SHA-256
  `e2f2aaa711421b3287c021597be25a3ec079df2e15da60cde27eff14c403545e`. CMOS
  target including the Rockwell bit instructions RMB/SMB/BBR/BBS.

These are the same tests as the `.ssd` images in `../discs/`, but built as raw
`ORG &2000` binaries rather than inside a disc, with a fixed vector table so the
timer and character-output routines can be patched for a host that is not a whole
BBC Micro:

| Address | Function |
| ------- | -------- |
| &2000   | entry point (`JMP starttest`) |
| &2010   | print character, A = char, must preserve X and Y |
| &2020   | initialise timer and screen |
| &2030   | start timer with the value in A, must preserve X and Y |
| &2040   | read timer into X |
| &2050   | end of tests, A = number of failures |

`test_6502_dp111_timing.cpp` loads these into a bare 6502 core with flat 64K
memory, models the 1 MHz VIA timer 1 by counting core cycles behind the
&FE64/&FE65 bus accesses, captures printed characters, and asserts zero failures.
The NMOS build runs on `M6502_nmos6502_config` (and agrees with the whole-machine
result from `6502timing.ssd`); the CMOS build runs on
`M6502_rockwell65c02_config`, the configuration the Tube coprocessor plugins use.
See the file header of that test for the derivation of the timer model. Not
checked by the suite, per its README: BRK and the jam (HALT) instructions.
