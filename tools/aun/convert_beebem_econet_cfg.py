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

"""Convert a BeebEm Econet.cfg, and optionally its AUNMap, to a Beebium aun-map.json.

BeebEm keeps its AUN peers in two files: Econet.cfg lists hosts as
``net stn ip port`` lines among its mode and timing keywords, and AUNMap holds
RISC OS style ``AddMap a.b.c.0 N`` lines. Beebium reads neither; this is the
one-time conversion to its aun-map.json (docs/discussion/aun-peer-map-file.md,
sections 2.2 and 2.5).

The files are read the way BeebEm reads them (Econet.cpp, ReadEconetConfigFile
and ReadAUNConfigFile): each line is trimmed; empty lines and lines starting
with ``#`` or ``|`` are skipped; everything from the first ``#`` is removed (and
from ``//`` in Econet.cfg); the rest is split on whitespace. Four fields are a
host, which becomes a ``peers`` entry, its trailing ``#`` comment becoming the
label. Two fields are a keyword, reported and not carried over. ``ADDMAP``
with three fields (any case) becomes a ``subnets`` entry: BeebEm keeps the
first three octets of the address, an implicit /24, and masks the net with 255
when Econet.cfg sets MASSAGENETS and 127 when it does not. Every other line is
reported as ignored, with its line number.

Usage::

    uv run convert_beebem_econet_cfg.py Econet.cfg [--aunmap AUNMap]
        [--output aun-map.json] [--merge [--replace]] [--dry-run]

The summary goes to stderr; the JSON goes to --output (default aun-map.json in
the current directory), written atomically, or to stdout with --dry-run.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# Why each BeebEm keyword is not carried over. They describe BeebEm's emulation,
# not the network, and Beebium has no equivalent setting to carry them into.
KEYWORDS = {
    "AUNMODE": "Beebium always speaks AUN over its AUN transport; BeebEm's "
               "pre-AUN raw Econet mode has no counterpart.",
    "LEARN": "Beebium does not add peers from received traffic; peers come from "
             "the map, --aun map= and mDNS discovery.",
    "AUNSTRICT": "The 'station is the last octet' guess is what a subnets entry "
                 "states explicitly, so it is carried by the ADDMAP lines, not "
                 "this switch.",
    "SINGLESOCKET": "A BeebEm socket-management debug option with no meaning "
                    "outside BeebEm.",
    "MASSAGENETS": "Used only to mask the ADDMAP nets as BeebEm does; Beebium "
                   "presents nets to the guest with --aun net= instead.",
    "FLAGFILLTIMEOUT": "BeebEm's own emulation timing; Beebium times the Econet "
                       "handshake itself.",
    "SCACKTIMEOUT": "BeebEm's own emulation timing; Beebium times the Econet "
                    "handshake itself.",
    "TIMEBETWEENBYTES": "BeebEm's own emulation timing; Beebium times the Econet "
                        "handshake itself.",
    "FOURWAYTIMEOUT": "BeebEm's own emulation timing; Beebium times the Econet "
                      "handshake itself.",
}

SAME_HOST_ADDRESSES = {"127.0.0.1", "localhost"}

PEER_KEY_ORDER = ("net", "station", "host", "port", "label")
SUBNET_KEY_ORDER = ("net", "subnet", "label")


@dataclass
class Report:
    """What the conversion found, for the summary on stderr."""

    peers_taken: list[str] = field(default_factory=list)
    subnets_taken: list[str] = field(default_factory=list)
    replaced: list[str] = field(default_factory=list)
    duplicates: list[str] = field(default_factory=list)
    keywords: list[tuple[str, int, str]] = field(default_factory=list)
    unknown_keywords: list[tuple[str, int, str]] = field(default_factory=list)
    ignored: list[tuple[str, int, str]] = field(default_factory=list)
    invalid: list[tuple[str, int, str]] = field(default_factory=list)
    commented_hosts: list[tuple[str, int, str]] = field(default_factory=list)
    same_host: list[str] = field(default_factory=list)
    econet_cfg_addmaps: list[int] = field(default_factory=list)


def _strip_comment(line: str, strip_slashes: bool) -> tuple[str, str | None]:
    """Split off the comment, as BeebEm does, and return (content, # comment)."""
    comment = None
    position = line.find("#")
    if position != -1:
        comment = line[position + 1:].strip() or None
        line = line[:position]
    if strip_slashes:
        position = line.find("//")
        if position != -1:
            line = line[:position]
    return line, comment


def _as_host_line(tokens: list[str]) -> tuple[int, int, str, int] | None:
    """The four fields of a host line, if they parse as BeebEm reads them."""
    if len(tokens) != 4:
        return None
    try:
        return int(tokens[0]), int(tokens[1]), tokens[2], int(tokens[3])
    except ValueError:
        return None


def _peer_problem(net: int, station: int, host: str, port: int) -> str | None:
    """Why a host line cannot be an aun-map.json peer, or None."""
    if not 0 <= net <= 127:
        return f"net {net} is outside 0..127"
    if not 1 <= station <= 254:
        return f"station {station} is outside 1..254"
    if not 1 <= port <= 65535:
        return f"port {port} is outside 1..65535"
    try:
        ipaddress.IPv4Address(host)
    except ValueError:
        return f"{host!r} is not an IPv4 address, which BeebEm requires"
    return None


@dataclass
class Parsed:
    peers: list[dict] = field(default_factory=list)
    subnets: list[dict] = field(default_factory=list)
    massage_nets: bool = False


def _add_subnet(parsed: Parsed, report: Report, where: str, tokens: list[str],
                comment: str | None, inmask: int) -> None:
    try:
        octets = ipaddress.IPv4Address(tokens[1]).packed
        requested_net = int(tokens[2])
    except ValueError:
        report.invalid.append((where, 0, f"ADDMAP {tokens[1]} {tokens[2]}: not an "
                                         "IPv4 address and a net number"))
        return
    net = requested_net & inmask
    subnet = f"{octets[0]}.{octets[1]}.{octets[2]}.0/24"
    entry = {"net": net, "subnet": subnet}
    if comment:
        entry["label"] = comment
    if any(existing["net"] == net for existing in parsed.subnets):
        report.duplicates.append(f"subnet for net {net} ({subnet}, {where}): "
                                 "an earlier ADDMAP already maps that net")
        return
    parsed.subnets.append(entry)


def parse_econet_cfg(text: str, source: str, parsed: Parsed, report: Report,
                     addmap_lines: list[tuple[int, list[str], str | None]]) -> None:
    """Read an Econet.cfg: hosts into ``parsed``, everything else into ``report``."""
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            # A commented-out host line is the shipped sample's whole host
            # list, so a user converting it unedited gets an empty file.
            if _as_host_line(line.lstrip("#").split()) is not None:
                report.commented_hosts.append((source, number, line))
            continue
        if line.startswith("|"):
            continue
        content, comment = _strip_comment(line, strip_slashes=True)
        tokens = content.split()
        if not tokens:
            continue
        where = f"{source} line {number}"
        if len(tokens) == 4:
            fields = _as_host_line(tokens)
            if fields is None:
                report.invalid.append((source, number, f"host line with a non-numeric "
                                                       f"field: {content.strip()}"))
                continue
            net, station, host, port = fields
            problem = _peer_problem(net, station, host, port)
            if problem is not None:
                report.invalid.append((source, number, problem))
                continue
            entry = {"net": net, "station": station, "host": host, "port": port}
            if comment:
                entry["label"] = comment
            if any((p["net"], p["station"]) == (net, station) for p in parsed.peers):
                # BeebEm finds the first matching entry in its table.
                report.duplicates.append(f"peer {net}.{station} ({where}): an earlier "
                                         "line already gives that station")
                continue
            parsed.peers.append(entry)
        elif len(tokens) == 2:
            keyword = tokens[0].upper()
            if keyword in KEYWORDS:
                report.keywords.append((keyword, number, KEYWORDS[keyword]))
                if keyword == "MASSAGENETS":
                    try:
                        parsed.massage_nets = int(tokens[1]) != 0
                    except ValueError:
                        report.invalid.append((source, number,
                                               f"MASSAGENETS {tokens[1]}: not a number"))
            else:
                report.unknown_keywords.append((tokens[0], number, content.strip()))
        elif len(tokens) == 3 and tokens[0].upper() == "ADDMAP":
            # BeebEm reads ADDMAP only from AUNMap; converted all the same,
            # once MASSAGENETS (which may come later) is known.
            addmap_lines.append((number, tokens, comment))
            report.econet_cfg_addmaps.append(number)
        else:
            report.ignored.append((source, number, content.strip()))


def parse_aunmap(text: str, source: str, parsed: Parsed, report: Report,
                 inmask: int) -> None:
    """Read an AUNMap: ADDMAP lines into ``parsed.subnets``."""
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("|"):
            continue
        content, comment = _strip_comment(line, strip_slashes=False)
        tokens = content.split()
        if not tokens:
            continue
        if len(tokens) == 3 and tokens[0].upper() == "ADDMAP":
            _add_subnet(parsed, report, f"{source} line {number}", tokens, comment, inmask)
        else:
            report.ignored.append((source, number, content.strip()))


def convert(econet_cfg_text: str, aunmap_text: str | None = None,
            econet_cfg_name: str = "Econet.cfg",
            aunmap_name: str = "AUNMap") -> tuple[Parsed, Report]:
    """Parse BeebEm's files into peers and subnets, with a report."""
    parsed = Parsed()
    report = Report()
    econet_cfg_addmaps: list[tuple[int, list[str], str | None]] = []
    parse_econet_cfg(econet_cfg_text, econet_cfg_name, parsed, report, econet_cfg_addmaps)
    inmask = 255 if parsed.massage_nets else 127
    for number, tokens, comment in econet_cfg_addmaps:
        _add_subnet(parsed, report, f"{econet_cfg_name} line {number}", tokens, comment, inmask)
    if aunmap_text is not None:
        parse_aunmap(aunmap_text, aunmap_name, parsed, report, inmask)
    for peer in parsed.peers:
        if peer["host"] in SAME_HOST_ADDRESSES:
            report.same_host.append(f"{peer['net']}.{peer['station']} at "
                                    f"{peer['host']}:{peer['port']}")
    return parsed, report


def _ordered(entry: dict, key_order: tuple[str, ...]) -> dict:
    """Known keys in the documented order, then any others as they were."""
    ordered = {key: entry[key] for key in key_order if key in entry}
    ordered.update((key, value) for key, value in entry.items() if key not in ordered)
    return ordered


def merge(existing: dict, parsed: Parsed, replace: bool, report: Report) -> dict:
    """Fold converted entries into an existing map, preserving its order and keys.

    On a duplicate (net, station) peer or a duplicate subnet net, the existing
    entry wins unless ``replace`` is set.
    """
    result = dict(existing)
    peers = list(result.get("peers", []))
    subnets = list(result.get("subnets", []))

    for entry in parsed.peers:
        key = (entry["net"], entry["station"])
        name = f"peer {key[0]}.{key[1]} -> {entry['host']}:{entry['port']}"
        index = next((i for i, p in enumerate(peers)
                      if (p.get("net"), p.get("station")) == key), None)
        if index is None:
            peers.append(entry)
            report.peers_taken.append(name)
        elif replace:
            peers[index] = {**peers[index], **entry}
            report.replaced.append(name)
        else:
            report.duplicates.append(f"{name}: the existing file already has "
                                     f"{key[0]}.{key[1]} (kept; --replace to overwrite)")

    for entry in parsed.subnets:
        name = f"subnet net {entry['net']} -> {entry['subnet']}"
        index = next((i for i, s in enumerate(subnets) if s.get("net") == entry["net"]), None)
        if index is None:
            subnets.append(entry)
            report.subnets_taken.append(name)
        elif replace:
            subnets[index] = {**subnets[index], **entry}
            report.replaced.append(name)
        else:
            report.duplicates.append(f"{name}: the existing file already maps net "
                                     f"{entry['net']} (kept; --replace to overwrite)")

    result["peers"] = [_ordered(p, PEER_KEY_ORDER) for p in peers]
    result["subnets"] = [_ordered(s, SUBNET_KEY_ORDER) for s in subnets]
    return result


def render(document: dict) -> str:
    return json.dumps(document, indent=2) + "\n"


def write_atomically(path: Path, text: str) -> None:
    """Write ``text`` to ``path`` through a temporary file renamed over it."""
    directory = path.parent if str(path.parent) else Path(".")
    descriptor, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=directory)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as stream:
            stream.write(text)
        os.replace(temp_name, path)
    except BaseException:
        Path(temp_name).unlink(missing_ok=True)
        raise


def summarise(report: Report, output: str, dry_run: bool) -> str:
    lines: list[str] = []
    lines.append(f"Peers taken: {len(report.peers_taken)}")
    lines += [f"  {name}" for name in report.peers_taken]
    lines.append(f"Subnets taken: {len(report.subnets_taken)}")
    lines += [f"  {name}" for name in report.subnets_taken]
    if report.replaced:
        lines.append("Replaced in the existing file:")
        lines += [f"  {name}" for name in report.replaced]
    if report.duplicates:
        lines.append("Skipped as duplicates:")
        lines += [f"  {name}" for name in report.duplicates]
    if report.same_host:
        lines.append("Same-host peers (converted as-is; Beebium routes same-host peers "
                     "itself):")
        lines += [f"  {name}" for name in report.same_host]
    if report.keywords:
        lines.append("BeebEm keywords not carried over:")
        lines += [f"  {keyword} (line {number}): {why}"
                  for keyword, number, why in report.keywords]
    if report.unknown_keywords:
        lines.append("Unknown keywords, not carried over (BeebEm reports these as "
                     "errors too):")
        lines += [f"  {keyword} (line {number}): {text}"
                  for keyword, number, text in report.unknown_keywords]
    if report.econet_cfg_addmaps:
        numbers = ", ".join(str(n) for n in report.econet_cfg_addmaps)
        lines.append(f"ADDMAP lines in Econet.cfg (line {numbers}) were converted, but "
                     "BeebEm itself reads ADDMAP only from AUNMap.")
    if report.invalid:
        lines.append("Not converted, invalid:")
        lines += [f"  {source} line {number}: {why}" if number else f"  {source}: {why}"
                  for source, number, why in report.invalid]
    if report.ignored:
        lines.append("Ignored, wrong number of fields (BeebEm ignores these too):")
        lines += [f"  {source} line {number}: {text}"
                  for source, number, text in report.ignored]
    if report.commented_hosts:
        lines.append(f"{len(report.commented_hosts)} commented-out host lines were not "
                     "converted. BeebEm's shipped Econet.cfg lists its example hosts "
                     "only as comments; uncomment the ones you use and convert again:")
        lines += [f"  {source} line {number}: {text}"
                  for source, number, text in report.commented_hosts]
    lines.append("Dry run: nothing written." if dry_run else f"Wrote {output}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Convert a BeebEm Econet.cfg (and AUNMap) to a Beebium aun-map.json.")
    parser.add_argument("econet_cfg", type=Path, help="BeebEm's Econet.cfg")
    parser.add_argument("--aunmap", type=Path, help="BeebEm's AUNMap")
    parser.add_argument("--output", type=Path, default=Path("aun-map.json"),
                        help="the aun-map.json to write (default: ./aun-map.json)")
    parser.add_argument("--merge", action="store_true",
                        help="fold into the existing --output instead of replacing it")
    parser.add_argument("--replace", action="store_true",
                        help="with --merge, converted entries overwrite existing ones")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the result to stdout and write nothing")
    args = parser.parse_args(argv)

    if args.replace and not args.merge:
        parser.error("--replace only applies with --merge")

    try:
        econet_cfg_text = args.econet_cfg.read_text(encoding="latin-1")
        aunmap_text = (args.aunmap.read_text(encoding="latin-1")
                       if args.aunmap is not None else None)
    except OSError as error:
        print(f"convert_beebem_econet_cfg: {error}", file=sys.stderr)
        return 1

    parsed, report = convert(econet_cfg_text, aunmap_text,
                             args.econet_cfg.name,
                             args.aunmap.name if args.aunmap else "AUNMap")

    existing: dict = {}
    if args.merge and args.output.exists():
        try:
            existing = json.loads(args.output.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            print(f"convert_beebem_econet_cfg: cannot merge into {args.output}: {error}",
                  file=sys.stderr)
            return 1
        if not isinstance(existing, dict):
            print(f"convert_beebem_econet_cfg: cannot merge into {args.output}: "
                  "not a JSON object", file=sys.stderr)
            return 1

    document = merge(existing, parsed, args.replace, report)
    text = render(document)
    if args.dry_run:
        sys.stdout.write(text)
    else:
        write_atomically(args.output, text)
    print(summarise(report, str(args.output), args.dry_run), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
