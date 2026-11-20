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

"""One-line reprs for the live parts of the client, as a REPL shows them.

A live object (one that reaches the server) prints as ``<ClassName detail>``:
what it is and, where one cheap read gives it, the state it is in. A repr
that reads the server says the server is unavailable rather than raising,
so a REPL can still show an object whose server has gone.
"""

from __future__ import annotations

from collections.abc import Callable

import grpc


def facade_repr(obj: object, detail: str) -> str:
    """``<ClassName detail>`` for a live object."""
    return f"<{type(obj).__name__} {detail}>" if detail else f"<{type(obj).__name__}>"


def read_or_unavailable(read: Callable[[], str]) -> str:
    """The text `read` returns, or why the server could not give it."""
    try:
        return read()
    except grpc.RpcError as error:
        code = error.code() if callable(getattr(error, "code", None)) else None
        return f"(server unavailable: {code.name})" if code is not None else "(server unavailable)"
    except ValueError:
        # grpc raises ValueError for a call on a closed channel.
        return "(server unavailable: connection closed)"
