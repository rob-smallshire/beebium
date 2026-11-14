#!/usr/bin/env python3
# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""Run the disc's Z self-test end to end on a Beebium Model B.

Copies the disc, boots it with Shift+Break, answers every prompt through the
zero-page handshake the programs publish (?&8F, ?&8E), chooses Z, then Q, and
ejects the disc so the written results are flushed. The copy is left for
decode_results.py.

Run with the Python client's environment, from the repository root:

    cd clients/beebium-python-client
    uv run python ../../tools/hardware-trials/adc/selftest_beebium.py \
        DISC_FILEPATH OUTPUT_DISC_FILEPATH
"""

import shutil
import sys
from pathlib import Path

from beebium.client import Beebium

WAITING = 0x8F
PROMPT_ID = 0x8E
SELFTEST_DONE = 0x8D
WAIT_COUNT = 0x8C
ANSWERS = {1: "selftest", 2: "selftest", 3: "0", 4: "Y", 6: "1.81"}
BUDGET_SECONDS = 300.0


def screen(bbc):
    try:
        return bbc.video.screen_text().text.strip()
    except Exception as error:  # the log is a convenience only
        return f"(screen unreadable: {error})"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    source_filepath = Path(sys.argv[1]).resolve()
    disc_filepath = Path(sys.argv[2]).resolve()
    shutil.copyfile(source_filepath, disc_filepath)

    with Beebium.launch(preset="model-b-disc") as bbc:
        drive = bbc.disc.drive(0)
        if not drive.is_empty:
            drive.eject(immediate=True)
            drive.wait_for_eject()
        drive.insert(disc_filepath, write_protect=False)
        peek = bbc.memory.address.peek
        bbc.memory.address.bus[WAITING] = 0
        bbc.keyboard.shift_break()

        chose_selftest = False
        prompts = 0
        while True:
            ok = bbc.run_until_or_timeout(lambda: peek[WAITING] in (1, 2, 3, 4),
                                          emulated_seconds=BUDGET_SECONDS)
            if not ok:
                sys.exit(f"Timed out waiting for a prompt. Screen:\n{screen(bbc)}")
            state = peek[WAITING]
            if state == 4:
                print("Menu quit; self-test finished.")
                break
            text = screen(bbc)
            last_line = [line for line in text.splitlines() if line.strip()][-1:] or [""]
            if state == 1:
                reply = " "
            elif state == 2:
                reply = ANSWERS.get(peek[PROMPT_ID], "") + "\r"
            elif peek[SELFTEST_DONE] == 0x5A:
                reply = "Q"
            elif not chose_selftest:
                reply, chose_selftest = "Z", True
            else:
                sys.exit(f"Menu reappeared before the self-test finished. Screen:\n{text}")
            prompts += 1
            print(f"[{prompts:3d}] state {state}: {last_line[0][:70]!r} -> {reply!r}",
                  flush=True)
            count = peek[WAIT_COUNT]
            bbc.debugger.run()
            bbc.keyboard.type(reply)
            # Taken once the program clears the flag, or has already moved on
            # to its next wait (which bumps the counter).
            ok = bbc.run_until_or_timeout(
                lambda: peek[WAITING] == 0 or peek[WAIT_COUNT] != count,
                emulated_seconds=BUDGET_SECONDS)
            if not ok:
                sys.exit(f"The prompt was not taken. Screen:\n{screen(bbc)}")

        bbc.debugger.run()
        drive.eject()
        if not drive.wait_for_eject(timeout=60):
            sys.exit("The disc did not eject.")
    print(f"Results are on {disc_filepath}")


if __name__ == "__main__":
    main()
