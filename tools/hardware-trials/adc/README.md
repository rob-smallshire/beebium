# uPD7002 analogue port trials: the self-running disc

This directory builds `adc-trials.ssd`, a bootable DFS disc that runs the seven
hardware trials in `docs/discussion/analogue-port-hardware-trials.md` on a real
BBC Micro Model B. The disc doesn't need typing at the BBC. Each program says
on screen what to do with the joystick, waits for SPACE, takes its
measurements, and writes the results back to the same disc. The measurement
code, timers, interrupt handling and expected readings are the ones in the
trial sheet; only the packaging differs.

## Files

| File | What it is |
|---|---|
| `MENU.bas` | The menu: machine notes, a status line per trial, run-all, self-test |
| `T1.bas` ... `T7.bas` | One program per trial |
| `COMMON.bas` | Shared procedures (prompts, results buffer, error handling), appended to every program when the disc is built |
| `BOOT.txt` | The text of `!BOOT`, which chains `MENU` |
| `build_disc.py` | Builds the disc |
| `decode_results.py` | Turns a returned disc into a report and CSV files |
| `selftest_beebium.py` | Runs the menu's Z self-test on a Beebium Model B, to check the disc |

The sources are 7-bit ASCII, BBC BASIC 2 with inline assembler. They use only
the filing operations DFS 0.90 has: `*SAVE`, `OPENIN`, `BGET#`, `EOF#` and
`CLOSE#`.

## Building

You need `uv` on the PATH; the scripts use only the Python standard library and
call `uvx oaknut-basic` and `uvx oaknut-disc` (at least 12.10.0, the first
release that writes the DFS catalogue in the order DFS itself keeps it; with an
earlier one, the first `*SAVE` on the BBC overwrites a program).

```
python3 tools/hardware-trials/adc/build_disc.py OUTPUT_DIRPATH
```

This writes `OUTPUT_DIRPATH/adc-trials.ssd` (the current directory if you
leave the argument out).

For a Master 128, build with `--machine master`:

```
python3 tools/hardware-trials/adc/build_disc.py --machine master OUTPUT_DIRPATH
```

This writes `adc-trials-master.ssd`. The Master's uPD7002 is at &FE18-&FE1F
rather than &FEC0-&FEDF, and its four registers repeat only once, at &FE1C.
The build rewrites the chip addresses in trials 1, 2, 3 and 7, and points
trial 2's mirror reads at &FE1C to &FE1F. It refuses to finish if any Model B
address survives. Nothing else changes: MOS 3.20 keeps the ADC workspace
where MOS 1.20 does (&24C, &24D), and its `*FX16` behaves the same. The
Master disc's menu checks the MOS type (OSBYTE 0) and warns if the machine
isn't a Master. The machine notes record `disc=master`. It prints the size of each tokenised program. Never
commit the `.ssd`.

## Running it on a Model B (or a Master)

Use `adc-trials.ssd` on a Model B and `adc-trials-master.ssd` on a Master 128;
the instructions are otherwise the same. On the wrong machine the timing loops
read a register that isn't the ADC and hang.

1. Write the image to a floppy (an 80-track single-sided DFS disc), or load it
   in a Gotek or similar drive emulator. **Leave the disc writable**: the
   results are saved to it.
2. Put it in drive 0 and press **Shift+Break**. The menu appears.
3. The first time, the menu asks for the machine notes: the board issue number
   if known, the start-up banner, how many minutes the machine has been on,
   and whether a joystick is available. Type each and press RETURN. The notes
   are kept in `M.INFO`; N on the menu asks again.
4. Press **R** to run every trial that has no result yet, in order, or a
   letter A to G to run one trial. Follow the prompts. Most need only SPACE
   once the joystick is where the prompt says. Trial 7 shows a plot after each
   sweep; photograph it if you can, then press any key. Trial 6 asks for a
   voltmeter reading; press RETURN alone to skip it.
5. A trial's results are saved when it finishes, and the menu then shows
   `done` beside it. **Break at any time** loses only the trial in progress:
   press Shift+Break to get back to the menu with everything finished so far
   intact.
6. Press **Q** at the menu when you have finished. It restores normal joystick
   sampling (`*FX16,4`).

Without a joystick, trials 5 and 7 record that they were skipped, and trials 1
and 2 run only the parts that need no stick.

**Do not use Z on real hardware.** It writes made-up results, marked
`selftest=1`, for checking the disc on an emulator. It refuses to run if the
disc already holds real results. A real run of any trial overwrites self-test
results, and the menu shows them as `self-test`, not `done`.

The machine-code parts of trials 1, 3 and 7 can't be stopped with Escape. If
one hangs, press Break, then Shift+Break.

## What the disc writes

| File | Contents |
|---|---|
| `M.INFO` | Machine notes |
| `R.T1` ... `R.T7` | One `key=value` line per measured quantity, CR line endings |
| `R.T7A`, `R.T7B`, `R.T7C` | Trial 7's summary for each sweep: control byte &08 full range, &00 full range, and the optional slow sweep across the middle |
| `D.T7A`, `D.T7B`, `D.T7C` | Trial 7's raw records, 4 bytes each: User VIA T2 low and high, result low and high. Time in microseconds is &FFFF minus the 16-bit timer value |

Trial 7 keeps as many records as fit under the MODE 4 screen, at most 2000. The
number is written as `records`.

## Decoding a returned disc

```
python3 tools/hardware-trials/adc/decode_results.py adc-trials.ssd [OUTPUT_DIRPATH]
```

This prints a report: the machine notes, every trial's values, and checks
against the datasheet's predictions where the trial sheet gives them. For each
sweep run it also writes:

- `T7A.csv` and so on: index, 16-bit result, 12-bit result, 10-bit result,
  microseconds;
- `T7A-drops.csv` and so on: the detected drops;
- a straight-line fit of each tooth, also in the report.

## Checking the disc on Beebium

Beebium has no ADC model yet, so the self-test replaces every chip access with
made-up data. It goes through every prompt, file write and menu path, and
produces a sawtooth for sweep A and a straight line for sweep B, so the decoder
has something plausible to report.

```
cd clients/beebium-python-client
uv run python ../../tools/hardware-trials/adc/selftest_beebium.py \
    /path/to/adc-trials.ssd /path/to/selftest-copy.ssd
python3 ../../tools/hardware-trials/adc/decode_results.py /path/to/selftest-copy.ssd
```

The script answers the prompts through two zero-page bytes the programs set
while they wait: `&8F` (1 for a key, 2 for a typed line, 3 for the menu, 4 when
finished) and `&8E` (which question is being asked). These bytes are harmless
on a real machine.
