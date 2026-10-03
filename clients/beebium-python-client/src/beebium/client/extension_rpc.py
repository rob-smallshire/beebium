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

"""Client side of the generic ExtensionRpc channel.

Extensions never host their own gRPC service (that would put two gRPC runtimes
in one process and crash -- see docs/discussion/extension-rpc-channel.md).
Instead the core hosts one ExtensionRpc service that carries opaque serialized
bytes; this helper wraps that single stub so a per-extension client (e.g.
``RpcSerial``) can tunnel its typed messages through it.

This is the hand-written equivalent of what a stub generator will emit later;
until then, extension clients serialize their own request message, call
``invoke``/``server_stream``, and parse the reply.
"""

from __future__ import annotations

from collections.abc import Iterator

from beebium.client._proto import extension_rpc_pb2, extension_rpc_pb2_grpc


class ExtensionChannel:
    """Thin wrapper over the core's ExtensionRpc stub.

    Routing: ``extension_id`` selects an instance -- a peripheral extension's
    id (from PeripheralExtensionService) or an Econet transport's id (from
    EconetTransportService). When it is empty the core routes by service name,
    which succeeds only while exactly one loaded instance offers the service;
    with more than one the call fails with FAILED_PRECONDITION naming the
    candidate ids.
    """

    def __init__(self, stub: extension_rpc_pb2_grpc.ExtensionRpcStub):
        self._stub = stub

    def invoke(self, service: str, method: str, payload: bytes, *, extension_id: str = "") -> bytes:
        """Unary call. Returns the response payload bytes.

        A non-OK extension status surfaces as the gRPC error it maps to (the
        same ``grpc.RpcError`` a native service call would raise).
        """
        response = self._stub.Invoke(
            extension_rpc_pb2.InvokeRequest(
                extension_id=extension_id,
                service=service,
                method=method,
                payload=payload,
            )
        )
        return response.payload

    def server_stream(self, service: str, method: str, payload: bytes, *, extension_id: str = "") -> Iterator[bytes]:
        """Server-streaming call. Yields each response payload's bytes."""
        request = extension_rpc_pb2.InvokeRequest(
            extension_id=extension_id,
            service=service,
            method=method,
            payload=payload,
        )
        for response in self._stub.ServerStream(request):
            yield response.payload
