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
