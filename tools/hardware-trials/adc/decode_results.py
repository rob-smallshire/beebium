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

"""Decode a returned adc-trials.ssd into a readable report and CSV files.

Usage: decode_results.py DISC_FILEPATH [OUTPUT_DIRPATH]

Reads M.INFO and the result files R.T1 ... R.T7 (key=value text) and, for
trial 7, the raw records D.T7A, D.T7B and D.T7C. Prints a report and writes
one CSV of (result, microseconds) per sweep run, plus the detected drops,
to OUTPUT_DIRPATH (default: <disc name>-results next to the disc).

Needs only the Python standard library and uvx on the PATH.
"""

import csv
import subprocess
import sys
from pathlib import Path

TRIAL_TITLES = {
    1: "Conversion time against input",
    2: "What the registers read",
    3: "The end-of-conversion line into the System VIA",
    4: "What an empty port reads through ADVAL",
    5: "With a joystick, through ADVAL",
    6: "The reference voltage",
    7: "The sweep: conversion time against result",
}
SWEEP_RUNS = {"A": "control &08, full sweep", "B": "control &00, full sweep",
              "C": "control &08, middle only"}
DROP_THRESHOLD_US = 300
BIN_WIDTH_CODES = 16  # 12-bit codes, the same as the program's 4 ADVAL units


def read_file(disc_filepath, name):
    """Return the bytes of a file on the disc, or None if it is absent."""
    result = subprocess.run(
        ["uvx", "--from", "oaknut-disc>=12.10.0", "oaknut-disc", "cat", f"{disc_filepath}:{name}"],
        capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout


def parse_keys(data):
    values = {}
    order = []
    for raw_line in data.split(b"\r"):
        line = raw_line.decode("ascii", "replace").strip("\n")
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
        order.append(key)
    return values, order


def hex_value(text):
    text = text.strip()
    return int(text[1:], 16) if text.startswith("&") else int(text)


def decode_records(data):
    records = []
    for offset in range(0, len(data) - 3, 4):
        low, high, result_low, result_high = data[offset:offset + 4]
        microseconds = 0xFFFF - (low + 256 * high)
        result16 = result_low + 256 * result_high
        records.append((result16, microseconds))
    return records


def find_drops(records):
    """Minimum time per bin of result; a fall of more than the threshold
    from one occupied bin to the next is a drop (the program's own rule)."""
    bins = {}
    for result16, microseconds in records:
        index = (result16 >> 4) // BIN_WIDTH_CODES
        bins[index] = min(bins.get(index, microseconds), microseconds)
    drops = []
    previous = None
    for index in sorted(bins):
        current = bins[index]
        if previous is not None and previous - current > DROP_THRESHOLD_US:
            drops.append((index * BIN_WIDTH_CODES, previous, current))
        previous = current
    return drops


def fit_line(points):
    n = len(points)
    if n < 2:
        return None
    mean_x = sum(x for x, _ in points) / n
    mean_y = sum(y for _, y in points) / n
    sxx = sum((x - mean_x) ** 2 for x, _ in points)
    if sxx == 0:
        return None
    slope = sum((x - mean_x) * (y - mean_y) for x, y in points) / sxx
    return slope, mean_y - slope * mean_x


def tooth_fits(records, drops):
    """Fit time against 12-bit result between drops, discarding points that
    belong to the neighbouring tooth (more than 300 us off the first fit)."""
    edges = [0] + [code for code, _, _ in drops] + [4096]
    fits = []
    for start, end in zip(edges, edges[1:]):
        points = [(r >> 4, t) for r, t in records if start <= (r >> 4) < end]
        fit = fit_line(points)
        if fit is None:
            continue
        slope, intercept = fit
        kept = [(x, y) for x, y in points if abs(y - (slope * x + intercept)) < DROP_THRESHOLD_US]
        fit = fit_line(kept) or fit
        slope, intercept = fit
        fits.append((start, end, len(kept), len(points) - len(kept), slope,
                     slope * start + intercept))
    return fits


def print_keys(values, order, skip=("selftest", "trial")):
    for key in order:
        if key not in skip:
            print(f"  {key} = {values[key]}")


def report_trial_1(values):
    rows = {}
    for key, value in values.items():
        if key.endswith("_mean_ms"):
            prefix = key[: -len("_mean_ms")]
            condition, control = prefix.rsplit("_", 1)
            # The exact figure: TIME's centiseconds for 100 conversions.
            total_cs = values.get(prefix + "_total_cs_for_100")
            if total_cs is not None:
                value = f"{int(total_cs) / 10:.1f}"
            rows.setdefault(condition, {})[control] = value
    print("  mean conversion time, ms (control &08 = 12-bit, &00 = 8-bit, &09 = channel 1 12-bit)")
    for condition, controls in rows.items():
        cells = ", ".join(f"&{c}: {v}" for c, v in sorted(controls.items()))
        print(f"    {condition:10} {cells}")


def report_trial_2(values, order):
    print_keys(values, order)
    for run in ("run1", "run2", "run3_left", "run4_right"):
        if f"{run}_low" not in values:
            continue
        low = hex_value(values[f"{run}_low"])
        checks = [
            f"low nibble zero: {'yes' if low & 0x0F == 0 else 'NO'}",
            f"fourth address = low: {'yes' if hex_value(values[f'{run}_fourth_address']) == low else 'NO'}",
            f"bit 7 clear after low read: {'yes' if hex_value(values[f'{run}_status_after_low_read']) & 0x80 == 0 else 'NO'}",
            f"bit 7 set after high read: {'yes' if hex_value(values[f'{run}_status_after_high_read']) & 0x80 else 'NO'}",
        ]
        print(f"  {run}: " + "; ".join(checks))
    if "run2_status_after_write" in values:
        echoed = hex_value(values["run2_status_after_write"]) & 0x04
        print(f"  flag bit 2 echoed with control &0E: {'yes' if echoed else 'no'}")
    if "run4_right_status_after_write" in values:
        bits = hex_value(values["run4_right_status_after_write"]) & 0x30
        meaning = ("holds the previous result's top bits" if bits == 0x30
                   else "clears them" if bits == 0 else "neither (unexpected)")
        print(f"  status bits 5-4 just after write, left then right: &{bits:02X}: the chip {meaning}")


def report_trial_3(values, order):
    print_keys(values, order)
    after_read = values.get("pos_flag_after_high_read")
    after_write = values.get("pos_flag_after_new_write")
    if after_read is not None and after_write is not None:
        if hex_value(after_read) and not hex_value(after_write):
            verdict = "the high-byte read releases the end-of-conversion line (datasheet)"
        elif hex_value(after_write) and not hex_value(after_read):
            verdict = "only the next write releases the line"
        else:
            verdict = "neither pattern; look at the raw flags"
        print(f"  verdict: {verdict}")


def report_trial_7(disc_filepath, values, order, output_dirpath):
    print_keys(values, order)
    for run in values.get("runs", ""):
        print(f"\n  Run {run} ({SWEEP_RUNS.get(run, '?')})")
        data = read_file(disc_filepath, f"R.T7{run}")
        if data is not None:
            run_values, run_order = parse_keys(data)
            print_keys(run_values, run_order)
        records_data = read_file(disc_filepath, f"D.T7{run}")
        if records_data is None:
            print("    no raw records on the disc")
            continue
        records = decode_records(records_data)
        csv_filepath = output_dirpath / f"T7{run}.csv"
        with csv_filepath.open("w", newline="") as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(["index", "result16", "result12", "result10", "microseconds"])
            for index, (result16, microseconds) in enumerate(records):
                writer.writerow([index, result16, result16 >> 4, result16 >> 6, microseconds])
        drops = find_drops(records)
        drops_filepath = output_dirpath / f"T7{run}-drops.csv"
        with drops_filepath.open("w", newline="") as drops_file:
            writer = csv.writer(drops_file)
            writer.writerow(["result12", "microseconds_before", "microseconds_after", "drop_us"])
            for code, before, after in drops:
                writer.writerow([code, before, after, before - after])
        print(f"    {len(records)} records -> {csv_filepath}")
        print(f"    drops of more than {DROP_THRESHOLD_US} us (12-bit result, before -> after):")
        for code, before, after in drops:
            print(f"      at {code:4d}: {before} -> {after} us (drop {before - after})")
        if not drops:
            print("      none")
        print("    straight-line fits between drops (12-bit result):")
        for start, end, kept, rejected, slope, at_start in tooth_fits(records, drops):
            print(f"      {start:4d}-{end:4d}: {kept} points ({rejected} off-line), "
                  f"{slope:.3f} us per code, {at_start:.0f} us at {start}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    disc_filepath = Path(sys.argv[1]).resolve()
    if not disc_filepath.exists():
        sys.exit(f"{disc_filepath} does not exist.")
    output_dirpath = (Path(sys.argv[2]) if len(sys.argv) > 2
                      else disc_filepath.with_name(disc_filepath.stem + "-results"))
    output_dirpath.mkdir(parents=True, exist_ok=True)

    print(f"uPD7002 analogue port trials: results from {disc_filepath.name}")
    info = read_file(disc_filepath, "M.INFO")
    if info is not None:
        print("\nMachine notes")
        values, order = parse_keys(info)
        print_keys(values, order, skip=())
    any_selftest = False
    for number, title in TRIAL_TITLES.items():
        print(f"\nTrial {number}. {title}")
        data = read_file(disc_filepath, f"R.T{number}")
        if data is None:
            print("  not run")
            continue
        values, order = parse_keys(data)
        if values.get("selftest") == "1":
            any_selftest = True
            print("  ** SELF-TEST: made-up data, not a hardware measurement **")
        if number == 1:
            report_trial_1(values)
        elif number == 2:
            report_trial_2(values, order)
        elif number == 3:
            report_trial_3(values, order)
        elif number == 7:
            report_trial_7(disc_filepath, values, order, output_dirpath)
        else:
            print_keys(values, order)
    if any_selftest:
        print("\nThis disc holds SELF-TEST results (marked above); they are not measurements.")
    print(f"\nCSV files are in {output_dirpath}")


if __name__ == "__main__":
    main()
