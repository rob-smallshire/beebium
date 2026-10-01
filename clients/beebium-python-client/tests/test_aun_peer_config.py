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

"""AUN peer configuration and transport activation against a real server.

Covers issue #54 (EconetService.Enable with an aun_port must bind the configured
transport, so AUN status reflects it) and issue #55 (the peer table is editable
before the backend is up; entries survive the backend being brought up).
"""

from __future__ import annotations

import json
import subprocess
import threading
import time
from pathlib import Path

from beebium.client import Beebium
from beebium.client._proto import extension_ui_pb2
from beebium.client.installation import DEFAULT_VARIANT, ServerInstallation
from beebium.ext.econet.aun import Aun, PeerSource


def _peers_has_map_file_row(view_proto: extension_ui_pb2.View, item_id: str) -> bool:
    """True if the pushed view's "peers" EditableList holds `item_id` with the
    "map file" provenance. The EditableList/FileReference primitives are not
    modelled by the dataclass client wrapper yet, so tests read the raw proto
    the sidebar receives."""

    def walk(control: extension_ui_pb2.Control) -> bool:
        case = control.WhichOneof("control")
        if case == "editable_list" and control.id == "peers":
            for item in control.editable_list.items:
                if item.id == item_id and item.secondary == "map file":
                    return True
        if case == "group":
            return any(walk(child) for child in control.group.controls)
        return False

    return walk(view_proto.root)


class _ViewStream:
    """Collects pushed AUN panel Views on a background thread, cancellable.

    Subscribes to the ExtensionUi SubscribeView stream the sidebar uses (raw
    proto, so EditableList items are visible) and appends every pushed View.
    """

    def __init__(self, bbc: Beebium, extension_id: str):
        request = extension_ui_pb2.SubscribeViewRequest(extension_id=extension_id)
        self._call = bbc.extension_ui._stub.SubscribeView(request)
        self._lock = threading.Lock()
        self._views: list[extension_ui_pb2.View] = []
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self) -> None:
        try:
            for view in self._call:
                with self._lock:
                    self._views.append(view)
        except Exception:
            pass  # the stream was cancelled, or the server went away

    def wait_initial(self, timeout: float = 5.0) -> extension_ui_pb2.View:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                if self._views:
                    return self._views[0]
            time.sleep(0.05)
        raise AssertionError("no initial view was pushed on subscription")

    def wait_for_map_file_row(self, item_id: str, timeout: float = 15.0) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                views = list(self._views)
            if any(_peers_has_map_file_row(v, item_id) for v in views):
                return True
            time.sleep(0.1)
        return False

    def any_has_map_file_row(self, item_id: str) -> bool:
        with self._lock:
            return any(_peers_has_map_file_row(v, item_id) for v in self._views)

    def close(self) -> None:
        self._call.cancel()
        self._thread.join(timeout=2.0)


def test_enable_with_port_binds_the_aun_transport(mos_filepath: Path, server_installation: ServerInstallation) -> None:
    # #54: with the AUN transport configured but no station at launch, the
    # backend is inactive; Enable(aun_port=...) must bring it up through the
    # transport so AUN status shows the bound port, not local_port=0.
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", "net=1"],
    ) as bbc:
        assert bbc.transport[Aun].status.local_port == 0
        assert bbc.transport[Aun].status.connected is False

        bbc.econet.enable(station_id=254, aun_port=32768)

        status = bbc.transport[Aun].status
        assert status.local_port == 32768
        assert status.connected is True


def test_add_peer_before_enable_survives_and_is_listed(
    mos_filepath: Path, server_installation: ServerInstallation
) -> None:
    # #55: the peer table lives in the transport, not the backend, so AddPeer
    # works before the socket is up and the entry survives Enable bringing it
    # up. Previously this failed with "AUN backend is not active".
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", "net=1"],
    ) as bbc:
        aun = bbc.transport[Aun]

        # No backend yet, but AddPeer still takes the entry.
        aun.add_peer(net=1, stn=254, ip_address="127.0.0.1", port=40001)
        peers = aun.peers
        assert len(peers) == 1
        assert peers[0].stn == 254
        assert peers[0].port == 40001
        assert peers[0].source == PeerSource.API

        # Bring the transport up; the entry survives and is now routable.
        bbc.econet.enable(station_id=200, aun_port=32768)
        assert bbc.transport[Aun].status.local_port == 32768

        peers = aun.peers
        assert len(peers) == 1
        assert peers[0].stn == 254
        assert peers[0].source == PeerSource.API


def test_api_peer_survives_disable_and_reenable(mos_filepath: Path, server_installation: ServerInstallation) -> None:
    # #55: DisableEconet frees the backend. The peer table lives in the
    # transport, so the Api peer survives, peer edits after Disable are safe
    # (no use-after-free on the freed backend), and the peer is routed again
    # after a re-Enable.
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", "net=1"],
    ) as bbc:
        aun = bbc.transport[Aun]

        bbc.econet.enable(station_id=200, aun_port=32768)
        aun.add_peer(net=1, stn=100, ip_address="127.0.0.1", port=40001)
        assert aun.status.local_port == 32768
        assert len(aun.peers) == 1

        bbc.econet.disable()
        # No backend now: status reports no link, but peer edits stay safe and
        # the Api entry is retained in the transport's peer set.
        assert aun.status.connected is False
        assert aun.status.local_port == 0
        aun.add_peer(net=1, stn=101, ip_address="127.0.0.1", port=40002)
        peers = {p.stn for p in aun.peers}
        assert {100, 101} <= peers

        # Re-enable: the Api peers are applied to the fresh backend.
        bbc.econet.enable(station_id=200, aun_port=32768)
        assert bbc.transport[Aun].status.local_port == 32768
        peers = {p.stn for p in aun.peers}
        assert {100, 101} <= peers
        assert all(p.source == PeerSource.API for p in aun.peers)


def test_map_file_peers_listed_with_provenance_and_reloaded(
    mos_filepath: Path, server_installation: ServerInstallation, tmp_path: Path
) -> None:
    # #139: launch with a map file; its peers are listed with "map file"
    # provenance, and an edit is picked up by ReloadMap without a restart.
    map_filepath = tmp_path / "aun-map.json"
    map_filepath.write_text(json.dumps({"peers": [{"net": 0, "station": 100, "host": "127.0.0.1", "port": 40100}]}))

    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", f"map-file={map_filepath}"],
    ) as bbc:
        bbc.econet.enable(station_id=2, aun_port=32768)
        aun = bbc.transport[Aun]

        peers = {p.stn: p for p in aun.peers}
        assert 100 in peers
        assert peers[100].source == PeerSource.MAP_FILE
        status = aun.status
        assert status.map_file_path == str(map_filepath)
        assert status.map_file_entry_count == 1

        # Edit the file on disk: drop 100, add 101. ReloadMap applies it.
        map_filepath.write_text(json.dumps({"peers": [{"net": 0, "station": 101, "host": "127.0.0.1", "port": 40101}]}))
        aun.reload_map()

        stations = {p.stn for p in aun.peers}
        assert 101 in stations
        assert 100 not in stations


def test_map_edit_rpcs_write_the_file_and_survive_a_hand_edit(
    mos_filepath: Path, server_installation: ServerInstallation, tmp_path: Path
) -> None:
    # #141: the map-edit RPCs write the server's own file; a hand edit of the
    # file between two RPC calls (an unknown key) survives the next write, and
    # ListMap reflects the file.
    map_filepath = tmp_path / "aun-map.json"

    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", f"map-file={map_filepath}"],
    ) as bbc:
        bbc.econet.enable(station_id=2, aun_port=32768)
        aun = bbc.transport[Aun]

        aun.add_map_peer(net=0, stn=254, host="192.168.1.10", port=32768, label="fs")
        aun.add_map_subnet(net=128, subnet="192.168.5.0/24", label="risc os")

        listing = aun.list_map()
        assert {p.stn for p in listing.peers} == {254}
        assert listing.peers[0].label == "fs"
        assert listing.peers[0].resolved is True  # an IPv4 literal resolves
        assert {s.net for s in listing.subnets} == {128}

        # The write reached the live routing table at once (not only the file).
        assert any(p.stn == 254 for p in aun.peers)

        # A hand edit adds an unknown top-level key.
        text = map_filepath.read_text()
        brace = text.index("{")
        map_filepath.write_text(text[: brace + 1] + '\n  "schema": 7,' + text[brace + 1 :])

        # The next RPC must preserve the hand-added key.
        aun.add_map_peer(net=0, stn=200, host="192.168.1.99", port=32768)
        after = map_filepath.read_text()
        assert '"schema"' in after
        assert "192.168.1.10" in after  # first peer kept
        assert "192.168.1.99" in after  # second peer added

        # RemoveMapPeer reports whether it removed anything.
        assert aun.remove_map_peer(net=0, stn=254) is True
        assert aun.remove_map_peer(net=0, stn=111) is False
        assert {p.stn for p in aun.list_map().peers} == {200}


def test_running_instance_sees_a_subcommand_write_via_the_poll(
    mos_filepath: Path, server_installation: ServerInstallation, tmp_path: Path
) -> None:
    # #141: a CLI subcommand writing the map file reaches a running instance
    # through the same mtime poll a hand edit uses -- no RPC, no restart.
    map_filepath = tmp_path / "aun-map.json"

    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", f"map-file={map_filepath}"],
    ) as bbc:
        bbc.econet.enable(station_id=2, aun_port=32768)
        aun = bbc.transport[Aun]
        assert not any(p.stn == 254 for p in aun.peers)

        # A separate process (the same executable) edits the file via a
        # subcommand -- exactly what a GUI or a hand run would do.
        completed = subprocess.run(
            [
                str(server_installation.executable_filepath(DEFAULT_VARIANT)),
                "add-aun-peer",
                "0.254",
                "127.0.0.1",
                "40254",
                "--map-file",
                str(map_filepath),
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert completed.returncode == 0, completed.stderr

        # The running server's poll (~2.5 s) picks up the write on its own.
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            peers = {p.stn: p for p in aun.peers}
            if 254 in peers:
                assert peers[254].source == PeerSource.MAP_FILE
                break
            time.sleep(0.25)
        else:
            raise AssertionError("the subcommand's write was not polled in")


def test_poll_driven_reload_repushes_the_sidebar_view(
    mos_filepath: Path, server_installation: ServerInstallation, tmp_path: Path
) -> None:
    # #146: a map-file change seen by the sweep poll must re-push the AUN panel
    # view, not just update the peer table. We subscribe to the panel's
    # SubscribeView stream (what the sidebar uses), then edit the file from
    # outside by subcommand -- no RPC on this instance -- and require a pushed
    # view to gain the "map file" row on its own. Station 111 is chosen so no
    # real announcer on the host or LAN supplies it via discovery; the only
    # source is the map file.
    map_filepath = tmp_path / "aun-map.json"

    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=["--aun", f"map-file={map_filepath}"],
    ) as bbc:
        bbc.econet.enable(station_id=2, aun_port=32768)
        ext_id = bbc.transport.active.id
        assert ext_id, "no active AUN transport to subscribe to"

        stream = _ViewStream(bbc, ext_id)
        try:
            stream.wait_initial()
            # Baseline: nothing has supplied station 111 yet.
            assert not stream.any_has_map_file_row("0.111")

            # A separate process edits the shared file -- no RPC on this server.
            completed = subprocess.run(
                [
                    str(server_installation.executable_filepath(DEFAULT_VARIANT)),
                    "add-aun-peer",
                    "0.111",
                    "127.0.0.1",
                    "40111",
                    "--map-file",
                    str(map_filepath),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            assert completed.returncode == 0, completed.stderr

            # The poll reloads the file and must re-push the view on its own.
            assert stream.wait_for_map_file_row("0.111"), (
                "the poll-driven reload updated the peer table but did not re-push the sidebar view (#146)"
            )
        finally:
            stream.close()


def test_add_map_peer_in_one_instance_updates_a_second_instances_sidebar(
    mos_filepath: Path, server_installation: ServerInstallation, tmp_path: Path
) -> None:
    # #146, two-instance variant: instance A adds a peer through AddMapPeer,
    # writing the shared map file; instance B, subscribed to its own panel
    # view, gains the row from its poll with no interaction. Distinct AUN ports
    # so both can bind on this host; station 111 again avoids real discovery.
    map_filepath = tmp_path / "aun-map.json"
    launch_args = {
        "server": server_installation,
        "mos_filepath": mos_filepath,
        "extra_args": ["--aun", f"map-file={map_filepath}"],
    }

    with Beebium.launch(**launch_args) as bbc_a, Beebium.launch(**launch_args) as bbc_b:
        bbc_a.econet.enable(station_id=20, aun_port=32768)
        bbc_b.econet.enable(station_id=21, aun_port=32769)
        ext_id_b = bbc_b.transport.active.id
        assert ext_id_b, "no active AUN transport on instance B"

        stream = _ViewStream(bbc_b, ext_id_b)
        try:
            stream.wait_initial()
            assert not stream.any_has_map_file_row("0.111")

            # Instance A edits the shared file through the RPC the sidebar uses.
            bbc_a.transport[Aun].add_map_peer(0, 111, "127.0.0.1", 40111)

            assert stream.wait_for_map_file_row("0.111"), (
                "instance B's sidebar view did not update after instance A added a map-file peer (#146)"
            )
        finally:
            stream.close()
