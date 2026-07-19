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

"""Import the emulator's golden-master screens as a recognition corpus.

A development tool, not part of the library or its build. Nothing in the
build or the test suite runs it; the images it produces are committed.

`tests/golden/` holds golden masters the emulator's own tests render and
compare pixel-exactly, covering MODE 0 to MODE 6 in two variants: a testcard
that fills every character cell with a cycling pattern of characters 32 to
126, and the same screen after `VDU 19,0,4,0,0,0` recolours the background.
That is a better recognition corpus than anything captured afresh -- it is
already exhaustive, already deterministic, and already maintained.

The images are copied rather than referenced. The library depends on nothing
outside its own directory, so it cannot reach into the emulator's test data;
and a frozen copy is what a recognition test wants in any case, since the
input should not change underneath it when the renderer is regenerated.

Converted from PPM to PNG only for size: the set is about 5 MB as
uncompressed PPM and a small fraction of that as PNG, with identical pixels.

    python3 import_golden_corpus.py            # from this directory
    python3 import_golden_corpus.py --check    # verify, importing nothing
"""

import argparse
import pathlib
import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - a tool, not a test
    print(
        "Pillow is needed to convert PPM to PNG. Run this under the Python "
        "client's environment:\n"
        "    cd clients/beebium-python-client && uv run --extra imaging "
        "python ../../src/screen-text/tests/fixtures/import_golden_corpus.py",
        file=sys.stderr,
    )
    raise SystemExit(2)

FIXTURES_DIRPATH = pathlib.Path(__file__).resolve().parent
REPO_DIRPATH = FIXTURES_DIRPATH.parents[3]
GOLDEN_DIRPATH = REPO_DIRPATH / "tests" / "golden" / "model-b"

# The two variants worth having, and what each is for.
VARIANTS = {
    "testcard_printable_chars": "testcard",
    "blue_background": "blue",
}

MODES = range(0, 7)


def source_filepath(mode, variant):
    return GOLDEN_DIRPATH / f"mode{mode}" / f"{variant}.ppm"


def target_filepath(mode, label):
    return FIXTURES_DIRPATH / "corpus" / f"mode{mode}-{label}.png"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="report what would be imported without writing anything",
    )
    args = parser.parse_args(argv)

    missing = []
    imported = 0

    for mode in MODES:
        for variant, label in VARIANTS.items():
            source = source_filepath(mode, variant)
            if not source.exists():
                missing.append(source)
                continue

            with Image.open(source) as image:
                target = target_filepath(mode, label)
                print(f"mode{mode} {label:9s} {image.width}x{image.height}"
                      f"  <- {source.relative_to(REPO_DIRPATH)}")
                if not args.check:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    image.save(target, optimize=True)
                    imported += 1

    for source in missing:
        print(f"missing: {source}", file=sys.stderr)

    if not args.check:
        print(f"\nimported {imported} images", file=sys.stderr)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
