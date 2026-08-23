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

"""Python wrapper for ExtensionUiService.

The Extension UI framework lets any extension declare a typed control
tree (the *View*) which the server streams to clients. Clients render
natively and post user actions back through Dispatch. This module wraps
the gRPC SubscribeView / Dispatch RPCs and converts the proto types
into ergonomic dataclasses.

See ``docs/discussion/extension-ui-architecture.md`` for the design.

Usage::

    # One-shot: get the current view of the piconet panel.
    view = next(bbc.extension_ui.subscribe_view("piconet"))
    for control in view.root.children:
        print(control.id, control.kind)

    # Toggle the piconet "Enabled" control.
    bbc.extension_ui.dispatch("piconet", "mode_toggle", view.revision,
                               payload=False)

    # Background: receive view updates as they arrive.
    handle = bbc.extension_ui.start_background_subscription(
        "piconet", lambda v: print("revision", v.revision))
    ...
    handle.stop()
"""

from __future__ import annotations

import enum
import threading
from collections.abc import Callable, Iterator
from dataclasses import dataclass

from beebium.client._proto import extension_ui_pb2, extension_ui_pb2_grpc


class IndicatorState(enum.IntEnum):
    """Semantic state for an Indicator control.

    Renderers map this to whatever colour / icon convention their
    platform prefers; the server reports semantics, not presentation.
    """

    UNKNOWN = 0
    OK = 1
    WARN = 2
    ERROR = 3


class ControlKind(enum.Enum):
    """Tag for the seven control primitives plus the structural Group."""

    LABEL = "label"
    INDICATOR = "indicator"
    TOGGLE = "toggle"
    BUTTON = "button"
    CHOICE = "choice"
    TEXT_INPUT = "text_input"
    GROUP = "group"
    UNSET = "unset"


@dataclass(frozen=True)
class Label:
    text: str = ""
    # Optional muted caption shown alongside the primary text. Used
    # for provenance / status / size metadata that's logically
    # attached to the primary line. Empty means no caption.
    secondary_text: str = ""


@dataclass(frozen=True)
class Indicator:
    state: IndicatorState = IndicatorState.UNKNOWN
    text: str = ""


@dataclass(frozen=True)
class Toggle:
    label: str = ""
    value: bool = False


@dataclass(frozen=True)
class Button:
    label: str = ""
    enabled: bool = True


@dataclass(frozen=True)
class Choice:
    label: str = ""
    options: tuple[str, ...] = ()
    selected_index: int = 0


@dataclass(frozen=True)
class TextInput:
    label: str = ""
    value: str = ""
    placeholder: str = ""


@dataclass(frozen=True)
class Group:
    """A nested group of controls. ``label`` is None for unlabelled groups."""

    label: str | None = None
    children: tuple[Control, ...] = ()


@dataclass(frozen=True)
class Control:
    """A control in the View tree.

    Exactly one of ``label``, ``indicator``, ``toggle``, ``button``,
    ``choice``, ``text_input``, ``group`` is populated, indicated by
    the ``kind`` discriminator.
    """

    id: str
    kind: ControlKind
    label: Label | None = None
    indicator: Indicator | None = None
    toggle: Toggle | None = None
    button: Button | None = None
    choice: Choice | None = None
    text_input: TextInput | None = None
    group: Group | None = None


@dataclass(frozen=True)
class View:
    """A complete view of an extension's UI at one revision."""

    extension_id: str
    revision: int
    root: Control


def _convert_control(proto: extension_ui_pb2.Control) -> Control:
    """Convert a single proto Control into the dataclass form."""
    case = proto.WhichOneof("control")
    if case == "label":
        return Control(
            id=proto.id,
            kind=ControlKind.LABEL,
            label=Label(
                text=proto.label.text,
                secondary_text=proto.label.secondary_text,
            ),
        )
    if case == "indicator":
        return Control(
            id=proto.id,
            kind=ControlKind.INDICATOR,
            indicator=Indicator(
                state=IndicatorState(proto.indicator.state),
                text=proto.indicator.text,
            ),
        )
    if case == "toggle":
        return Control(
            id=proto.id,
            kind=ControlKind.TOGGLE,
            toggle=Toggle(
                label=proto.toggle.label,
                value=proto.toggle.value,
            ),
        )
    if case == "button":
        return Control(
            id=proto.id,
            kind=ControlKind.BUTTON,
            button=Button(
                label=proto.button.label,
                enabled=proto.button.enabled,
            ),
        )
    if case == "choice":
        return Control(
            id=proto.id,
            kind=ControlKind.CHOICE,
            choice=Choice(
                label=proto.choice.label,
                options=tuple(proto.choice.options),
                selected_index=proto.choice.selected_index,
            ),
        )
    if case == "text_input":
        return Control(
            id=proto.id,
            kind=ControlKind.TEXT_INPUT,
            text_input=TextInput(
                label=proto.text_input.label,
                value=proto.text_input.value,
                placeholder=proto.text_input.placeholder,
            ),
        )
    if case == "group":
        children = tuple(_convert_control(c) for c in proto.group.controls)
        # 'label' is an optional field in proto3; HasField distinguishes
        # "not set" from "set to empty string".
        label = proto.group.label if proto.group.HasField("label") else None
        return Control(
            id=proto.id,
            kind=ControlKind.GROUP,
            group=Group(label=label, children=children),
        )
    return Control(id=proto.id, kind=ControlKind.UNSET)


def _convert_view(proto: extension_ui_pb2.View) -> View:
    return View(
        extension_id=proto.extension_id,
        revision=proto.view_revision,
        root=_convert_control(proto.root),
    )


@dataclass(frozen=True)
class DispatchResult:
    """Return value from Extension UI dispatch."""

    accepted: bool
    error: str = ""


class SubscriptionHandle:
    """Handle for a background view subscription."""

    def __init__(self, thread: threading.Thread, stop_event: threading.Event):
        self._thread = thread
        self._stop_event = stop_event

    def stop(self, timeout: float = 1.0) -> None:
        """Signal the background subscription to stop and join the thread.

        This is a prompt cancel: the loop checks the stop flag before each
        callback, so an item already pulled from the stream when stop() is
        called is not delivered. To let a finite stream finish and deliver every
        item, use wait() instead.
        """
        self._stop_event.set()
        self._thread.join(timeout)

    def wait(self, timeout: float | None = None) -> bool:
        """Block until the subscription finishes on its own -- a finite stream
        ends, or the server closes it -- and report whether it did. Unlike
        stop(), this does not signal cancellation; it only waits."""
        self._thread.join(timeout)
        return not self._thread.is_alive()

    @property
    def is_running(self) -> bool:
        return self._thread.is_alive()


class ExtensionUi:
    """Wraps ExtensionUiService.

    Use ``subscribe_view`` to iterate over View updates for a named
    extension, and ``dispatch`` to post a user action back. The server
    delivers the response of an action as the next yielded View on the
    subscription, not in the dispatch return -- ``DispatchResult.accepted``
    only reports whether the framework's validation gauntlet (extension
    exists, control id known, payload type matches, revision current)
    passed.
    """

    def __init__(self, stub: extension_ui_pb2_grpc.ExtensionUiServiceStub):
        self._stub = stub

    def subscribe_view(self, extension_id: str) -> Iterator[View]:
        """Open a server-stream of View updates for one extension.

        The first View arrives immediately; subsequent Views arrive as
        the extension's state changes.

        Raises:
            grpc.RpcError: with code NOT_FOUND if the extension does
                not exist or has no UI.
        """
        request = extension_ui_pb2.SubscribeViewRequest(extension_id=extension_id)
        for proto_view in self._stub.SubscribeView(request):
            yield _convert_view(proto_view)

    def start_background_subscription(
        self,
        extension_id: str,
        callback: Callable[[View], None],
    ) -> SubscriptionHandle:
        """Subscribe in a daemon thread; invoke callback on each View.

        Returns a handle that can ``.stop()`` the background thread.
        Exceptions inside the stream (e.g. server disconnect, NOT_FOUND)
        terminate the thread silently -- check ``handle.is_running`` if
        the caller needs to detect that.
        """
        stop_event = threading.Event()

        def loop() -> None:
            try:
                for view in self.subscribe_view(extension_id):
                    if stop_event.is_set():
                        break
                    callback(view)
            except Exception:
                pass

        thread = threading.Thread(target=loop, daemon=True)
        thread.start()
        return SubscriptionHandle(thread, stop_event)

    def dispatch(
        self,
        extension_id: str,
        control_id: str,
        view_revision: int,
        payload: bool | str | int | None = None,
    ) -> DispatchResult:
        """Post a user action back to the server.

        The ``payload`` is type-dispatched to match the addressed
        control's expected variant:

            * ``bool``  -> Toggle
            * ``str``   -> TextInput
            * ``int``   -> Choice (selected index, must be non-negative)
            * ``None``  -> Button (no payload)

        Returns ``DispatchResult(accepted=False, error=...)`` if the
        framework's validation gauntlet rejects the request (stale
        revision, unknown control, payload-type mismatch, unknown
        extension). The actual mutation of the extension's state is
        observable on the SubscribeView stream as the next pushed View,
        not in the return value.
        """
        request = extension_ui_pb2.DispatchRequest(
            extension_id=extension_id,
            control_id=control_id,
            view_revision=view_revision,
        )
        if payload is None:
            pass  # leave the oneof unset (Button)
        elif isinstance(payload, bool):
            request.bool_value = payload
        elif isinstance(payload, str):
            request.string_value = payload
        elif isinstance(payload, int):
            if payload < 0:
                raise ValueError(f"Choice index payload must be non-negative; got {payload}")
            request.index_value = payload
        else:
            raise TypeError(f"dispatch payload must be bool, str, int, or None; got {type(payload).__name__}")

        response = self._stub.Dispatch(request)
        return DispatchResult(accepted=response.accepted, error=response.error)
