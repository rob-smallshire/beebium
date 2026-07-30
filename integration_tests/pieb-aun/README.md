# PiEconetBridge AUN interop tests

Drives a Beebium station against a real, unmodified
[PiEconetBridge](https://github.com/cr12925/PiEconetBridge) over AUN — with no
Raspberry Pi, no Econet HAT, and no kernel module.

The bridge's `-l` switch selects IP-only operation, and a configuration with no
`WIRE NET` line yields what upstream calls Null networks: ones carrying local
emulation and distant AUN hosts only. Its emulated fileserver initialises its
own filestore, including a privileged `SYST` user with a blank password, so
each run starts from an empty directory.

Design and the wider test programme:
`docs/discussion/pieconetbridge-aun-interop-testing.md`.
Defects these tests target: `docs/discussion/aun-robustness.md`.

## Running

```bash
cd integration_tests/pieb-aun
uv run pytest -m slow -v
```

Tests are skipped unless `-m slow` is given. A Beebium server must be built,
and either Docker must be available or a natively-built `econet-hpbridge` must
be findable.

Useful when something fails:

```bash
# Beebium's own AUN packet trace, teed to the test output
BEEBIUM_AUN_TRACE=1 BEEBIUM_SERVER_STDERR=1 uv run pytest -m slow -s
```

The bridge's log is printed automatically whenever a test that used it fails.

## Environment

| Variable | Effect |
|---|---|
| `BEEBIUM_SERVER` | Path to `beebium-model-b`. Otherwise `build-release/`, `build/` and `cmake-build-debug/` are searched, in that order. |
| `BEEBIUM_ROM_DIR` | ROM directory. Defaults to `roms/`. |
| `BEEBIUM_PIEB_FLAVOUR` | `native` or `container`, forcing the choice. CI uses this to exercise the container path on a machine that also has a native binary. |
| `BEEBIUM_PIEB_BIN` | Path to a built `econet-hpbridge` (Linux). |
| `BEEBIUM_PIEB_SRC` | A PiEconetBridge checkout whose `utilities/econet-hpbridge` has been built (Linux). |
| `PIEB_DEBUG` | Passed to the container: raises the bridge's debug level further. |

## Platform notes

**Linux** uses `--network host`, or a native binary if one is configured. There
is no NAT in the path, so the bridge is given a static `AUN MAP HOST` entry and
our station identity is pinned by configuration. This is the stricter
arrangement and the one CI should run.

**macOS and Windows** run the bridge under Docker Desktop, whose published-port
forwarder rewrites both the source address and the source port of
host-to-container UDP — measured, not assumed. Since AUN peers are matched by
`(address, port)` at both ends, no static entry can match, so the fixture adds
upstream's `DYNAMIC <net>` instead and lets the bridge allocate us a station on
first contact. Tests are unaffected; the cost is that our station number is
bridge-assigned rather than declared.

## Layout

- `src/pieb_test_support/bridge.py` — the `Bridge` abstraction and its native
  and container implementations, config rendering, and the readiness and
  address-discovery logic.
- `src/pieb_test_support/topology.py` — the Econet addressing the fixtures and
  tests share, and why the numbers are what they are.
- `tests/conftest.py` — ROM, server and bridge fixtures.
- `docker/pieconetbridge/` (repo root) — the container image, pinned to an
  upstream commit.
