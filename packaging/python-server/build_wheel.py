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

"""Repackage a release bundle archive into a platform wheel.

    build_wheel.py <bundle-archive> <platform-tag>

Unpacks the bundle (a release .tar.gz or Windows .zip) verbatim into
``src/beebium/server/_bundle/`` -- stripping only its single top-level directory
so ``_bundle/`` is the ``bin/ lib/ share/`` root -- then runs ``uv build
--wheel`` with BEEBIUM_WHEEL_TAG set, so hatchling emits
``beebium_server-<ver>-py3-none-<platform-tag>.whl`` directly (see hatch_build.py).

The server bytes are therefore byte-identical to the ones already on the GitHub
Release, and the wheel is just a repackaging -- no compile. ``_bundle/`` is
gitignored and exists only at build time.

Example:
    build_wheel.py beebium-server-0.1.3-linux-arm64.tar.gz manylinux_2_36_aarch64
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUNDLE_DIRPATH = HERE / "src" / "beebium" / "server" / "_bundle"
DIST_DIRPATH = HERE / "dist"


def _extract_bundle(archive_filepath: Path, bundle_dirpath: Path) -> None:
    """Extract the archive into bundle_dirpath, stripping its top-level dir."""
    if bundle_dirpath.exists():
        shutil.rmtree(bundle_dirpath)
    tmp_dirpath = bundle_dirpath.parent / "_bundle_extract_tmp"
    if tmp_dirpath.exists():
        shutil.rmtree(tmp_dirpath)
    tmp_dirpath.mkdir(parents=True)

    name = archive_filepath.name
    if name.endswith((".tar.gz", ".tgz")):
        with tarfile.open(archive_filepath, "r:gz") as archive:
            try:
                archive.extractall(tmp_dirpath, filter="data")  # py3.12+
            except TypeError:
                archive.extractall(tmp_dirpath)  # older Python: our own trusted archive
    elif name.endswith(".zip"):
        with zipfile.ZipFile(archive_filepath) as archive:
            archive.extractall(tmp_dirpath)
    else:
        raise SystemExit(f"unsupported bundle archive (expected .tar.gz or .zip): {archive_filepath}")

    entries = list(tmp_dirpath.iterdir())
    if len(entries) == 1 and entries[0].is_dir():
        # The bundles unpack to a single top-level directory; make its contents
        # the bundle root so _bundle/ is directly the bin/ lib/ share/ tree.
        shutil.move(str(entries[0]), str(bundle_dirpath))
        shutil.rmtree(tmp_dirpath, ignore_errors=True)
    else:
        shutil.move(str(tmp_dirpath), str(bundle_dirpath))


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        sys.stderr.write(f"usage: {Path(argv[0]).name} <bundle-archive> <platform-tag>\n")
        return 2
    archive_filepath = Path(argv[1]).resolve()
    platform_tag = argv[2]
    if not archive_filepath.is_file():
        raise SystemExit(f"bundle archive not found: {archive_filepath}")

    print(f"==> Unpacking {archive_filepath.name} into {BUNDLE_DIRPATH}")
    _extract_bundle(archive_filepath, BUNDLE_DIRPATH)
    server = BUNDLE_DIRPATH / "bin" / "beebium-model-b"
    if not (server.exists() or (BUNDLE_DIRPATH / "bin" / "beebium-model-b.exe").exists()):
        raise SystemExit(f"bundle looks wrong: no bin/beebium-model-b[.exe] under {BUNDLE_DIRPATH}")

    print(f"==> Building wheel (tag py3-none-{platform_tag})")
    env = os.environ.copy()
    env["BEEBIUM_WHEEL_TAG"] = platform_tag
    subprocess.run(
        ["uv", "build", "--wheel", "--out-dir", str(DIST_DIRPATH)],
        cwd=HERE,
        env=env,
        check=True,
    )

    built = sorted(DIST_DIRPATH.glob(f"*-py3-none-{platform_tag}.whl"))
    if not built:
        raise SystemExit(f"no wheel matching *-py3-none-{platform_tag}.whl was produced in {DIST_DIRPATH}")
    wheel = built[-1]
    print(f"==> Built {wheel}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
