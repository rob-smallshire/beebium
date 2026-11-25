# AES plugin

A Beebium coprocessor plugin emulating the Acorn ARM Evaluation System: an ARM1 second processor on the BBC Micro Tube. Started as a private hobby project. Written from the documents only.

It reaches the AES banner and the `A*` prompt, and `AB`, `AAsm`, ClockSp, SNTest, the FPE and LISP all run. How to build and test it in Beebium's tree is below. `docs/` paths named in this folder are AESulator's own notes, kept in its project and not in Beebium's tree.

## Files

- `ArmCore.hpp`, `ArmCore.cpp`: the ARM1 core, the class `ArmCore`. `ArmCore.hpp` also defines two types the core uses:
  - `ArmOptions`, a struct: the core's uncertain behaviours as switches, each with a default (see `docs/arm1-and-aes.md`, Part 2). Guesses with no switch are marked `7a` in the code.
  - `ArmBus`, an abstract class: the memory interface the board must supply to the core.
- `AesOptions.hpp`: the struct `AesOptions`, every run-time choice for the board, including `ram`, `abort` and `rom`, with the core's `ArmOptions` inside it as `cpu`.
- `AesMemory.hpp`, `AesMemory.cpp`: the memory map behind `ArmBus`. `TubePort` is what the Tube side, `beebium/AesTube`, supplies.
- `AesTiming.hpp`, `AesTiming.cpp`: board time in crystal ticks.
- `AesOptions.cpp`: the table of switches. Three are Beebium settings, in `manifest.json`; the rest are changed by editing a default and rebuilding.
- `beebium/`: the parts that need Beebium. `AesTube` runs the board against the host's clock; `AesExtension` makes it a plugin, with `plugin_entry.cpp` at the top of the folder, as in Beebium's own coprocessors; `TubeLog.hpp` is the counting wrapper, a diagnostic compiled in only by the test build.
- `CMakeLists.txt`, `manifest.json`: packaging, built with Beebium's servers (below). `manifest.json` also holds AESulator's version, which `CMakeLists.txt` passes to the plugin's start-up line.
- `roms/`: the bundled Executive ROM, `acorn-aes-executive_1_00.rom`, which `manifest.json` declares and the `rom` setting uses by default.
- `tests/`: `test_data.cpp` (data processing, shifts, conditions), `test_memory.cpp` (LDR, STR, LDM, STM), `test_control.cpp` (branches, register banks, mode changes, reset, pipeline), `test_exceptions.cpp` (SWI, undefined, IRQ, FIQ, address exceptions, aborts), `test_cycles.cpp` (cycle counts, the manuals' example programs), `test_aes_memory.cpp` (map, boot latch and loading the ROM), `test_aes_tube_abort.cpp` (Tube addresses, ABORT, the map's timing), `test_aes_timing.cpp`, `test_a_defaults.cpp` (built-in defaults within their limits; runs first), `test_aes_options.cpp` (the switches, against the profile and `manifest.json`), `test_aes_tube.cpp` (the ARM side of the Tube, with Beebium's Tube chip linked and called), `test_aes_banner.cpp` (the banner, tied to the working ROM), `test_aes_rom_facts.cpp` (the ROM facts files, against the ROM), `test_aes_tube_log.cpp` (the counting wrapper), `test_aes_board.cpp` (the board, including one small program under every RAM size and abort setting), `test_aes_exceptions.cpp` (each exception on the board, own handlers), `test_aes_debug.cpp` (the debugger's registers, memory, steps and resets), `test_aes_debug_stops.cpp` (the debugger's breakpoints and watchpoints), `test_z_executive.cpp` (exceptions through the real Executive; runs last), `ArmTest.hpp` (the test rig), `AesBoardTest.hpp` (what the board tests share), `DebugRig.hpp` (what the debugger tests share), `RomCheck.hpp` (ties a test to its ROM), `test_main.cpp` (the runner); `machine_test.cpp` (the AES inside a real Model B, in one process). `rom-facts/`: what the tests expect of each ROM, one file named by its SHA-1.

Names starting with `k` are constants.

## Build and test

The plugin builds with Beebium's servers, by Beebium's own build instructions: `cmake --build build --target beebium-servers` builds it into the server's `extensions/` folder, and a build with tests also makes `test_aes_plugin`, `test_aes_machine` and the Tube-log test plugin (`tube-log/` in the plugin's build folder: the plugin with `TubeLog.hpp` compiled in, loaded with `--extension-dir`). Run the C++ tests with:

    ctest --test-dir build -R aes --output-on-failure

`aes_plugin_tests` runs every `tests/test_*.cpp` on the plugin's own runner and prints a summary with the counts failed and skipped; the `AES:` tests are `machine_test.cpp`'s, under Catch2. The Executive tests read the bundled ROM, or the one named in `BEEBIUM_AES_ROM_FILEPATH`. With a ROM that has no file in `tests/rom-facts/`, the tests tied to it skip and say why; `AES_REQUIRE_ALL=1` turns any skip into a failure.

The tests of the AES in a real server are Python, in Beebium's Python client suite at `clients/beebium-python-client/tests/aes/`, and run with the rest of that suite, against the server it is given:

    cd clients/beebium-python-client
    uv run python -m pytest tests/aes --beebium-server=../../build/src/server/beebium-model-b

## Rules

Each test names the fact it checks. A test is changed only when the test itself is wrong or a newly found document overrides it, and every change is reported with its reason. Each comment names the source of a behaviour. A label in a comment says what kind of source it is: DOC a document, SRC source code or the ROM's own bytes, INF inferred, USER the project owner's own knowledge, AGREED a proposal the owner approved, UNK not known.
