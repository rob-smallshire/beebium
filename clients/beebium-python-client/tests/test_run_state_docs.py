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

"""The run-state table in the README says what the docstrings say (#191).

Every client method that needs the machine running or stopped, or leaves it
one way, carries two docstring lines, ``Requires: ...`` and ``Leaves the
machine: ...``. The README's run-state table collects them. This test reads
both and requires them to agree, so neither can drift from the other.
"""

from __future__ import annotations

import importlib
import inspect
import pkgutil
import re
from pathlib import Path

import beebium.client

TEMPLATE_FILEPATH = Path(__file__).parents[1] / "readme" / "README.md.j2"
REQUIRES = re.compile(r"^\s*Requires: (.+)\.$", re.M)
LEAVES = re.compile(r"^\s*Leaves the machine: (.+)\.$", re.M)
COPROCESSOR_REQUIRES = re.compile(r"^\s*Coprocessor requires: (.+)\.$", re.M)
COPROCESSOR_LEAVES = re.compile(r"^\s*Leaves the coprocessor: (.+)\.$", re.M)
ROW = re.compile(r"^\| `(\w+)\.(\w+)` \| (.+) \| (.+) \|$")


def _documented(
    requires_pattern: re.Pattern[str] = REQUIRES, leaves_pattern: re.Pattern[str] = LEAVES
) -> dict[str, tuple[str, str]]:
    """Every public method's run-state lines, by Class.method."""
    found: dict[str, tuple[str, str]] = {}
    for module_info in pkgutil.iter_modules(beebium.client.__path__):
        if module_info.name.startswith("_"):
            continue
        module = importlib.import_module(f"beebium.client.{module_info.name}")
        for class_name, cls in inspect.getmembers(module, inspect.isclass):
            if cls.__module__ != module.__name__:
                continue
            for name, member in vars(cls).items():
                function = getattr(member, "__func__", member)
                doc = inspect.getdoc(function) or ""
                requires, leaves = requires_pattern.findall(doc), leaves_pattern.findall(doc)
                if not requires and not leaves:
                    continue
                where = f"{class_name}.{name}"
                assert len(requires) == 1 and len(leaves) == 1, (
                    f"{where} needs exactly one Requires: and one Leaves the machine: line")
                found[where] = (requires[0], leaves[0])
    return found


def _tabled(heading: str = "### Run state") -> dict[str, tuple[str, str]]:
    """A README template run-state table, by Class.method."""
    text = TEMPLATE_FILEPATH.read_text()
    section = text.split(heading + "\n", 1)[1].split("\n### ", 1)[0]
    rows = {}
    for line in section.splitlines():
        match = ROW.match(line)
        if match:
            rows[f"{match[1]}.{match[2]}"] = (match[3], match[4])
    return rows


def test_the_table_and_the_docstrings_agree() -> None:
    documented, tabled = _documented(), _tabled()
    assert documented, "no docstring carries the run-state lines"
    assert sorted(set(documented) - set(tabled)) == [], "documented but not in the table"
    assert sorted(set(tabled) - set(documented)) == [], "in the table but not documented"
    for where, lines in documented.items():
        assert tabled[where] == lines, where


def test_the_lines_use_the_shared_words() -> None:
    """Leaves is one of four values; Requires starts from one of four."""
    for where, (requires, leaves) in _documented().items():
        assert leaves in {"RUNNING", "STOPPED", "unchanged", "as it was on entry"}, where
        assert requires.startswith(
            ("nothing", "any run state", "a running machine", "a stopped machine")), where


def test_the_coprocessor_table_and_the_docstrings_agree() -> None:
    """The coprocessor's lines, and their table, are kept in step the same way (#204)."""
    documented = _documented(COPROCESSOR_REQUIRES, COPROCESSOR_LEAVES)
    tabled = _tabled("### Coprocessor run state")
    assert documented, "no docstring carries the coprocessor run-state lines"
    assert sorted(set(documented) - set(tabled)) == [], "documented but not in the table"
    assert sorted(set(tabled) - set(documented)) == [], "in the table but not documented"
    for where, lines in documented.items():
        assert tabled[where] == lines, where
    for where, (requires, leaves) in documented.items():
        assert leaves in {"RUNNING", "STOPPED", "unchanged", "as it was on entry"}, where
        assert requires.startswith(
            ("nothing", "any run state", "a running host", "a stopped coprocessor")), where
