# Disc test assets

## tube_r3_tests.ssd

hoglet's Tube ULA register 3 test disc, from the "Tube ULA Re-Implementation"
thread on Stardot (https://stardot.org.uk/forums/viewtopic.php?t=28080).
Attached to https://stardot.org.uk/forums/viewtopic.php?p=409877 (download
file id 97037). 200 KiB, SHA-256
`0ec5fdf990b6aec236d8a5f5f40b4bd04d9cfa477fe2f02792db7079b760b226`.

Needs a 6502 second processor (`--tube-65c02`). Catalogue:
`$.R3TEST` (`CHAIN "R3TEST"`, hoglet's R3 FIFO test) and `$.ULA` (his
follow-up R1/R2/R4 FIFO test). Used by the scenario test that reproduces
issue #71 (a host write to a full Tube R3 register stalls the host).

hoglet published this program on that thread as a test case for emulator and
Tube ULA implementers, inviting others to run it against their implementations;
it is included here in that spirit. (Redistribution remains the author's call.)

## tube_speed70.ssd

A synthetic coprocessor speed probe generated for issue #70 (the second
processor runs about 4% fast). `$.R70` (`CHAIN "R70"`) assembles two loops of
identical structure and cycle count -- one all LDA zp, one all STA zp -- times
each with the host TIME, and prints the effective MHz. It reproduces
tom_seddon's cheese-wedge measurement (LDA about 2.93 MHz, STA about 2.70 MHz;
the internal 65C102 about 3.94 for both). Used by the scenario test
test_tube_speed.py.

Not third-party: the BASIC source is `tube_speed70.bas` in this directory, and
the disc is `oaknut-basic tokenise` of it written to a DFS SSD with
`oaknut-disc`. Regenerate with those tools if the program changes.

## TakBasicAsm.ssd

acheton1984's "Tak on 6502" benchmark disc, the end-to-end fidelity check for
the second processor's speed (issue #70). Author acheton1984; MIT licence
(github.com/acheton1984/ReTestingTheTak/LICENCE); version 1.0, 2025-03-20;
source https://raw.githubusercontent.com/acheton1984/ReTestingTheTak/main/discs/TakBasicAsm.ssd
(204800 bytes, SHA-256
`3feaab6c4a893d1e4edb61a9e10395c584502ee27f23f0b56fe762cc4289485b`). The disc's
own attribution notes some code is adapted from Acorn User June 1986 p179 and
November 1986 p197.

Needs a 6502 second processor (`--tube-65c02`). Catalogue: `$.!BOOT`,
`$.!ReadMe`, `$.TAK`, `$.TAKAsm`, `$.TAKfp`, `$.TAKscv`, `$.TAKstr` (plus their
A./S. source variants). Used by test_tak_benchmark.py: CHAIN "TAKAsm" prints
`TAK(18,12,6)=7 Time=NNN` in centiseconds; hardware is 264 (2.64 s on a BBC B
OS 1.20 + 6502 Second Processor).

## 6502timing.ssd, 6502timing1M.ssd

Dominic Plunkett's (dp111) 6502 instruction timing test suite, from
https://github.com/dp111/6502Timing. GPL-3.0 (the same licence as Beebium),
version 0.24, pinned upstream commit
`2cb005d7c04eee011c43e0954d9243dcfc4f5ffb`.

- `6502timing.ssd` -- 12800 bytes, SHA-256
  `c69941c5e607b8d3954b6f5e2006dd6790a61d2fc5a4abc8f5eda66be12f887c`.
- `6502timing1M.ssd` -- 12800 bytes, SHA-256
  `2824451d75d8a84109204ee3b6ebdc1d40b3e827200aae75fb892f5cfcc9b48c`. Places the
  timed absolute addresses at &FCFE, straddling the 1 MHz page boundary, so the
  suite also measures Beebium's own 1 MHz bus cycle stretching.

Standard Model B (no second processor). Each disc is `!Boot` (SHIFT-BREAK)
bootable and also runs from BASIC with `*RUN 6502tim`. The suite times almost
every documented and undocumented NMOS 6502 instruction against the 1 MHz System
VIA timer 1, prints any instruction whose timing is wrong, writes the failure
count to zero page &7A (`passfailzp`) and to &FCD0 (FRED, "so emulators can trap
writes to this address"), and prints `Number of failures : 0xNN`. About four
emulated seconds each. Both pass on master today, so the Python scenario test
test_dp111_timing.py and the C++ &FCD0-trap test test_dp111_timing_fcd0.cpp use
them as regression guards for the core, the VIA timer and the 1 MHz stretch --
not as reproductions of a known defect. Not checked by the suite, per its
README: BRK and the jam (HALT) instructions.
