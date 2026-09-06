# Contributing to beebium-server

Development of the whole project happens in the
[Beebium repository](https://github.com/rob-smallshire/beebium); this directory,
`packaging/python-server`, holds the `beebium-server` PyPI package.

## How this wheel is built

This wheel is **not** compiled from source here. `build_wheel.py` repackages a
release bundle (a `.tar.gz` / `.zip` the monorepo already builds and validates)
into a platform wheel:

```bash
python build_wheel.py beebium-server-<version>-macos-arm64.tar.gz macosx_11_0_arm64
```

It unpacks the bundle verbatim into `src/beebium/server/_bundle/` (the gitignored
`bin/ lib/ share/` payload) and runs `uv build --wheel` with the platform tag
set, so the server binaries are byte-identical to those on the GitHub Release.
The five platform wheels are built and published from the release workflow.

## The README is generated -- do not edit README.md

`README.md` (the PyPI long description) is generated so its code examples cannot
drift from working code. Do **not** edit `README.md` by hand. Instead:

- Prose lives in `readme/README.md.j2` (a Jinja2 template).
- The Python Usage example is a **real, executable test** under
  `readme/snippets/`, named `test_readme_<name>.py`; it marks the region shown
  in the README with `# readme:begin` / `# readme:end`, and the generator
  splices that region in verbatim (dedented, marker lines removed) -- so what
  the README shows is exactly what runs. Keep assertions outside the marked
  region. The shell example is prose in the template, pinned by
  `test_readme_cli.py`, which runs the console script and asserts on its real
  output.
- The repo-root `scripts/generate_readme.py` (one generator, shared with the
  `beebium` client) renders the template into `README.md`; it takes this package
  directory as its argument.

Because generation needs jinja2, run it from the client's uv dev environment:

```bash
# from clients/beebium-python-client:
uv run python ../../scripts/generate_readme.py ../../packaging/python-server
```

CI enforces both halves. `clients/beebium-python-client/scripts/check_readme_regen.sh`
re-renders every generated README and fails if a committed file drifted, and the
snippet tests run against the installed wheel on the verify-server-wheels legs.

Requires-python here is `>=3.9`, so any code shown in the README (and the tested
snippet regions) must use 3.9-compatible syntax.

## License

By contributing you agree that your contributions are licensed under
GPL-3.0-or-later, in line with the rest of the project.
