# AUN tools

## Converting BeebEm's Econet.cfg and AUNMap

`convert_beebem_econet_cfg.py` turns a BeebEm `Econet.cfg`, and optionally its
`AUNMap`, into a Beebium `aun-map.json`
(`docs/discussion/aun-peer-map-file.md`, sections 2.2 and 2.5). Beebium reads
no BeebEm format itself, so this is a one-time conversion. Standard library
only:

```bash
cd tools/aun
uv run convert_beebem_econet_cfg.py ~/BeebEm/Econet.cfg --aunmap ~/BeebEm/AUNMap
```

| Option | Effect |
|--------|--------|
| `--aunmap PATH` | Also convert BeebEm's AUNMap. |
| `--output PATH` | The file to write. Default: `aun-map.json` in the current directory. |
| `--merge` | Fold into the existing `--output` instead of replacing it. Its entries, their order and any keys this tool does not know are kept. On a duplicate peer `(net, station)` or subnet net, the existing entry wins. |
| `--replace` | With `--merge`, converted entries overwrite duplicates instead. |
| `--dry-run` | Print the result to stdout and write nothing. |

The output is pretty-printed with a stable key order and written atomically.
A summary goes to stderr: every entry taken, replaced or skipped as a
duplicate, and everything that was not converted.

### What is converted

The files are read as BeebEm reads them (`Econet.cpp`, `ReadEconetConfigFile`
and `ReadAUNConfigFile`): lines are trimmed; empty lines and lines starting
with `#` or `|` are skipped; everything from the first `#` is removed, and
from `//` in Econet.cfg; the rest is split on whitespace.

- **Host lines**, exactly four fields `net stn ip port`, become `peers`
  entries. A trailing `#` comment becomes the entry's `label`. A host must fit
  aun-map.json (net 0..127, station 1..254, port 1..65535, an IPv4 address as
  BeebEm requires); anything else is reported as invalid. If a station appears
  twice, the first line wins, as it does in BeebEm.
- **`ADDMAP a.b.c.d N`** lines, three fields in any case, become `subnets`
  entries. BeebEm keeps only the first three octets of the address, so the
  subnet is `a.b.c.0/24`, and masks the net with 255 when Econet.cfg sets
  `MASSAGENETS` and 127 when it does not. ADDMAP lines in Econet.cfg are
  converted too and noted, though BeebEm itself reads them only from AUNMap.
- **Same-host peers** (`127.0.0.1`) are converted as written and listed in the
  summary: Beebium routes same-host peers itself.

### What is not

- **Keywords** (two fields): AUNMODE, LEARN, AUNSTRICT, SINGLESOCKET,
  MASSAGENETS, FLAGFILLTIMEOUT, SCACKTIMEOUT, TIMEBETWEENBYTES and
  FOURWAYTIMEOUT describe BeebEm's emulation, not the network. Each is listed
  with a sentence on why it is not carried over; any other keyword is listed
  as unknown.
- **Lines with the wrong number of fields** are listed as ignored, with their
  line numbers. BeebEm ignores them too.
- **Commented-out host lines.** BeebEm's shipped Econet.cfg lists its example
  hosts only as comments, so converting it unedited gives an empty `peers`
  list. The summary lists every commented-out host line; uncomment the ones
  you use in Econet.cfg and convert again.

### Tests

```bash
cd tools/aun
uv run pytest
```

The fixtures include BeebEm's shipped sample `Econet.cfg` and `AUNMap`
(BeebEm is GPL, as Beebium is).
