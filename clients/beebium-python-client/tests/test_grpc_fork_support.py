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

"""gRPC fork support is off unless a program asks for it.

The client forks only to exec at once, and never uses gRPC in a child. With
grpcio's fork support on, every real fork() made while a channel is busy runs
gRPC's fork handlers in the still-Python child, which on the macOS x86_64 CI
lane killed children with SIGPIPE before they could exec. (Linux subprocess
uses vfork(), which runs no fork handlers, unless a preexec_fn forces a fork.) The package turns the
support off before anything imports grpc, by whichever path it is entered.

Each check runs in a fresh interpreter: once grpc is imported the setting is
fixed for the life of the process.
"""

from __future__ import annotations

import os
import subprocess
import sys
import textwrap
from pathlib import Path

import pytest

VARIABLE = "GRPC_ENABLE_FORK_SUPPORT"

# Every way into the package that can import grpc: the package itself, a
# submodule entered directly, the pytest plugin, a generated stub, and each
# extension adapter's generated stub.
ENTRY_MODULES = [
    "beebium.client",
    "beebium.client.server",
    "beebium.client.pytest_plugin",
    "beebium.client._proto.video_pb2_grpc",
    "beebium.ext.econet.aun._proto.aun_pb2_grpc",
    "beebium.ext.econet.piconet._proto.piconet_service_pb2_grpc",
    "beebium.ext.peripheral.acorn_rtc._proto.acorn_rtc_pb2_grpc",
    "beebium.ext.peripheral.acorn_scsi._proto.scsi_host_adapter_pb2_grpc",
    "beebium.ext.peripheral.host_serial._proto.host_serial_pb2_grpc",
    "beebium.ext.peripheral.rpc_serial._proto.rpc_serial_pb2_grpc",
]


def _environment(**overrides: str) -> dict[str, str]:
    environment = {k: v for k, v in os.environ.items() if k != VARIABLE}
    environment.update(overrides)
    return environment


def _python(code: str, environment: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-c", textwrap.dedent(code)],
        env=environment,
        capture_output=True,
        text=True,
        timeout=120,
    )


def _setting_after_importing(module: str, environment: dict[str, str]) -> str:
    result = _python(
        f"""
        import os, sys
        import {module}
        assert "grpc" in sys.modules, "the entry path did not import grpc"
        print(os.environ.get("{VARIABLE}"))
        """,
        environment,
    )
    assert result.returncode == 0, result.stderr
    return result.stdout.strip()


@pytest.mark.parametrize("module", ENTRY_MODULES)
def test_importing_turns_fork_support_off(module: str) -> None:
    assert _setting_after_importing(module, _environment()) == "false"


@pytest.mark.parametrize("module", ENTRY_MODULES)
def test_a_program_that_asks_for_fork_support_keeps_it(module: str) -> None:
    assert _setting_after_importing(module, _environment(**{VARIABLE: "true"})) == "true"


def test_the_pytest_plugin_entry_point_turns_fork_support_off(tmp_path: Path) -> None:
    # pytest loads the plugin from its entry point before collecting, so a
    # test in a fresh pytest run sees the setting the plugin made.
    (tmp_path / "test_probe.py").write_text(
        textwrap.dedent(
            f"""
            import os
            import sys

            def test_probe():
                assert "beebium.client.pytest_plugin" in sys.modules
                assert os.environ.get("{VARIABLE}") == "false"
            """
        )
    )
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", str(tmp_path)],
        env=_environment(),
        capture_output=True,
        text=True,
        timeout=120,
        cwd=tmp_path,
    )
    assert result.returncode == 0, result.stdout + result.stderr
