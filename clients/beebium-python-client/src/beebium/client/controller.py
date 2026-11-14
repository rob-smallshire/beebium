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

"""Controllers: the joysticks, handsets, mice and light pens a person holds.

A controller publishes named controls, each an axis (0 to 65535, centre 32768)
or a button (pressed or not), in the device's own vocabulary: a joystick's are
``stick1.x``, ``stick1.y``, ``fire1``, ``stick2.x``, ``stick2.y`` and ``fire2``.
This module wraps ControllerService.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from enum import Enum

from beebium.client._proto import controller_pb2, controller_pb2_grpc


class ControlKind(Enum):
    """Whether a control is an axis or a button."""

    AXIS = "axis"
    BUTTON = "button"


@dataclass(frozen=True)
class Control:
    """One control a controller publishes."""

    id: str
    display_name: str
    group: str
    kind: ControlKind


@dataclass(frozen=True)
class Controller:
    """A controller attached to the machine, with its controls in order."""

    controller_id: str
    display_name: str
    controls: tuple[Control, ...]


_KINDS = {
    controller_pb2.AXIS: ControlKind.AXIS,
    controller_pb2.BUTTON: ControlKind.BUTTON,
}


class Controllers:
    """Client for ControllerService.

    Usage::

        for controller in bbc.controllers.list():
            print(controller.controller_id, [c.id for c in controller.controls])

        bbc.controllers.set("joystick", {"stick1.x": 0xF000, "fire1": True})
        values = bbc.controllers.get("joystick")  # {"stick1.x": 61440, "fire1": True, ...}

    The server's errors are raised as they come, as ``grpc.RpcError``: NOT_FOUND
    for an unknown controller, INVALID_ARGUMENT for an unknown control, a value
    of the wrong kind or an axis above 65535, with a sentence in ``details()``.
    A refused ``set`` changes nothing.
    """

    def __init__(self, stub: controller_pb2_grpc.ControllerServiceStub):
        self._stub = stub

    def list(self) -> list[Controller]:
        """The controllers attached to the machine; empty when there are none."""
        response = self._stub.ListControllers(controller_pb2.ListControllersRequest())
        return [
            Controller(
                controller_id=c.controller_id,
                display_name=c.display_name,
                controls=tuple(
                    Control(
                        id=control.id,
                        display_name=control.display_name,
                        group=control.group,
                        kind=_KINDS[control.kind],
                    )
                    for control in c.controls
                ),
            )
            for c in response.controllers
        ]

    def get(self, controller_id: str) -> dict[str, int | bool]:
        """Every control's value, in the controller's order: an int for an
        axis, a bool for a button."""
        response = self._stub.GetControls(
            controller_pb2.GetControlsRequest(controller_id=controller_id)
        )
        values: dict[str, int | bool] = {}
        for value in response.values:
            if value.WhichOneof("value") == "axis":
                values[value.id] = value.axis
            else:
                values[value.id] = value.pressed
        return values

    def set(self, controller_id: str, values: Mapping[str, int | bool]) -> None:
        """Set controls, in order: an int sets an axis, a bool a button.

        The server applies all of them or, on an error, none.
        """
        request = controller_pb2.SetControlsRequest(controller_id=controller_id)
        for control_id, value in values.items():
            entry = request.values.add(id=control_id)
            if isinstance(value, bool):
                entry.pressed = value
            else:
                entry.axis = value
        self._stub.SetControls(request)
