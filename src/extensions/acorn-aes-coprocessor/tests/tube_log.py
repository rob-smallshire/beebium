# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.

# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# tube_log.py, reads the counting wrapper's log
# version 2.0.0, 20 November 2026.

"""Reads the counting wrapper's log (TubeLog.hpp) and counts what the ARM really did at the Tube.
Emulator output, never an expected value (USER, 2 Nov 2026; overseer, 31 Oct 2026)."""
import collections

# DOC, SRC Kernel:101-108: eight registers a word apart. Chip numbering, as AesMemory.hpp.
R1DATA, R2DATA, R3DATA, R4DATA = 1, 3, 5, 7
TUBE, LOWER_HALF = 0x1000000, 0x800000  # SRC, Kernel:79; INF, tube.addr_decode.
CALLS = {0: "OSRDCH", 2: "OSCLI", 4: "OSBYTE <&80", 6: "OSBYTE >=&80", 8: "OSWORD", 0x0C: "OSARGS",
         0x0E: "OSBGET", 0x10: "OSBPUT", 0x12: "OSFIND", 0x14: "OSFILE", 0x16: "OSGBPB"}
Row = collections.namedtuple("Row", "tick direction address reg value count")


def read_rows(path):
    """Every row, after checking them against the end line, if the server lived to write it."""
    rows, end = [], None
    with open(path) as log:
        for line in log:
            parts = line.split()
            if parts[0] == "end":
                end = int(parts[1])
                continue
            reg = -1 if parts[3] == "-" else int(parts[3])
            rows.append(Row(int(parts[0]), parts[1], int(parts[2], 16), reg, int(parts[4], 16), int(parts[5])))
    assert end is None or end == sum(row.count for row in rows), f"{end} accesses, rows disagree"
    return rows


def mapping_faults(rows):
    """Rows whose register is not the one the address selects (DOC, HW 5.3: A2-A4), or outside
    the eight registers' repeats. An empty list means the mapping held on every access."""
    def wrong(row):
        if row.reg < 0:
            return row.address & (TUBE | LOWER_HALF) != TUBE | LOWER_HALF
        return (row.address >> 2 & 7) != row.reg or row.address & 3 or row.address & LOWER_HALF
    return [row for row in rows if wrong(row)]


def values(rows, direction, reg):
    return [row.value for row in rows if row.direction == direction and row.reg == reg for _ in range(row.count)]


def transfers(rows):
    """Splits the ARM's R4 and R3 data into transfers, as the Executive reads them (SRC, Kernel
    IRQ4, 962-1000): a byte of &80 or more is an error; else type, id; type 5 stops there; other
    types add four address bytes and a sync byte. Bytes on R3 count to the transfer before them.
    Returns ({type: [count, bytes]}, errors)."""
    seen, errors = collections.defaultdict(lambda: [0, 0]), 0
    needed, current = 0, None
    for row in rows:
        if row.reg == R4DATA and row.direction == "R":
            for _ in range(row.count):
                if needed:
                    needed -= 1
                elif row.value >= 0x80:
                    errors += 1
                else:
                    seen[row.value][0] += 1
                    needed = 1 if row.value == 5 else 6
                    current = row.value
        elif row.reg == R3DATA and current is not None:
            seen[current][1] += row.count
    return dict(sorted(seen.items())), errors


def named_call(call):
    """A call's name, with the sub-number that says what it does (DOC, AN pp.22-25)."""
    name = CALLS[call[0]]
    if call[0] in (4, 6, 0x14, 0x16, 0x0C):
        return f"{name} A=&{call[-1]:02X}"
    if call[0] in (8, 0x12):
        return f"{name} A=&{call[1]:02X}"
    return name


def count(rows, calls_on_r2):
    """Calls on R2 by name, R1 bytes each way, transfers on R4, and accesses reaching no register."""
    stream = values(rows, "W", R2DATA)
    calls, broken = [], None
    try:
        calls = calls_on_r2(stream)
    except (AssertionError, ValueError, IndexError) as error:
        broken = str(error)[:80]
    types, errors = transfers(rows)
    return {"calls": collections.Counter(named_call(call) for call in calls),
            "r2_parse_failed": broken,
            "r1_out": len(values(rows, "W", R1DATA)), "r1_in": len(values(rows, "R", R1DATA)),
            "types": types, "errors": errors,
            "no_register": sum(row.count for row in rows if row.reg < 0),
            "mapping_faults": len(mapping_faults(rows)),
            "accesses": sum(row.count for row in rows)}
