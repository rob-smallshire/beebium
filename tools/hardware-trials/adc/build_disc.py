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

"""Build adc-trials.ssd, the bootable uPD7002 analogue port trial disc.

Each program (MENU.bas, T1.bas ... T7.bas) has COMMON.bas appended, is
tokenised with oaknut-basic and put on a fresh DFS image with oaknut-disc,
alongside a !BOOT that chains MENU, with the boot option set to EXEC.

The sources are written for the Model B, whose uPD7002 is at &FEC0-&FEDF.
With --machine master the build rewrites them for the Master 128, whose
uPD7002 is at &FE18-&FE1F (its four registers repeat once, at &FE1C), and
writes adc-trials-master.ssd. Nothing else differs: MOS 3.20 keeps the ADC
workspace at the same addresses as MOS 1.20 (&24C current channel, &24D
maximum channel) and its OSBYTE &10 behaves the same.

Usage: build_disc.py [--machine model-b|master] [OUTPUT_DIRPATH]
       (default: model-b, and the current directory)

Needs only the Python standard library and uvx on the PATH.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE_DIRPATH = Path(__file__).resolve().parent
PROGRAM_NAMES = ["MENU", "T1", "T2", "T3", "T4", "T5", "T6", "T7"]
COMMON_FILENAME = "COMMON.bas"
BOOT_FILENAME = "BOOT.txt"

# Per machine: the image name, the disc title, and the rewrites applied to
# each program's source before tokenising, in order. After rewriting, no
# Model B ADC address may remain.
MACHINES = {
    "model-b": {
        "ssd_filename": "adc-trials.ssd",
        "title": "ADC TRIALS",
        "rewrites": {},
    },
    "master": {
        "ssd_filename": "adc-trials-master.ssd",
        "title": "ADC MASTER",
        "rewrites": {
            "MENU": [
                ('PRINT "uPD7002 analogue port trials"',
                 'PRINT "uPD7002 analogue port trials: Master"'),
                # OSBYTE 0 with X=1 returns the MOS version: 3 on a Master 128.
                ("50 MODE 7\n",
                 "50 MODE 7\n"
                 "52 A%=0:X%=1:V%=((USR&FFF4)AND&FF00)DIV256\n"
                 '54 IF V%<>3 THEN PRINT "This disc is for the Master 128 (MOS 3),"\'"but this machine reports MOS type ";V%;"."\':PROCkey("Press SPACE to carry on anyway.")\n'),
                ('1710 PROCw("minutes_on",c$):PROCw("joystick",j$)',
                 '1710 PROCw("minutes_on",c$):PROCw("joystick",j$):PROCw("disc","master")'),
            ],
            "T2": [
                ("M1%=?&FEC4:M2%=?&FEC8:M3%=?&FED0:M4%=?&FEDC:M5%=?&FEC6",
                 "M1%=?&FE1C:M2%=?&FE1D:M3%=?&FE1E:M4%=?&FE1F:M5%=?&FE1E"),
                ('"_mirror_FEC4"', '"_mirror_FE1C"'),
                ('"_mirror_FEC8"', '"_mirror_FE1D"'),
                ('"_mirror_FED0"', '"_mirror_FE1E"'),
                ('"_mirror_FEDC"', '"_mirror_FE1F"'),
                ('"_mirror_FEC6_low"', '"_mirror_FE1E_low"'),
            ],
            "*": [
                ("&FEC0", "&FE18"),
                ("&FEC1", "&FE19"),
                ("&FEC2", "&FE1A"),
                ("&FEC3", "&FE1B"),
            ],
        },
    },
}
# 12.10.0 is the first release that writes the DFS catalogue in descending
# start-sector order; with earlier ones a *SAVE on the BBC overwrites a file.
OAKNUT_DISC = ["uvx", "--from", "oaknut-disc>=12.10.0", "oaknut-disc"]
OAKNUT_BASIC = ["uvx", "oaknut-basic"]
LOAD_ADDRESS = "0x1900"
EXEC_ADDRESS = "0x8023"


def run(args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(
            f"Command failed: {' '.join(args)}\n{result.stdout}{result.stderr}"
        )
    return result.stdout


def read_ascii(filepath):
    data = filepath.read_bytes()
    bad = [i for i, b in enumerate(data) if b > 0x7E or (b < 0x20 and b not in (0x0A, 0x0D))]
    if bad:
        sys.exit(f"{filepath.name} is not 7-bit printable ASCII (byte offset {bad[0]}).")
    return data.decode("ascii")


def line_numbers(text):
    numbers = []
    for line in text.splitlines():
        if line.strip():
            numbers.append(int(line.split(None, 1)[0]))
    return numbers


def rewrite(program_name, text, rewrites):
    for old, new in rewrites.get(program_name, []) + rewrites.get("*", []):
        if old not in text and program_name in rewrites and (old, new) in rewrites[program_name]:
            sys.exit(f"{program_name}.bas no longer contains {old!r}; update the rewrites.")
        text = text.replace(old, new)
    if rewrites:
        for line in text.splitlines():
            code = line.split("REM", 1)[0]
            if "&FEC" in code or "&FED" in code:
                sys.exit(f"{program_name}.bas still uses a Model B ADC address: {line}")
    return text


def combined_source(program_name, common_text, rewrites):
    text = rewrite(program_name, read_ascii(SOURCE_DIRPATH / f"{program_name}.bas"), rewrites)
    program_numbers = line_numbers(text)
    common_numbers = line_numbers(common_text)
    if max(program_numbers) >= min(common_numbers):
        sys.exit(
            f"{program_name}.bas uses line {max(program_numbers)}, which clashes "
            f"with {COMMON_FILENAME} starting at {min(common_numbers)}."
        )
    return text.rstrip("\n") + "\n" + common_text


def build(output_dirpath, machine="model-b"):
    if shutil.which("uvx") is None:
        sys.exit("uvx was not found on the PATH; install uv first.")
    config = MACHINES[machine]
    output_dirpath.mkdir(parents=True, exist_ok=True)
    ssd_filepath = output_dirpath / config["ssd_filename"]
    if ssd_filepath.exists():
        ssd_filepath.unlink()
    common_text = read_ascii(SOURCE_DIRPATH / COMMON_FILENAME)

    with tempfile.TemporaryDirectory() as work_dirname:
        work_dirpath = Path(work_dirname)
        run([*OAKNUT_DISC, "create", str(ssd_filepath), "--title", config["title"]])
        for name in PROGRAM_NAMES:
            source_filepath = work_dirpath / f"{name}.bas"
            source_filepath.write_text(combined_source(name, common_text, config["rewrites"]), encoding="ascii")
            tokenised_filepath = work_dirpath / f"{name}.tok"
            run([*OAKNUT_BASIC, "tokenise", str(source_filepath), str(tokenised_filepath)])
            size = tokenised_filepath.stat().st_size
            print(f"{name:5} {size:6d} bytes tokenised (TOP = &{0x1900 + size:04X} at PAGE &1900)")
            run([
                *OAKNUT_DISC, "put",
                "--load", LOAD_ADDRESS, "--exec", EXEC_ADDRESS,
                f"{ssd_filepath}:$.{name}", str(tokenised_filepath),
            ])
        boot_text = read_ascii(SOURCE_DIRPATH / BOOT_FILENAME)
        boot_filepath = work_dirpath / "BOOT.bin"
        boot_filepath.write_bytes(boot_text.replace("\r\n", "\n").replace("\n", "\r").encode("ascii"))
        run([*OAKNUT_DISC, "put", "--meta-format", "none",
             f"{ssd_filepath}:$.!BOOT", str(boot_filepath)])
        run([*OAKNUT_DISC, "opt", str(ssd_filepath), "3"])
    print(f"Built {ssd_filepath}")
    return ssd_filepath


def main():
    args = sys.argv[1:]
    machine = "model-b"
    if args[:1] == ["--machine"]:
        if len(args) < 2 or args[1] not in MACHINES:
            sys.exit(f"--machine takes one of: {', '.join(MACHINES)}")
        machine = args[1]
        args = args[2:]
    output_dirpath = Path(args[0]) if args else Path.cwd()
    build(output_dirpath, machine)


if __name__ == "__main__":
    main()
