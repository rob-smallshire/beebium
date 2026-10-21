# External References

Useful external documentation for BBC Micro emulation development.

## Memory Maps

- **BBC Micro Complete Memory Map**: https://mdfs.net/Docs/Comp/BBC/AllMem
  Comprehensive reference covering zero page, stack, OS workspace, sideways RAM/ROM, I/O, and hardware registers. Essential for understanding memory layout differences between configurations (e.g., DFS vs CFS filing system workspace at $B0-$BC).

## Hardware Documentation

- **BBC Micro Advanced User Guide**: https://stardot.org.uk/mirrors/www.bbcdocs.com/filebase/essentials/BBC%20Microcomputer%20Advanced%20User%20Guide.pdf
  Official Acorn documentation covering hardware architecture, memory map, and MOS entry points.

- **6502.org**: http://www.6502.org/
  Comprehensive 6502 CPU documentation including instruction set, timing, and hardware details.

## Emulator References

- **jsbeeb**: https://github.com/mattgodbolt/jsbeeb
  JavaScript BBC Micro emulator used as test oracle for differential testing.

- **beebjit**: https://github.com/scarybeasts/beebjit
  Chris Evans' BBC Micro emulator. Beebium's Intel 8271 model (`Intel8271.hpp`, GPL-3.0 only) is derived from its `intel_fdc.c`, built from his reverse engineering of the chip: https://scarybeastsecurity.blogspot.com/2020/11/reverse-engineering-forgotten-1970s.html

- **b-em**: https://github.com/stardot/b-em
  Cross-platform BBC Micro emulator with extensive hardware support.

- **BeebEm**: https://github.com/stardot/beebem-windows
  Windows BBC Micro emulator, original source for many peripheral implementations.
