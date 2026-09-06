# Contributing to the Beebium Python client

Development of the whole project happens in the
[Beebium repository](https://github.com/rob-smallshire/beebium); this directory,
`clients/beebium-python-client`, holds the `beebium` PyPI package.

## Working from a checkout (uv)

This package is a [uv](https://docs.astral.sh/uv/) project (`pyproject.toml` +
`uv.lock`). `uv` manages an isolated environment for you, so it works on
distributions that enforce [PEP 668](https://peps.python.org/pep-0668/) (Arch,
recent Debian/Ubuntu, Fedora) where a system-wide `pip install` is blocked.

`beebium` is a library here, not an application: contributors run it from this
checkout with `uv`, and end users install the published wheel. The `uv.lock` is
a development convenience, not something users depend on.

```bash
cd clients/beebium-python-client

# Run anything in the project environment (uv creates it on first use):
uv run python examples/serial_demo.py --port 50071

# Run the test suite. Development tooling (pytest, grpcio-tools, jinja2) lives
# in the `dev` dependency-group, which uv installs by default -- no extra flags:
uv run python -m pytest

# Or materialise a .venv you can activate (the dev group is included):
uv sync
```

No `pip` is required, and nothing is installed into your system Python.

### Plain virtualenv

PEP 668 only blocks the *system* environment, so `pip` works inside a virtualenv:

```bash
cd clients/beebium-python-client
python -m venv .venv
. .venv/bin/activate
pip install -e .            # the client
pip install --group dev     # dev tooling (pip >= 25.1); or just use uv
```

## Test against the built wheel

Tests must run against code installed from the built wheel, never against
`src/`, so that missing package data, an unshipped `py.typed` marker, namespace
misconfiguration or an unregistered entry point are caught rather than passed
over by a source-tree run:

```bash
# With no arguments only the packaging assertions in tests_packaging/ run
# (no server needed); pass pytest arguments to run other tests against the wheel:
bash scripts/test-wheel.sh
bash scripts/test-wheel.sh tests/ readme/snippets/ -q
```

The plugin finds `roms/` and the checkout's server build from pytest's rootdir,
so no environment variables are needed in-repo. `BEEBIUM_ROM_DIR` /
`BEEBIUM_SERVER` override that discovery. Server-backed tests skip if no server
is available.

## The README is generated -- do not edit README.md

`README.md` is generated so its code examples cannot drift from working code.
Do **not** edit `README.md` by hand. Instead:

- Prose lives in `readme/README.md.j2` (a Jinja2 template).
- Every Usage code example is a **real, executable test** under
  `readme/snippets/`, named `test_readme_<name>.py` (the `test_readme_` prefix
  keeps their basenames from colliding with `tests/` when pytest collects both
  together). Each snippet file marks the region that appears in the README with
  `# readme:begin` / `# readme:end`; the generator splices that region in
  verbatim (dedented, marker lines removed) -- so what the README shows is
  exactly what runs. Keep assertions or fixtures a snippet should not display
  outside the marked region.
- The repo-root `scripts/generate_readme.py` renders the template into
  `README.md`. One generator serves every generated README (this client and the
  `beebium-server` wheel); it takes the package directory as an argument.

After editing the template or a snippet, regenerate and run the snippet tests:

```bash
uv run python ../../scripts/generate_readme.py .   # regenerate this README.md
bash scripts/test-wheel.sh readme/snippets/ -q     # prove the examples run
```

CI enforces both: `scripts/check_readme_regen.sh` fails if the committed
`README.md` differs from a fresh render, and the snippet tests run against the
built wheel on every push. `git grep -n TODO` before you push.

## Proto stubs

The generated gRPC stubs under `src/beebium/client/_proto` (and
`src/beebium/ext`) are produced by `scripts/generate_proto.sh` and checked by
`scripts/check_proto_regen.sh`. Regenerate them after changing a `.proto`.

## License

By contributing you agree that your contributions are licensed under
GPL-3.0-or-later, in line with the rest of the project.
