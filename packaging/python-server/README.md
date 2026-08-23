# beebium-server

Prebuilt [Beebium](https://github.com/rob-smallshire/beebium) BBC Micro emulator
server binaries, shipped as a platform wheel.

```
pip install beebium beebium-server
```

The `beebium` Python client then launches a server from this package and drives
it over gRPC, with the ROMs, presets and extensions it needs -- no environment
variables, no ROM files located by hand.

```python
import beebium.server
beebium.server.executable_filepath()   # the model-b binary, ensured executable
beebium.server.variants()              # the four machine variants
```

Installing this package also puts `beebium-model-b` (and the three other
variants) on `PATH`.

This wheel is a repackaging of the release bundle that the Beebium monorepo
already builds and validates; the server binaries are byte-identical to those on
the GitHub Release. It has no Python dependencies and does not depend on
`beebium`.
