# Computech Integra-B

Reference material for the `beebium-model-b-integra-b` machine variant
(GitHub issue #96): a BBC Model B fitted with the Computech Integra-B
expansion board and its IBOS ROM.

## Sources

| File | What it is |
|------|------------|
| `Integra-B User Guide for IBOS v1.20-Rev02a-Final.pdf` | The Computech user guide for IBOS 1.20, rebuilt as a searchable document by Ken Lowe (2024). Sections 8-2 (memory registers), 9-4 (fitting RAM/ROM) and 9-6 (write protection) define the hardware behaviour emulated. |
| `IntegraB.pdf` | KiCad schematic of the board (rev 1a), shared on Stardot by Ken Lowe, who amended the RTC sheet for compatibility with later CDP6818 parts. |
| `IntegraSSD/` | Integra system discs (Integra Windows, IntBEEP). |

Khazul, the remaining rights holder of the original Integra-B software and
hardware, has said it may be used in emulators such as Beebium
(<https://stardot.org.uk/forums/viewtopic.php?p=465436#p465436>).

The bundled ROM `roms/computech-ibos_1_26.rom` is IBOS 1.26 (2022), the
community-maintained rebuild of Computech's IBOS 1.20. It is compatible with
the 1.20 guide.

## Hardware, as emulated

- **Sideways slots** use full 4-bit decoding:
  - 0-3: the motherboard ROM sockets (ROM or empty);
  - 4-7: the board's two 32K static RAMs, always fitted;
  - 8-15: four socket pairs (8/9, 10/11, 12/13, 14/15). Each socket holds a
    ROM or is empty, or a pair holds one 32K RAM covering both of its slots.
    `--sideways` has to fit RAM to both slots of a pair.
- **ROMSEL `&FE30`** (write-only, mirrored to `&FE33`): bits 0-3 select the
  bank, bit 6 is PRVEN and bit 7 is MEMSEL.
- **RAMSEL `&FE34`** (write-only, mirrored to `&FE37`): bit 4 is PRVS8
  (`&9000-&AFFF`), bit 5 PRVS4 (`&8000-&8FFF`), bit 6 PRVS1 (`&8000-&83FF`)
  and bit 7 SHEN.
- **Shadow RAM** (20K) replaces `&3000-&7FFF` for the CPU when SHEN is set
  and MEMSEL is clear. The video circuitry always reads main memory.
- **Private RAM** (12K) overlays `&8000-&AFFF` according to PRVEN and the
  PRVS bits (guide 8-2 truth table). Shadow and private RAM are one 32K
  chip.
- **Reset:** both latches are cleared by the 6502 reset line, so by Break
  as well as power-on.
- **Real-time clock:** a CDP6818 (MC146818-compatible). The address goes to
  `&FE38` and data is read or written at `&FE3C`. Its IRQ output drives the
  CPU IRQ line, and its RESET pin is the 6502 reset line. The calendar is
  host local time plus an offset that the guest sets, so the clock keeps
  real time across host sleep. The periodic interrupt is driven by emulated
  cycles.
- **Battery backup:** a launch starts from a board that has been set up,
  as if IBOS's Full System Reset and `*CONFIGURE LANG 3` had already been
  run and the clock set (`IntegraBBatterySeed.hpp`). Changes made during a
  session are not persisted between launches.
- **Write protection** works per RAM chip, so it always covers two slots.
  The protection groups are `slots-4-5` and `slots-6-7`, plus `slots-8-9` to
  `slots-14-15` for socket pairs fitted with RAM. Set them at launch with
  `--write-protect <group>`, or at runtime with
  `SidewaysService.SetSlotProtection`. IBOS's `*ROMS` shows protected banks
  as `P`.

RAM fitted in the socket pairs must be declared to IBOS, as the guide
explains (section 1-5). For example, a chip in socket 9 needs
`*FX162,127,31`; after Ctrl-Break the banner then reads `INTEGRA-B 160K`.
