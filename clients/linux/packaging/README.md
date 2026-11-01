# Linux client packaging files

What the `beebium-client` package installs beside the program (see the
install rules and the CPack stanza at the end of `../CMakeLists.txt`, and
`docs/packaging.md`):

| File | Installed as |
|------|--------------|
| `org.beebium.Beebium.desktop` | `/usr/share/applications/` |
| `org.beebium.Beebium.xml` | `/usr/share/mime/packages/` (the machine document and preset file types) |
| `org.beebium.Beebium.metainfo.xml` | `/usr/share/metainfo/` |
| `icons/hicolor/<size>/apps/org.beebium.Beebium.png` | `/usr/share/icons/hicolor/` |

The icons are "The Shape of Beebium", the artwork every Beebium client uses,
cut to the hicolor sizes by the icon generator in `scripts/logo`; they are its
output, so regenerate rather than edit them:

```bash
cd scripts/logo && uv run beebium-icon build -o /tmp/beebium-icons linux
# then copy /tmp/beebium-icons/linux/hicolor/<size>/apps/beebium.png to
# icons/hicolor/<size>/apps/org.beebium.Beebium.png here
```

The artwork is a raster image, so there is no scalable SVG icon.
