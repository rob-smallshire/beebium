"""Shared launch/connect plumbing for the example programs.

Every example calls ``run(demo, ...)`` with a function that takes a connected
``Beebium`` client. This keeps each example focused on the one API it
illustrates; the connect-or-launch boilerplate (and the standard
``--port``/``--server``/``--mos`` arguments) lives here once.

    # attach to a server you started yourself
    python examples/<name>.py --port 50071

    # or let the example launch (and stop) its own server
    python examples/<name>.py --server ../../build/src/server/beebium-model-b \
                              --mos ../../roms/acorn-mos_1_20.rom

With no --port the example self-launches; --mos/--basic default to the ROMs in
``<repo>/roms`` when present.
"""

from __future__ import annotations

import argparse
from collections.abc import Callable
from pathlib import Path

from beebium.client import Beebium

_ROMS = Path(__file__).resolve().parents[3] / "roms"


def _default(name: str) -> Path | None:
    candidate = _ROMS / name
    return candidate if candidate.exists() else None


def run(
    demo: Callable[[Beebium], None],
    *,
    description: str,
    extra_args: list[str] | None = None,
) -> None:
    """Parse standard arguments, obtain a connected client, and run ``demo``."""
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        "--port",
        type=int,
        default=None,
        help="Attach to a server already running on this gRPC port instead of self-launching.",
    )
    parser.add_argument(
        "--server",
        type=Path,
        default=None,
        help="beebium-server executable for self-launch (default: $BEEBIUM_SERVER or PATH).",
    )
    parser.add_argument(
        "--mos",
        type=Path,
        default=None,
        help="MOS ROM for self-launch (default: <repo>/roms/acorn-mos_1_20.rom).",
    )
    parser.add_argument(
        "--basic",
        type=Path,
        default=None,
        help="BASIC ROM for self-launch (default: <repo>/roms/bbc-basic_2.rom).",
    )
    args = parser.parse_args()

    if args.port is not None:
        with Beebium.connect(target=f"localhost:{args.port}") as bbc:
            demo(bbc)
        return

    mos = args.mos or _default("acorn-mos_1_20.rom")
    if mos is None:
        parser.error(
            "self-launch needs --mos PATH (no default ROM found under "
            "<repo>/roms); or start a server yourself and pass --port."
        )
    with Beebium.launch(
        mos_filepath=mos,
        basic_filepath=args.basic or _default("bbc-basic_2.rom"),
        server=args.server,
        extra_args=extra_args or [],
    ) as bbc:
        demo(bbc)
