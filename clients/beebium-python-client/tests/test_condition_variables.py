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

"""Debugger conditions name the machine's latches; the server lists them (#186)."""

from __future__ import annotations

import pytest

from beebium.client.exceptions import InvalidConditionError


@pytest.mark.parametrize(
    ("variant", "machine_names", "watchpoint_names"),
    [
        ("model-b", {"bank", "romsel"}, {"addr", "value", "write"}),
        ("model-b-plus", {"bank", "romsel", "acccon", "andy", "vdusel", "vdu"},
         {"addr", "value", "write", "shadow_access"}),
        ("model-b-integra-b",
         {"bank", "romsel", "ramsel", "shen", "prven", "memsel", "prvs1", "prvs4", "prvs8"},
         {"addr", "value", "write", "shadow_access"}),
    ],
)
def test_the_server_lists_each_models_names(launch_bbc, variant, machine_names,
                                            watchpoint_names) -> None:
    bbc = launch_bbc(variant=variant)
    variables = bbc.debugger.condition_variables()
    by_scope: dict[str, set[str]] = {}
    for variable in variables:
        assert variable.description
        by_scope.setdefault(variable.scope, set()).add(variable.name)
    assert by_scope["machine"] == machine_names
    assert by_scope["watchpoint"] == watchpoint_names
    assert {"A", "X", "Y", "PC", "cycles", "hits"} <= by_scope["built_in"]


def test_a_listed_name_works_and_an_unknown_one_is_refused_by_name(launch_bbc) -> None:
    bbc = launch_bbc(variant="model-b-plus")
    with bbc.debugger.breakpoint(0x0400, condition="vdusel && bank == 15"):
        pass
    with pytest.raises(InvalidConditionError, match="no variable 'shen' on model-b-plus"):
        bbc.debugger.add_breakpoint(0x0400, condition="shen")
    with pytest.raises(InvalidConditionError, match="'value' in a breakpoint condition"):
        bbc.debugger.add_breakpoint(0x0400, condition="value == 0")
    watchpoint = bbc.debugger.add_watchpoint(0x3000, 0x8000, "write",
                                             condition="vdusel && !shadow_access")
    watchpoint.remove()
