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

"""Choosing which Beebium server to run.

A server is never just one binary: an *installation* is the four machine
binaries plus the extensions, ABI libraries, ROMs and presets laid out beside
them -- what a ``.deb``, a Homebrew keg, a Scoop app, a checkout build and the
``beebium-server`` wheel each provide. :class:`ServerInstallation` names one, so
choosing a server is a first-class, explicit choice, not only a search order.
"""

from __future__ import annotations

import os
import shutil
import sys
import warnings
from dataclasses import dataclass, field
from pathlib import Path

from beebium.client.exceptions import ServerNotFoundError


def _warn_if_wheel_version_differs() -> None:
    """Warn (but do not fail) when the installed beebium-server wheel's version
    differs from the client's. The protocol fingerprint handshake remains the
    real compatibility guard; this only flags a likely-unintended skew."""
    import beebium.server as beebium_server  # type: ignore[import-not-found]  # already importable if we got here

    from beebium.client._version import __version__ as client_version

    server_version = getattr(beebium_server, "__version__", None)
    if server_version and server_version != client_version:
        warnings.warn(
            f"beebium-server {server_version} differs from the beebium client "
            f"{client_version}; using the wheel server anyway (the protocol "
            f"fingerprint remains the real compatibility guard).",
            UserWarning,
            stacklevel=3,
        )

# The machine variants, as the suffix of the binary name (beebium-<variant>).
VARIANTS = ("model-b", "model-b-plus", "model-b-plus-128k", "model-b-romram")
DEFAULT_VARIANT = "model-b"


def _exe_name(variant: str) -> str:
    """The binary filename for a variant (beebium-<variant>[.exe])."""
    name = f"beebium-{variant}"
    if sys.platform == "win32":
        name += ".exe"
    return name


def _is_executable(filepath: Path) -> bool:
    """Whether filepath is an executable server binary (cross-platform)."""
    if not filepath.exists():
        return False
    if sys.platform == "win32":
        return filepath.suffix.lower() in (".exe", ".cmd", ".bat", ".com")
    return os.access(filepath, os.X_OK)


def find_in_build_tree(exe_name: str, search_dirpaths: list[Path] | None = None) -> Path | None:
    """Find a server binary in a build directory at or above one or more roots.

    Each root is searched together with every one of its ancestors, so a build
    directory anywhere between the root and the filesystem root is found. Walking
    every ancestor rather than counting to a fixed depth cannot go stale when a
    directory is renamed or moved.

    With no roots given the search walks up from this module's own file, which
    resolves an editable/source checkout. A caller can instead pass other roots
    -- the pytest plugin passes pytest's rootdir, so a client installed from a
    wheel (whose __file__ is in site-packages, with no checkout above it) still
    finds the freshly-built server in the checkout under test.

    Returns None when no build directory is found, the normal case for an
    installed wheel with nothing else to search. ``exe_name`` is the full binary
    filename (e.g. ``beebium-model-b`` / ``beebium-model-b.exe``).
    """
    if search_dirpaths is None:
        search_dirpaths = [Path(__file__).resolve()]

    build_dirs = ["build", "cmake-build-debug", "cmake-build-release"]
    if sys.platform == "win32":
        build_dirs.extend(
            ["build-win-x64-release", "build-win-x64-debug", "out/build/x64-Release", "out/build/x64-Debug"]
        )

    seen: set[Path] = set()
    for start in search_dirpaths:
        for ancestor in (start, *start.parents):
            if ancestor in seen:
                continue
            seen.add(ancestor)
            for build_dir in build_dirs:
                server_dirpath = ancestor / build_dir / "src" / "server"
                if sys.platform == "win32":
                    for config in ["Release", "Debug", ""]:
                        candidate = server_dirpath / config / exe_name if config else server_dirpath / exe_name
                        if _is_executable(candidate):
                            return candidate
                else:
                    candidate = server_dirpath / exe_name
                    if _is_executable(candidate):
                        return candidate
    return None


@dataclass(frozen=True)
class ServerInstallation:
    """A located Beebium server installation: its origin and where it lives.

    Construct one explicitly with :meth:`from_executable`, :meth:`from_root`,
    :meth:`installed_wheel` or :meth:`on_path`, or let :meth:`default` apply the
    resolution order. :meth:`executable_filepath` selects a machine variant
    within it.
    """

    #: Short human label of where this came from (for describe()).
    origin: str
    #: Directory holding the beebium-<variant> binaries.
    bin_dirpath: Path
    #: Install prefix (bin/'s parent) when known; None for a bare binary.
    root_dirpath: Path | None = None
    #: The exact binary a caller named, returned as the default variant so an
    #: explicit choice runs verbatim; None when the installation is a directory.
    explicit_executable: Path | None = field(default=None)

    # -- constructors -------------------------------------------------------

    @classmethod
    def from_executable(cls, executable: str | Path, *, origin: str = "explicit executable") -> ServerInstallation:
        """An installation from one binary; the other variants are looked up
        beside it by name. rom/preset dirpaths are None (a bare binary carries
        no share/ tree)."""
        filepath = Path(executable)
        if not _is_executable(filepath):
            raise ServerNotFoundError(f"server not found or not executable: {filepath}")
        return cls(origin=origin, bin_dirpath=filepath.parent, root_dirpath=None, explicit_executable=filepath)

    @classmethod
    def from_root(cls, root: str | Path, *, origin: str = "install root") -> ServerInstallation:
        """An installation from an install prefix (containing ``bin/``) or from a
        ``bin/`` directory itself."""
        dirpath = Path(root)
        if (dirpath / "bin").is_dir():
            return cls(origin=origin, bin_dirpath=dirpath / "bin", root_dirpath=dirpath)
        if any(_is_executable(dirpath / _exe_name(v)) for v in VARIANTS):
            # A bin/ directory was passed directly; its parent is the prefix.
            return cls(origin=origin, bin_dirpath=dirpath, root_dirpath=dirpath.parent)
        raise ServerNotFoundError(f"no bin/ directory or server binaries under install root: {dirpath}")

    @classmethod
    def installed_wheel(cls) -> ServerInstallation:
        """The installed ``beebium-server`` wheel. Raises ImportError when it is
        not installed (the client never hard-depends on it)."""
        import beebium.server as beebium_server  # type: ignore[import-not-found]  # lazy: importing must never happen at module load

        root = Path(beebium_server.bundle_dirpath())
        return cls(origin="beebium-server wheel", bin_dirpath=root / "bin", root_dirpath=root)

    @classmethod
    def on_path(cls) -> ServerInstallation:
        """Whatever ``beebium-model-b`` resolves to on PATH."""
        which = shutil.which(_exe_name(DEFAULT_VARIANT))
        if which is None:
            raise ServerNotFoundError(f"{_exe_name(DEFAULT_VARIANT)} not found on PATH")
        filepath = Path(which)
        return cls(origin="PATH", bin_dirpath=filepath.parent, root_dirpath=None, explicit_executable=filepath)

    @classmethod
    def coerce(cls, value: ServerInstallation | str | Path, *, origin: str | None = None) -> ServerInstallation:
        """Turn a user-supplied ``server=`` into an installation: an existing
        directory becomes :meth:`from_root`, anything else :meth:`from_executable`."""
        if isinstance(value, ServerInstallation):
            return value
        path = Path(value)
        if path.is_dir():
            return cls.from_root(path, origin=origin or "install root")
        return cls.from_executable(path, origin=origin or "explicit executable")

    @classmethod
    def default(cls) -> ServerInstallation:
        """Resolve a server when the user was not specific, in this order:

        1. ``BEEBIUM_SERVER`` (a binary path or an install root; invalid raises)
        2. a build directory in the surrounding checkout (development only)
        3. the installed ``beebium-server`` wheel
        4. ``beebium-model-b`` on ``PATH``

        The wheel outranks PATH because it is the one installation
        version-locked to the client by construction (same tag, same
        fingerprint); PATH holds whatever the machine happens to have.
        """
        env = os.environ.get("BEEBIUM_SERVER")
        if env:
            return cls.coerce(env, origin="BEEBIUM_SERVER")

        build = find_in_build_tree(_exe_name(DEFAULT_VARIANT))
        if build is not None:
            return cls.from_executable(build, origin="checkout build")

        try:
            wheel = cls.installed_wheel()
        except ImportError:
            wheel = None
        if wheel is not None:
            _warn_if_wheel_version_differs()
            return wheel

        which = shutil.which(_exe_name(DEFAULT_VARIANT))
        if which is not None:
            return cls.on_path()

        raise ServerNotFoundError(
            "beebium-model-b not found. Set BEEBIUM_SERVER, install the beebium-server "
            "package, or add beebium-model-b to PATH."
        )

    # -- accessors ----------------------------------------------------------

    def variants(self) -> tuple[str, ...]:
        """Which of the four machine variants are present in this installation."""
        present = []
        for variant in VARIANTS:
            if variant == DEFAULT_VARIANT and self.explicit_executable is not None:
                present.append(variant)
            elif _is_executable(self.bin_dirpath / _exe_name(variant)):
                present.append(variant)
        return tuple(present)

    def executable_filepath(self, variant: str = DEFAULT_VARIANT) -> Path:
        """Path to a machine variant's binary, ensured executable.

        Raises:
            ValueError: if ``variant`` is not one of :data:`VARIANTS`.
            ServerNotFoundError: if the binary is absent from this installation
                (the message names the installation via :meth:`describe`).
        """
        if variant not in VARIANTS:
            raise ValueError(f"unknown variant {variant!r}; expected one of {', '.join(VARIANTS)}")
        if self.explicit_executable is not None and variant == DEFAULT_VARIANT:
            candidate = self.explicit_executable
        else:
            candidate = self.bin_dirpath / _exe_name(variant)
        if not candidate.exists():
            raise ServerNotFoundError(f"variant {variant!r} not found in {self.describe()}: {candidate}")
        # Belt and braces: repair a missing execute bit (idempotent) so an
        # installer that dropped it does not fail the launch. See the wheel's
        # own executable_filepath for the same guarantee.
        if sys.platform != "win32" and not os.access(candidate, os.X_OK):
            candidate.chmod(candidate.stat().st_mode | 0o111)
        return candidate

    @property
    def rom_dirpath(self) -> Path | None:
        """The bundled ROM directory (``<root>/share/beebium/roms``), or None
        for a bare binary with no share/ tree."""
        return self._share_dirpath("roms")

    @property
    def preset_dirpath(self) -> Path | None:
        """The bundled preset directory, or None for a bare binary."""
        return self._share_dirpath("presets")

    def _share_dirpath(self, name: str) -> Path | None:
        if self.root_dirpath is None:
            return None
        dirpath = self.root_dirpath / "share" / "beebium" / name
        return dirpath if dirpath.is_dir() else None

    def describe(self) -> str:
        """Origin and path, for logs and error messages."""
        location = self.explicit_executable or self.root_dirpath or self.bin_dirpath
        return f"{self.origin} ({location})"
