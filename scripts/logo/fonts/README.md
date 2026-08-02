# Vendored fonts

These faces are the reproducible defaults for the icon generator.  Both are
under the SIL Open Font License 1.1, which permits redistribution, so the icons
can be regenerated identically on any machine and in CI.

| File                 | Family      | Role                    | Licence               |
|----------------------|-------------|-------------------------|-----------------------|
| `NunitoSans.ttf`     | Nunito Sans | `symbol`, `name`        | `NunitoSans-OFL.txt`  |
| `Barlow-Medium.ttf`  | Barlow      | `technical`             | `Barlow-OFL.txt`      |
| `Barlow-SemiBold.ttf`| Barlow      | `technical`, if wanted  | `Barlow-OFL.txt`      |

Nunito Sans is a variable font; the configuration instances it per role, at
weight 1000 for the symbol and 700 for the element name.  It stands in for
Avenir Next, and Barlow for DIN, which are the faces the design was drawn for
but which may not be redistributed.  See `configs/beebium-avenir-din.toml`.
