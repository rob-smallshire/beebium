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
