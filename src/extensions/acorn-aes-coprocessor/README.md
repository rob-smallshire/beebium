# AES plugin

A Beebium coprocessor plugin emulating the Acorn ARM Evaluation System: an ARM1 second processor on the BBC Micro Tube. Started as a private hobby project. Written from the documents only.

It reaches the AES banner and the `A*` prompt, and `AB`, `AAsm`, ClockSp, SNTest, the FPE and LISP all run. How to build it is in `BUILDING.md`, at the top of the package; how to use it is in `docs/user-guide.md`.

## Files

- `ArmCore.hpp`, `ArmCore.cpp`: the ARM1 core, the class `ArmCore`. `ArmCore.hpp` also defines two types the core uses:
  - `ArmOptions`, a struct: the core's uncertain behaviours as switches, each with a default (see `docs/arm1-and-aes.md`, Part 2). Guesses with no switch are marked `7a` in the code.
  - `ArmBus`, an abstract class: the memory interface the board must supply to the core.
- `AesOptions.hpp`: the struct `AesOptions`, every run-time choice for the board, including `ram`, `abort` and `rom`, with the core's `ArmOptions` inside it as `cpu`.
- `AesMemory.hpp`, `AesMemory.cpp`: the memory map behind `ArmBus`. `TubePort` is what the Tube side, `beebium/AesTube`, supplies.
- `AesTiming.hpp`, `AesTiming.cpp`: board time in crystal ticks.
- `AesOptions.cpp`: the table of switches. Three are Beebium settings, in `manifest.json`; the rest are changed by editing a default and rebuilding.
- `beebium/`: the parts that need Beebium. `AesTube` runs the board against the host's clock; `AesExtension` makes it a plugin, with `plugin_entry.cpp` at the top of the folder, as in Beebium's own coprocessors; `TubeLog.hpp` is the counting wrapper, a diagnostic compiled in only by the test build.
- `CMakeLists.txt`, `manifest.json`: packaging. Build with `tools/plugin-build.sh`. `manifest.json` also holds AESulator's version, which `CMakeLists.txt` passes to the plugin's start-up line.
- `tests/`: `test_data.cpp` (data processing, shifts, conditions), `test_memory.cpp` (LDR, STR, LDM, STM), `test_control.cpp` (branches, register banks, mode changes, reset, pipeline), `test_exceptions.cpp` (SWI, undefined, IRQ, FIQ, address exceptions, aborts), `test_cycles.cpp` (cycle counts, the manuals' example programs), `test_aes_memory.cpp` (map and boot latch), `test_aes_tube_abort.cpp` (Tube addresses, ABORT, the map's timing), `test_aes_timing.cpp`, `test_a_defaults.cpp` (built-in defaults within their limits; runs first), `test_aes_options.cpp` (the switches, against the profile and `manifest.json`), `test_aes_tube.cpp` (the ARM side of the Tube, with Beebium's Tube chip linked and called), `test_aes_banner.cpp` (the banner, tied to the working ROM), `test_aes_rom_facts.cpp` (the ROM facts files, against the ROM), `test_aes_tube_log.cpp` (the counting wrapper), `test_aes_board.cpp` (the board, including one small program under every RAM size and abort setting), `test_aes_exceptions.cpp` (each exception on the board, own handlers), `test_z_executive.cpp` (exceptions through the real Executive; runs last), `ArmTest.hpp` (the test rig), `AesBoardTest.hpp` (what the board tests share), `RomCheck.hpp` (ties a test to its ROM), `test_main.cpp`, `run-tests.sh`, `milestone.cpp`, `run-milestone.sh`; in the Beebium tree: `machine_test.cpp` (the AES inside a real Model B, in one process); for a real server: `test_server.py`, `test_tube_wire.py` (what the Tube carries, through the real Executive and BBC host), `test_tube_cover_io.py`, `test_tube_cover_swis.py` and `test_tube_cover_break.py` (the rest of the SWI index and Tube tables, with words typed at `A*`: OSWORD and files; the screen, keyboard and other SWIs; the cache and soft Break), `cover_support.py` (what those three share), `test_portability.py` (paths, skipping, the ship folder), `test_tube_log.py` and `tube_log.py` (the wrapper's logs), `aes_host.py` (drives them), `aes-banner.cpp` (prints the banner expected for a ROM and a RAM size, for the server tests), `conftest.py`, `dfs_image.py`, `pyproject.toml`, `uv.lock` (the exact versions of the Python test packages, which uv installs), `test-env.sh`, `run-server-tests.sh`. `rom-facts/`: what the tests expect of each ROM, one file named by its SHA-1.

Names starting with `k` are constants.

## Build and test

Needs `clang++` (C++20) and a working copy of Beebium, because the tests link its Tube chip. From nothing, build Beebium first by its own instructions. Where things are is in one file, `aes-paths.sh`, copied from `aes-paths.template.sh`; a variable already set wins.

    acorn-aes-coprocessor/tests/run-tests.sh

It copies the source to a scratch folder under Beebium's build folder, builds there and prints a summary with the counts failed and skipped. The Executive tests read the ROM named in `AES_ROM`, and the run prints its SHA-1. Without the ROM, or with a ROM that has no file in `tests/rom-facts/`, the tests tied to it skip and say why. `AES_REQUIRE_ALL=1` turns any skip into a failure. Where there is a `scratch/` folder, a passing run writes the table of tests to `scratch/plugin-tests.md`, not committed. Always build from that copy: the compiler can misread a freshly edited file on the shared folder. Add memory checks with:

    EXTRA_FLAGS="-fsanitize=address,undefined" acorn-aes-coprocessor/tests/run-tests.sh

The milestone run takes the Executive ROM from reset to its first Tube access at 2 and 4 MB, with `abort` off and strict, then traces it under strict, all with both empty-read values of `ram.absent_bank`:

    acorn-aes-coprocessor/tests/run-milestone.sh <rom file>

To check the tests themselves, `tools/fault-check.py` breaks a copy of the code in small deliberate ways, one at a time, and sees whether the tests notice:

    python3 -u tools/fault-check.py | tee results/fault-check.log

Run it from the project folder. The list of breakages, and how to add one: `tools/fault-list.py`.

## Rules

Each test names the fact it checks. A test is changed only when the test itself is wrong or a newly found document overrides it, and every change is reported with its reason. Each comment names the source of a behaviour. A label in a comment says what kind of source it is: DOC a document, SRC source code or the ROM's own bytes, INF inferred, USER the project owner's own knowledge, AGREED a proposal the owner approved, UNK not known.
