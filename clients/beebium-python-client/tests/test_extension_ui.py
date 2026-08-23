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

"""Unit tests for the ExtensionUi client wrapper.

End-to-end behaviour against a real server is covered by the C++
test_grpc_extension_ui_service. These tests pin the Python wrapper:
proto-to-dataclass conversion of every Control variant, the dispatch
payload-type dispatch logic, and the validation errors for bad payload
types.
"""

from __future__ import annotations

import threading
from unittest.mock import MagicMock

import pytest

from beebium.client._proto import extension_ui_pb2
from beebium.client.extension_ui import (
    ControlKind,
    DispatchResult,
    ExtensionUi,
    IndicatorState,
    View,
)


def _make_view_proto() -> extension_ui_pb2.View:
    """Build a moderately deep View proto exercising every primitive."""
    view = extension_ui_pb2.View()
    view.extension_id = "test"
    view.view_revision = 7

    root = view.root
    root.id = "root"
    root.group.label = "Outer"

    label_c = root.group.controls.add()
    label_c.id = "lbl"
    label_c.label.text = "Hello"
    label_c.label.secondary_text = "world"

    indicator_c = root.group.controls.add()
    indicator_c.id = "ind"
    indicator_c.indicator.state = extension_ui_pb2.Indicator.WARN
    indicator_c.indicator.text = "watch out"

    toggle_c = root.group.controls.add()
    toggle_c.id = "tog"
    toggle_c.toggle.label = "Enabled"
    toggle_c.toggle.value = True

    button_c = root.group.controls.add()
    button_c.id = "btn"
    button_c.button.label = "Go"
    button_c.button.enabled = False

    choice_c = root.group.controls.add()
    choice_c.id = "ch"
    choice_c.choice.label = "Mode"
    choice_c.choice.options.append("a")
    choice_c.choice.options.append("b")
    choice_c.choice.selected_index = 1

    text_c = root.group.controls.add()
    text_c.id = "txt"
    text_c.text_input.label = "Host"
    text_c.text_input.value = "127.0.0.1"
    text_c.text_input.placeholder = "ip"

    inner_c = root.group.controls.add()
    inner_c.id = "inner"
    # SetInParent marks the oneof as "group" without populating any
    # nested fields. Deliberately leave inner_c.group.label unset to
    # exercise the absent-label round-trip.
    inner_c.group.SetInParent()

    return view


class TestSubscribeView:
    def test_yields_converted_view(self):
        stub = MagicMock()
        stub.SubscribeView.return_value = iter([_make_view_proto()])

        client = ExtensionUi(stub)
        views = list(client.subscribe_view("test"))
        assert len(views) == 1

        view = views[0]
        assert isinstance(view, View)
        assert view.extension_id == "test"
        assert view.revision == 7
        assert view.root.id == "root"
        assert view.root.kind == ControlKind.GROUP
        assert view.root.group is not None
        assert view.root.group.label == "Outer"

        children = view.root.group.children
        assert len(children) == 7

        label, indicator, toggle, button, choice, text_input, inner = children

        assert label.kind == ControlKind.LABEL
        assert label.label.text == "Hello"
        assert label.label.secondary_text == "world"

        assert indicator.kind == ControlKind.INDICATOR
        assert indicator.indicator.state == IndicatorState.WARN
        assert indicator.indicator.text == "watch out"

        assert toggle.kind == ControlKind.TOGGLE
        assert toggle.toggle.label == "Enabled"
        assert toggle.toggle.value is True

        assert button.kind == ControlKind.BUTTON
        assert button.button.enabled is False

        assert choice.kind == ControlKind.CHOICE
        assert choice.choice.options == ("a", "b")
        assert choice.choice.selected_index == 1

        assert text_input.kind == ControlKind.TEXT_INPUT
        assert text_input.text_input.value == "127.0.0.1"
        assert text_input.text_input.placeholder == "ip"

        assert inner.kind == ControlKind.GROUP
        assert inner.group.label is None  # absent in proto -> None in dataclass


class TestDispatchPayloadDispatch:
    def _stub_with_accept(self) -> MagicMock:
        stub = MagicMock()
        stub.Dispatch.return_value = extension_ui_pb2.DispatchResponse(accepted=True)
        return stub

    def test_bool_payload_sets_bool_value(self):
        stub = self._stub_with_accept()
        client = ExtensionUi(stub)
        result = client.dispatch("ext", "ctrl", 5, payload=True)
        assert result == DispatchResult(accepted=True)
        sent = stub.Dispatch.call_args[0][0]
        assert sent.extension_id == "ext"
        assert sent.control_id == "ctrl"
        assert sent.view_revision == 5
        assert sent.WhichOneof("payload") == "bool_value"
        assert sent.bool_value is True

    def test_str_payload_sets_string_value(self):
        stub = self._stub_with_accept()
        client = ExtensionUi(stub)
        client.dispatch("ext", "ctrl", 5, payload="hello")
        sent = stub.Dispatch.call_args[0][0]
        assert sent.WhichOneof("payload") == "string_value"
        assert sent.string_value == "hello"

    def test_int_payload_sets_index_value(self):
        stub = self._stub_with_accept()
        client = ExtensionUi(stub)
        client.dispatch("ext", "ctrl", 5, payload=2)
        sent = stub.Dispatch.call_args[0][0]
        assert sent.WhichOneof("payload") == "index_value"
        assert sent.index_value == 2

    def test_none_payload_leaves_oneof_unset(self):
        stub = self._stub_with_accept()
        client = ExtensionUi(stub)
        client.dispatch("ext", "ctrl", 5, payload=None)
        sent = stub.Dispatch.call_args[0][0]
        assert sent.WhichOneof("payload") is None

    def test_default_payload_is_none(self):
        stub = self._stub_with_accept()
        client = ExtensionUi(stub)
        client.dispatch("ext", "ctrl", 5)
        sent = stub.Dispatch.call_args[0][0]
        assert sent.WhichOneof("payload") is None

    def test_negative_int_raises(self):
        client = ExtensionUi(MagicMock())
        with pytest.raises(ValueError, match="non-negative"):
            client.dispatch("ext", "ctrl", 5, payload=-1)

    def test_unsupported_payload_type_raises(self):
        client = ExtensionUi(MagicMock())
        with pytest.raises(TypeError, match="must be bool, str, int, or None"):
            client.dispatch("ext", "ctrl", 5, payload=3.14)

    def test_dispatch_propagates_error_from_server(self):
        stub = MagicMock()
        stub.Dispatch.return_value = extension_ui_pb2.DispatchResponse(
            accepted=False, error="stale event: client revision 1, current 7"
        )
        client = ExtensionUi(stub)
        result = client.dispatch("ext", "ctrl", 1, payload=True)
        assert result.accepted is False
        assert "stale" in result.error


class TestBackgroundSubscription:
    def test_finite_stream_delivers_every_view(self):
        """A finite stream ends on its own and every item is delivered.

        Wait for the thread to finish rather than stop() it: stop() is a prompt
        cancel that drops any item already pulled from the stream, so calling it
        before the thread has consumed the two views would race (delivering 0).
        """
        stub = MagicMock()
        # Two pushes then the stream ends naturally.
        stub.SubscribeView.return_value = iter([_make_view_proto(), _make_view_proto()])
        seen: list[View] = []

        client = ExtensionUi(stub)
        handle = client.start_background_subscription("test", lambda view: seen.append(view))

        assert handle.wait(timeout=2.0), "background thread did not finish the finite stream in time"
        assert handle.is_running is False
        assert len(seen) == 2
        assert all(isinstance(v, View) for v in seen)

    def test_stop_interrupts_an_infinite_stream(self):
        """stop() terminates the thread even when the stream never ends."""
        stub = MagicMock()

        def infinite_views():
            while True:
                yield _make_view_proto()

        stub.SubscribeView.return_value = infinite_views()
        delivering = threading.Event()

        client = ExtensionUi(stub)
        handle = client.start_background_subscription("test", lambda view: delivering.set())

        # Synchronise on the first delivery (not a sleep): the thread is now
        # actively iterating an unbounded stream.
        assert delivering.wait(timeout=2.0), "background thread never started delivering"
        handle.stop(timeout=2.0)
        assert handle.is_running is False
