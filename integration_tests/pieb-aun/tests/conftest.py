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

"""Fixtures for the PiEconetBridge AUN interop tests.

These tests run a real, unmodified PiEconetBridge with no Pi, no Econet HAT
and no kernel module, and drive a Beebium station against its emulated
fileserver over AUN.

Run with:
    cd integration_tests/pieb-aun
    uv run pytest -m slow -v -s
"""

from __future__ import annotations

import os
import sys
import uuid
from pathlib import Path

import pytest

from pieb_test_support.bridge import (
    AunHost,
    BridgeConfig,
    ContainerBridge,
    NativeBridge,
    build_image,
    docker_available,
    free_udp_port,
    native_binary,
)
from pieb_test_support.perturb import PerturbationProxy
from pieb_test_support.topology import (
    BEEBIUM_BRIDGE_SIDE_NET,
    BEEBIUM_NET_DEFAULT,
    BEEBIUM_STATION,
    BRIDGE_AUN_PORT,
    BRIDGE_FS_STATION,
    BRIDGE_NET,
    DYNAMIC_NET,
)

REPO_ROOT = Path(__file__).resolve().parents[3]



def pytest_collection_modifyitems(config, items):
    """Skip tests marked 'slow' unless explicitly requested."""
    if config.getoption("-m", default="") and "slow" in config.getoption("-m"):
        return
    skip_slow = pytest.mark.skip(reason="slow test -- run with pytest -m slow")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip_slow)


# ---- Beebium-side fixtures ----


@pytest.fixture(scope="session")
def roms_dirpath():
    env = os.environ.get("BEEBIUM_ROM_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise FileNotFoundError(f"BEEBIUM_ROM_DIR={env} is not a directory")
    candidate = REPO_ROOT / "roms"
    if candidate.is_dir():
        return candidate
    raise FileNotFoundError(
        "ROM directory not found. Set BEEBIUM_ROM_DIR or place ROMs in roms/"
    )


@pytest.fixture(scope="session")
def mos_filepath(roms_dirpath):
    p = roms_dirpath / "acorn-mos_1_20.rom"
    if not p.exists():
        pytest.skip(f"MOS 1.20 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def basic_filepath(roms_dirpath):
    p = roms_dirpath / "bbc-basic_2.rom"
    if not p.exists():
        pytest.skip(f"BASIC 2 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def nfs_filepath(roms_dirpath):
    """A network filing system ROM for the client station.

    ANFS 4.18 is preferred because it is what the L3FS suite already uses on a
    Model B; NFS 3.34 is accepted as an alternative.
    """
    for name in ("acorn-anfs_4_18.rom", "acorn-nfs_3_34.rom"):
        p = roms_dirpath / name
        if p.exists():
            return p
    pytest.skip(f"No NFS/ANFS ROM found in {roms_dirpath}")


@pytest.fixture(scope="session")
def server_filepath():
    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER={env} not found")
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        REPO_ROOT / "build-release" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        REPO_ROOT / "build" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        REPO_ROOT / "cmake-build-debug" / "src" / "server" / f"beebium-model-b{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip("beebium-model-b not found. Set BEEBIUM_SERVER or build the server.")


# ---- Bridge fixtures ----


@pytest.fixture(scope="session")
def bridge_flavour():
    """Which bridge implementation this session will use.

    An explicit BEEBIUM_PIEB_FLAVOUR of 'native' or 'container' overrides the
    choice, so CI can exercise the container path even on a machine where a
    native binary is present.
    """
    requested = os.environ.get("BEEBIUM_PIEB_FLAVOUR", "").strip().lower()
    if requested == "native":
        if native_binary() is None:
            pytest.skip(
                "BEEBIUM_PIEB_FLAVOUR=native but no econet-hpbridge found. "
                "Set BEEBIUM_PIEB_BIN or BEEBIUM_PIEB_SRC."
            )
        return "native"
    if requested == "container":
        if not docker_available():
            pytest.skip("BEEBIUM_PIEB_FLAVOUR=container but Docker is unavailable")
        return "container"
    if requested:
        raise ValueError(
            f"BEEBIUM_PIEB_FLAVOUR={requested!r}: expected 'native' or 'container'"
        )

    if native_binary() is not None:
        return "native"
    if docker_available():
        return "container"
    pytest.skip(
        "No PiEconetBridge available: no native econet-hpbridge (set "
        "BEEBIUM_PIEB_BIN or BEEBIUM_PIEB_SRC on Linux) and no working Docker."
    )


@pytest.fixture(scope="session")
def bridge_image(bridge_flavour):
    """Build the container image once per session, if it will be used."""
    if bridge_flavour == "container":
        build_image()
    return bridge_flavour


@pytest.fixture
def beebium_net(request):
    """What Beebium declares via ``--aun net=``.

    Defaults to 0, the flat-cloud convention. A test that wants to exercise a
    genuine multi-net topology overrides it:

        @pytest.mark.parametrize("beebium_net", [2], indirect=True)
    """
    return getattr(request, "param", BEEBIUM_NET_DEFAULT)


@pytest.fixture
def beebium_aun_port():
    """An ephemeral UDP port for this test's Beebium AUN socket."""
    return free_udp_port()


@pytest.fixture
def bridge(bridge_image, bridge_flavour, beebium_aun_port, tmp_path, request):
    """A running PiEconetBridge with a fileserver, ready for traffic.

    Yields once the bridge reports both its fileserver initialised and its
    main loop idle, then dumps its log if the test failed and stops it.
    """
    use_host_network = sys.platform == "linux"

    # The bridge resolves peer addresses when it parses its config, so it must
    # be told at construction time how to reach us. Under host networking or
    # natively that is loopback; under Docker Desktop it is the container's
    # route back to the host.
    host_address = "127.0.0.1" if (
        bridge_flavour == "native" or use_host_network
    ) else "host.docker.internal"

    # Where the path between us and the bridge is NAT'd, no static AUN MAP
    # HOST entry can match our traffic: Docker Desktop's published-port
    # forwarder rewrites both the source address and the source port, and the
    # rewritten port is not knowable before the first datagram. Upstream's own
    # answer to an unrecognised AUN source is a dynamic net, so use one --
    # the bridge allocates us a station on first contact. On a clean path we
    # keep the static entry, which pins our identity and is the stricter test.
    natting = bridge_flavour == "container" and not use_host_network

    config = BridgeConfig(
        fs_net=BRIDGE_NET,
        fs_station=BRIDGE_FS_STATION,
        aun_port=BRIDGE_AUN_PORT,
        peers=[] if natting else [
            AunHost(
                net=BEEBIUM_BRIDGE_SIDE_NET,
                station=BEEBIUM_STATION,
                address=host_address,
                port=beebium_aun_port,
            )
        ],
        dynamic_net=DYNAMIC_NET if natting else None,
    )

    if bridge_flavour == "native":
        instance = NativeBridge(config, native_binary(), tmp_path)
    else:
        instance = ContainerBridge(
            config,
            container_name=f"beebium-pieb-{uuid.uuid4().hex[:12]}",
            use_host_network=use_host_network,
        )

    try:
        instance.wait_until_ready()
        if isinstance(instance, ContainerBridge):
            # Must happen after the container is up, and before Beebium is
            # launched: the discovered address goes into Beebium's peer map.
            discovered = instance.discover_peer_address()
            print(f"\nBridge reachable at {discovered}:{instance.aun_port}; "
                  f"its datagrams will appear to come from {discovered}")
        yield instance
    finally:
        # Dump the log before stopping, and from here rather than a separate
        # fixture: every tier-2 scenario fails identically from the outside
        # -- the guest did not get its reply -- so the bridge's own account of
        # what it saw is the difference between a diagnosable failure and a
        # mystery. Teardown order makes this the last moment the instance is
        # reachable.
        report = getattr(request.node, "rep_call", None)
        if report is not None and report.failed:
            print("\n---- PiEconetBridge log ----")
            print(instance.logs())
            print("---- end PiEconetBridge log ----")
        instance.stop()


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    """Record each phase's result so fixtures can see whether a test failed."""
    outcome = yield
    report = outcome.get_result()
    setattr(item, f"rep_{report.when}", report)


@pytest.fixture
def beebium_args(bridge, beebium_aun_port, beebium_net, nfs_filepath):
    """Command-line arguments for a Beebium station peered with the bridge."""
    return _beebium_args(beebium_aun_port, beebium_net, nfs_filepath,
                         bridge.beebium_map_entry())


@pytest.fixture
def perturbing_proxy(bridge):
    """A relay between Beebium and the bridge, perturbing under test control.

    The traffic stays genuine -- real scout timing, real fileserver replies --
    and only its arrival order becomes ours to choose. Nothing reorders on a
    loopback path by itself, so a scenario that needs reordering needs this.
    """
    proxy = PerturbationProxy(bridge.peer_address(), bridge.aun_port)
    try:
        yield proxy
    finally:
        proxy.stop()


@pytest.fixture
def beebium_args_via_proxy(bridge, perturbing_proxy, beebium_aun_port,
                           beebium_net, nfs_filepath):
    """Beebium pointed at the proxy rather than straight at the bridge."""
    net, station = bridge.fileserver
    map_entry = f"{net}.{station}@{perturbing_proxy.address}@{perturbing_proxy.port}"
    return _beebium_args(beebium_aun_port, beebium_net, nfs_filepath, map_entry)


def _beebium_args(aun_port, net, nfs_filepath, map_entry):
    return [
        "--sideways", f"9:rom:{nfs_filepath}",
        "--station", str(BEEBIUM_STATION),
        "--aun", (
            f"port={aun_port}"
            f":net={net}"
            f":map={map_entry}"
        ),
        "--machine-name", f"Station {BEEBIUM_STATION}",
    ]
