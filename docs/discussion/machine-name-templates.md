# Machine names as templates

Status: server and client libraries built (2026-10-03, issue #153); the
macOS rename popover (section 8) is still to come. Decisions marked (user)
come from the user's comments on the issue. This document describes the
design as built; where building it settled a point the first draft left open
or had wrong, the section says so.

## 1. Problem

Launch three "Station 80 (AUN, Model B)" machines and renumber two of
them: each window title still says "Station 80" until it is renamed by
hand. A name that describes mutable state should follow that state.

## 2. Model

A machine has a **name template** and a **rendered name**.

- The template is what the user edits and what presets, the command line
  and saved machines store. It is ordinary text containing placeholders:
  `Station {econet-station} (AUN, Model B)`.
- The rendered name is the template with each placeholder replaced by its
  current value. It is what window titles, `SystemInfo`, status events
  and the `_beebium._tcp` announcement carry. Nothing else changes for
  consumers of the name: `MachineIdentity.name` remains "the name to
  show".
- A template with no placeholders is a plain name and behaves exactly as
  names do today.

The **server** owns placeholders: their set, their meaning and their
values. A client never needs to know which placeholders exist (user); it
asks.

## 3. Template syntax

- `{key}` is a placeholder. Keys are lowercase ASCII words joined by
  hyphens, and every key begins with the domain that owns it:
  `econet-station`, `machine-model`. There are no bare keys such as
  `station` or `transport`: a general word claims more than it means, and
  a second kind of transport or station later would have nowhere to go
  (user).
- `{{` and `}}` are literal braces.
- An unknown key renders verbatim, braces included, and is reported by
  validation as unknown. So a template written for a newer server, or
  for an extension that is not loaded, degrades visibly and loses nothing.
  An empty `{}` and a brace pair whose content breaks the key grammar are
  unknown keys too.
- Malformed text renders literally and is reported separately from unknown
  keys: a `{` with no `}` before the next `{` or the end (the `{` and the text
  up to that point), and a lone `}` not part of `}}`. So `{a{econet-station}}`
  renders `{a` literally, the placeholder, and a literal `}`.
- Inside braces the characters `:`, `|`, `?` and `!` are reserved for
  future use (formatting, fallbacks, conditional text). Today a
  placeholder containing one is treated as unknown. Reserving them now is
  what lets the syntax grow without breaking existing templates.
- A placeholder that is known but **not applicable** to this machine (an
  Econet placeholder on a machine with no Econet fitted) renders as the
  empty string, and the picker shows it as not applicable.
- A value is inserted as it is, never parsed again, so a value containing
  braces cannot inject a placeholder.
- Rendering is a single left-to-right pass, linear in the template's length.
- A template that renders blank (`{econet-station}` with no Econet fitted)
  gives the model's display name instead: a machine always has a name to
  show and to announce.

## 4. Placeholders are data, provided by their owners

A placeholder is described by a record, not by code in clients:

| Field | Meaning |
|-------|---------|
| `key` | Stable identifier used in templates. Never renamed once shipped. |
| `label` | Short human name for a picker: "Econet station". |
| `description` | One sentence on what it shows and when it changes. |
| `group` | Heading for a picker: "Machine", "Econet". |
| `insertion` | The text a picker inserts into a template: `{key}`. Carried so that front ends need not build it (section 8). |
| `value` | Current value on this machine, as text; empty when not applicable. |
| `applicable` | False when this machine cannot have a value for it. |

Placeholders come from **providers**: the core machine, the Econet
socket, and any extension (a transport, a peripheral) through one method
on the extension API. The server's registry is the union. Adding a
placeholder is adding a provider entry: no protocol change, no client
change, no template migration. The domain prefix is a rule, not a
convention: the registry rejects a provider's key that does not start
with one of the provider's declared domains (`machine-`, `econet-`,
`scsi-`, ...), so a key says whose state it shows. A domain is a single
lowercase word and belongs to exactly one provider; that exclusivity, not
the prefix alone, is what keeps two providers from colliding. A provider that
breaks a rule is refused whole: a plugin loses its placeholders, the machine
still launches.

A placeholder earns its place only if its value **can change while the
template stays the same**, or differs between machines launched from one
preset. Static facts belong in the template as literal text: a preset
writes "(AUN, Model B)" itself.

## 5. The first set

Deliberately small. Each is state a user can change, or that the
machine chooses for itself.

| Key | Group | Value | Changes when |
|-----|-------|-------|--------------|
| `econet-station` | Econet | The station number **in force** (user): the number the guest read from the station links (`&FE18`) on its **first** read since the last reset, or since Econet was fitted -- the read with which a filing system takes its number at boot, and so the number it is using. Before that read it is the configured number. Later reads in the same boot do not count: the NFS and ANFS read `&FE18` as INTOFF on every Econet NMI, and counting those would show a renumber before the Break that puts it in force. | The guest's first read after a Break, after a change in the sidebar or by `SetStationId`; or at launch under `--station auto`. |
| `econet-net` | Econet | This machine's Econet net number (0 for the local net). | Launch configuration. |
| `econet-transport` | Econet | The transport's display name, the label its sidebar panel carries ("AUN", "Piconet"); empty when Econet is fitted with no transport. | Econet enabled or disabled at runtime. |
| `machine-model` | Machine | The machine model's display name, as the server reports it in `SystemInfo` ("BBC Model B"): one name for a model. | Never; included because it differs between machines sharing a hand-written template, and costs nothing. |
| `machine-preset` | Machine | The name of the preset the machine was launched from. Not applicable, so empty, when it was launched without one. | Never; as above. |

Not included, with reasons:

- **The launcher's ordinal** ("#2"). It is the launcher's state, not the
  machine's. The app writes it into the template as literal text at
  launch (section 8), so no placeholder and no client-to-server variable
  mechanism is needed.
- **Host, gRPC port, UUID.** Useful for remote and multi-host work, but
  nobody has asked; the registry makes them a later, isolated addition.
- **Disc titles, ROM names, coprocessor.** Plausible; each is one provider
  entry when wanted.

## 6. Rendering and change

- The server re-renders once a second (user: "every second or two would be
  fine") on a thread of its own, never the emulation thread, reading
  provider values through paths that are safe from another thread
  (`econet-station` is two relaxed atomics the emulation thread writes on its
  `&FE18` read, with no lock on that path). No provider is asked to push
  changes; polling at this rate is cheap and keeps providers trivial to
  write.
- When the rendered name changes, the server emits the identity change on
  `WatchServerStatus` exactly as a rename does, and re-publishes the
  `_beebium._tcp` announcement, at most once every five seconds -- long
  enough to cover a registration's probe and announcement burst -- so a
  flapping value cannot churn mDNS (see the responder saturation note in
  networking.md). Changes within the interval coalesce into one
  re-announcement of the latest name. A user's rename is deliberate and rare,
  and is re-announced at once.
- Rendering is total: it never fails. Unknown, malformed and inapplicable
  placeholders render as section 3 says.

## 7. Protocol surface

`system.proto` (fingerprinted: this is a minor version bump).

- `MachineIdentity` gains `name_template`. `name` stays the rendered name.
- `SetMachineName` takes the template (a plain name is a template; an empty
  one is refused). Its response carries the identity with both fields and a
  `NameTemplateReport`: the unknown keys, the inapplicable keys and the
  malformed fragments in the template, so a client can warn without
  parsing. The template is applied even when parts of it do not render;
  there is no strict mode, and the report goes only to the caller -- a
  second client watching the status stream that wants one calls
  `PreviewMachineName`.
- `ListNamePlaceholders` returns the records of section 4 with current
  values.
- `PreviewMachineName(template)` returns the rendering and the same
  `NameTemplateReport`, without changing anything. Clients use it for live
  preview instead of implementing the syntax.

Python and TypeScript expose the template, the rendered name and the
placeholder list (user), plus preview. Neither client carries a list of
placeholder keys.

Command line and presets: `--machine-name` and a preset's `machine_name`
are templates. A `list-name-placeholders` subcommand prints the registry
for a model (keys, labels, descriptions; values where they are known
before launch), for people writing presets.

## 8. Front end (macOS first; the rule is platform-independent)

- **Rename shows the template, not the rendering** (user). Beside the
  text field, a picker lists the placeholders the server reports, grouped,
  each with its label and current value; choosing one inserts `{key}` at
  the caret. Under the field, a live preview of the rendered name from
  `PreviewMachineName`, and a note naming any unknown keys.
- The picker, labels, groups and values all come from the server. The
  front end's only knowledge is the `{key}` insertion form, which it
  takes from the placeholder record rather than building, so even that
  can change server-side (the record carries the insertion text).
- **Launch.** The app builds the machine's template from the preset's
  `machine_name` if it has one, otherwise the preset's display name with
  braces escaped, and appends its per-launch ordinal as literal text:
  `Station {econet-station} (AUN, Model B) #2`.
- Window titles, the Window menu and the connection registry use the
  rendered name from the status stream, as now.

## 9. Presets

Built-in presets set `machine_name` only where a placeholder helps:

- `Station 80 (AUN, Model B)` and `Station 81 (Piconet, Model B)`:
  `Station {econet-station} (AUN, Model B)` and the Piconet equivalent.
- `AUN client, auto station (Model B)`: `Station {econet-station} (AUN,
  Model B)`, replacing today's "AUN client".
- `Station 254 (L3 File Server, Model B)`: `L3 File Server, station
  {econet-station}`.
- All others: no `machine_name`; the preset's display name is used.

A preset's own `name`, shown in the picker, stays literal: there is no
machine to render against.

## 10. Saved machines

`machine.json` (docs/discussion/presets-and-machine-directories.md,
section 4.5) stores the template. The Welcome window's Machines page
shows the name as last rendered, stored alongside it at save and
shutdown, since no server is running to render it.

## 11. Extensibility checklist

- New placeholder: one provider entry. No proto, client or template
  change.
- New formatting or fallback syntax: uses a reserved character; old
  templates are unaffected and old servers render the new form verbatim.
- New front end: needs no placeholder knowledge; it lists, inserts,
  previews.
- Placeholder removed or extension not loaded: renders verbatim and is
  reported as unknown.

## 12. Settled points

- A not-applicable placeholder renders as empty rather than a visible
  marker: empty reads better in titles, and the picker and the report make
  the cause discoverable.
- `machine-model` is the full display name `SystemInfo` reports ("BBC Model
  B"), so there is one name for a model.
