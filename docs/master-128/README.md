# BBC Master 128 reference material

Reference material for the Master 128 variant (#189). The research and
decisions are in `docs/discussion/master-128-research.md`, the first round's
plan in `docs/plans/master-128-round-1.md`. The main PCB circuit diagram and
the MC146818 datasheet are in the gitignored `docs/manuals/` and
`docs/datasheets/` directories (the datasheet's transcript,
`docs/datasheets/MC146818.md`, is committed).

## Acorn Master ROM Cartridge schematic

`Acorn Master ROM Cartridge Schematic.tiff` is the schematic of Acorn's own
ROM cartridge for the Master's cartridge slots, with a PNG rendering beside it
for viewing. What it shows:

- Two 27128 sockets (IC1, IC2) and a 74HCT139 dual decoder (IC3) on the
  44-way edge connector PL1. Only A0-A13 and D0-D7 reach the sockets; the
  cartridge has no A14 or A15 of its own.
- Bank selection: one half of the decoder takes CS (the slot's chip select,
  edge pin A4) and ROMQA (ROMSEL bit 0, pin A16). With CS high, ROMQA 0
  enables IC2 and ROMQA 1 enables IC1, so IC2 is the slot's even bank (ROM 0
  or 2) and IC1 the odd one (ROM 1 or 3). A cartridge with one chip fitted
  answers on one bank only; a 16K cartridge that answers on both needs the
  chip select arranged otherwise.
- Read and write: the other half of the decoder is enabled by the slot's nOE
  (pin A2, pulled up by R1) and steered by R/W (pin A11). A read drives both
  chips' nOE; a write drives both chips' pin 27, the 27128's PGM pin. That is
  the "write-enable plumbed through" that lets a 27513 paged EPROM in the
  same socket take its page number from a write (stardot thread 13799), and
  it is why a RAM-test write from a ROM or the MOS would change a 27513
  cartridge's page.
- Power: +5V on A1/B1, 0V on A22/B22 (A20 unconnected), one decoupling
  capacitor.

The 27513's own datasheet is `docs/datasheets/Intel_27513.pdf`, with a
transcript beside it (`Intel_27513.md`): a 64K EPROM as four 16K pages, the
page chosen by a write cycle whose data bits select it, and a page latch that
clears to page 0 at power-up.

For Beebium: the built-in `acorn-rom-cartridge` extension's two parameters are
these two sockets, the lower answering on the slot's even bank and the upper
on the odd one; a paged 27513 cartridge would be a later provider that turns
a write into a page change (research document, section 1.6).
