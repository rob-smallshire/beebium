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

"""Tests for the BeebEm Econet.cfg / AUNMap to aun-map.json converter (#140).

The fixtures are BeebEm's shipped sample files (BeebEm is GPL, as Beebium is).
"""

from __future__ import annotations

import json
from pathlib import Path

import convert_beebem_econet_cfg as tool
import pytest

FIXTURES_DIRPATH = Path(__file__).parent / "fixtures"


def _convert(econet_cfg: str, aunmap: str | None = None):
    return tool.convert(econet_cfg, aunmap)


def _run(tmp_path: Path, econet_cfg: str, *args: str, aunmap: str | None = None) -> int:
    econet_cfg_filepath = tmp_path / "Econet.cfg"
    econet_cfg_filepath.write_text(econet_cfg)
    argv = [str(econet_cfg_filepath), *args]
    if aunmap is not None:
        aunmap_filepath = tmp_path / "AUNMap"
        aunmap_filepath.write_text(aunmap)
        argv += ["--aunmap", str(aunmap_filepath)]
    return tool.main(argv)


# ---- The shipped samples ----


def test_shipped_samples_convert_to_subnets_and_report_everything_else(tmp_path, capsys):
    output_filepath = tmp_path / "aun-map.json"
    status = tool.main([str(FIXTURES_DIRPATH / "Econet.cfg"),
                        "--aunmap", str(FIXTURES_DIRPATH / "AUNMap"),
                        "--output", str(output_filepath)])
    assert status == 0
    document = json.loads(output_filepath.read_text())
    # The sample's hosts are all commented out, so there are no peers. Its
    # MASSAGENETS 1 means BeebEm masks ADDMAP nets with 255: kept as written.
    assert document == {
        "peers": [],
        "subnets": [
            {"net": 128, "subnet": "192.168.0.0/24"},
            {"net": 132, "subnet": "192.168.12.0/24"},
            {"net": 133, "subnet": "192.168.13.0/24"},
        ],
    }
    summary = capsys.readouterr().err
    for keyword in ("AUNMODE", "LEARN", "AUNSTRICT", "SINGLESOCKET", "MASSAGENETS",
                    "FLAGFILLTIMEOUT", "SCACKTIMEOUT", "TIMEBETWEENBYTES", "FOURWAYTIMEOUT"):
        assert f"  {keyword} (line " in summary
    # A first-time user would otherwise get an empty peers list and no reason.
    assert "12 commented-out host lines were not converted" in summary
    assert "# 0 254 127.0.0.1 32768" in summary


def test_the_aunmap_commented_out_addmap_is_not_converted():
    _, aunmap_report = _convert("", (FIXTURES_DIRPATH / "AUNMap").read_text())
    parsed, _ = _convert("MASSAGENETS 1\n", (FIXTURES_DIRPATH / "AUNMap").read_text())
    # "|AddMap 1.0.128.0 128" is a | comment and never reaches the table.
    assert all(s["subnet"] != "1.0.128.0/24" for s in parsed.subnets)
    assert aunmap_report.ignored == []


# ---- Host lines ----


def test_host_lines_become_peers_with_their_comment_as_label():
    parsed, report = _convert(
        "0 254 192.168.1.10 32768  # PiEconetBridge file server\n"
        "0 40 192.168.1.40 32768\n")
    assert parsed.peers == [
        {"net": 0, "station": 254, "host": "192.168.1.10", "port": 32768,
         "label": "PiEconetBridge file server"},
        {"net": 0, "station": 40, "host": "192.168.1.40", "port": 32768},
    ]
    assert report.same_host == []


def test_same_host_peers_are_converted_and_flagged():
    parsed, report = _convert("0 101 127.0.0.1 10101\n")
    assert parsed.peers == [{"net": 0, "station": 101, "host": "127.0.0.1", "port": 10101}]
    assert report.same_host == ["0.101 at 127.0.0.1:10101"]


def test_pipe_comment_lines_are_skipped():
    parsed, report = _convert("| a pipe comment with four words\n0 1 10.0.0.1 32768\n")
    assert [p["station"] for p in parsed.peers] == [1]
    assert report.ignored == [] and report.unknown_keywords == []


def test_double_slash_comments_are_stripped_in_econet_cfg():
    # BeebEm 4.19 and earlier shipped Econet.cfg with // comments on some lines.
    parsed, report = _convert("0 2 10.0.0.2 32768 // laptop\nLEARN 1 // learn mode\n")
    assert parsed.peers == [{"net": 0, "station": 2, "host": "10.0.0.2", "port": 32768}]
    assert [k for k, _, _ in report.keywords] == ["LEARN"]


def test_lines_with_the_wrong_field_count_are_ignored_with_their_numbers():
    parsed, report = _convert(
        "0 1 10.0.0.1\n"              # three fields
        "0 2 10.0.0.2 32768 extra\n"  # five fields
        "LONELY\n"                    # one field
        "0 3 10.0.0.3 32768\n")
    assert [p["station"] for p in parsed.peers] == [3]
    assert [(n, text) for _, n, text in report.ignored] == [
        (1, "0 1 10.0.0.1"), (2, "0 2 10.0.0.2 32768 extra"), (3, "LONELY")]


def test_unknown_keywords_are_listed_as_unknown():
    _, report = _convert("AUNMODE 1\nFROBNICATE 7\n")
    assert [k for k, _, _ in report.keywords] == ["AUNMODE"]
    assert [(k, n) for k, n, _ in report.unknown_keywords] == [("FROBNICATE", 2)]


def test_keywords_are_case_insensitive():
    _, report = _convert("aunmode 1\nLearn 0\n")
    assert [k for k, _, _ in report.keywords] == ["AUNMODE", "LEARN"]


def test_out_of_range_and_non_ipv4_hosts_are_reported_not_converted():
    parsed, report = _convert(
        "200 1 10.0.0.1 32768\n"
        "0 255 10.0.0.1 32768\n"
        "0 1 example.org 32768\n"
        "0 1 10.0.0.1 70000\n")
    assert parsed.peers == []
    assert len(report.invalid) == 4


def test_a_station_repeated_in_one_file_keeps_the_first_as_beebem_does():
    parsed, report = _convert("0 1 10.0.0.1 32768\n0 1 10.0.0.9 32768\n")
    assert parsed.peers == [{"net": 0, "station": 1, "host": "10.0.0.1", "port": 32768}]
    assert len(report.duplicates) == 1


# ---- ADDMAP ----


def test_addmap_keeps_three_octets_and_masks_the_net_with_127_by_default():
    # MASSAGENETS defaults to off in BeebEm, so the net is masked with 127.
    parsed, _ = _convert("", "AddMap 192.168.7.99 129\naddmap 10.1.2.0 5\n")
    assert parsed.subnets == [{"net": 1, "subnet": "192.168.7.0/24"},
                              {"net": 5, "subnet": "10.1.2.0/24"}]


def test_addmap_net_is_kept_whole_when_massagenets_is_on():
    parsed, _ = _convert("MASSAGENETS 1\n", "ADDMAP 192.168.7.0 129\n")
    assert parsed.subnets == [{"net": 129, "subnet": "192.168.7.0/24"}]


def test_addmap_in_econet_cfg_is_converted_and_noted():
    # Masked once MASSAGENETS is known, even though it comes after the ADDMAP.
    parsed, report = _convert("ADDMAP 10.0.4.0 132\nMASSAGENETS 1\n")
    assert parsed.subnets == [{"net": 132, "subnet": "10.0.4.0/24"}]
    assert report.econet_cfg_addmaps == [1]


def test_aunmap_lines_that_are_not_addmap_are_ignored():
    _, report = _convert("", "AddMap 10.0.0.0\nRoute 10.0.0.0 128\n")
    assert [n for _, n, _ in report.ignored] == [1, 2]


# ---- Writing, merging, dry run ----


def test_output_is_pretty_printed_in_a_stable_key_order(tmp_path):
    output_filepath = tmp_path / "aun-map.json"
    assert _run(tmp_path, "0 9 10.0.0.9 32768 # nine\n", "--output", str(output_filepath)) == 0
    text = output_filepath.read_text()
    assert text == (
        '{\n'
        '  "peers": [\n'
        '    {\n'
        '      "net": 0,\n'
        '      "station": 9,\n'
        '      "host": "10.0.0.9",\n'
        '      "port": 32768,\n'
        '      "label": "nine"\n'
        '    }\n'
        '  ],\n'
        '  "subnets": []\n'
        '}\n')
    # Written through a temporary file renamed over the output: none is left.
    assert {p.name for p in tmp_path.iterdir()} == {"Econet.cfg", "aun-map.json"}


def _existing_map(path: Path) -> None:
    path.write_text(json.dumps({
        "peers": [{"label": "kept", "net": 0, "station": 1, "host": "10.9.9.1",
                   "port": 32768, "x-future": True}],
        "subnets": [{"net": 128, "subnet": "10.9.9.0/24"}],
        "x-top-level": "preserved",
    }))


def test_merge_keeps_existing_entries_on_a_duplicate(tmp_path, capsys):
    output_filepath = tmp_path / "aun-map.json"
    _existing_map(output_filepath)
    assert _run(tmp_path, "0 1 10.0.0.1 32768\n0 2 10.0.0.2 32768\nMASSAGENETS 1\n",
                "--output", str(output_filepath), "--merge",
                aunmap="AddMap 10.0.0.0 128\n") == 0
    document = json.loads(output_filepath.read_text())
    assert document["x-top-level"] == "preserved"
    assert document["peers"] == [
        {"net": 0, "station": 1, "host": "10.9.9.1", "port": 32768, "label": "kept",
         "x-future": True},
        {"net": 0, "station": 2, "host": "10.0.0.2", "port": 32768},
    ]
    assert document["subnets"] == [{"net": 128, "subnet": "10.9.9.0/24"}]
    summary = capsys.readouterr().err
    assert "Skipped as duplicates:" in summary
    assert "peer 0.1 -> 10.0.0.1:32768" in summary


def test_merge_with_replace_overwrites_duplicates_and_keeps_unknown_keys(tmp_path, capsys):
    output_filepath = tmp_path / "aun-map.json"
    _existing_map(output_filepath)
    assert _run(tmp_path, "0 1 10.0.0.1 32768\nMASSAGENETS 1\n",
                "--output", str(output_filepath), "--merge", "--replace",
                aunmap="AddMap 10.0.0.0 128\n") == 0
    document = json.loads(output_filepath.read_text())
    assert document["peers"] == [
        {"net": 0, "station": 1, "host": "10.0.0.1", "port": 32768, "label": "kept",
         "x-future": True},
    ]
    assert document["subnets"] == [{"net": 128, "subnet": "10.0.0.0/24"}]
    assert "Replaced in the existing file:" in capsys.readouterr().err


def test_replace_requires_merge(tmp_path):
    with pytest.raises(SystemExit):
        _run(tmp_path, "", "--replace")


def test_dry_run_writes_nothing(tmp_path, capsys):
    output_filepath = tmp_path / "aun-map.json"
    _existing_map(output_filepath)
    before = output_filepath.read_text()
    assert _run(tmp_path, "0 5 10.0.0.5 32768\n", "--output", str(output_filepath),
                "--merge", "--dry-run") == 0
    assert output_filepath.read_text() == before
    assert {p.name for p in tmp_path.iterdir()} == {"Econet.cfg", "aun-map.json"}
    captured = capsys.readouterr()
    assert json.loads(captured.out)["peers"][-1]["station"] == 5
    assert "Dry run: nothing written." in captured.err


def test_a_malformed_existing_file_is_not_overwritten(tmp_path, capsys):
    output_filepath = tmp_path / "aun-map.json"
    output_filepath.write_text("{ not json")
    assert _run(tmp_path, "0 5 10.0.0.5 32768\n", "--output", str(output_filepath),
                "--merge") == 1
    assert output_filepath.read_text() == "{ not json"
