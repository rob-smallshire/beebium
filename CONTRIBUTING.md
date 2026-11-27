# Contributing to Beebium

Beebium welcomes contributions: fixes, new peripherals and coprocessors as
plugins, new clients, documentation and tests. This page explains how to
work with the repository so that your change integrates smoothly. The
building and testing instructions themselves are in the
[README](README.md#building-from-source) and are not repeated here.


## Talking first

For anything larger than a fix, open an issue before the branch, so that the
shape of the change can be agreed while it is still cheap to change: which
interface it touches, what it is called, what the presets and documentation
will say. Design notes live in `docs/` and `docs/discussion/`. Questions
about how something is meant to work are welcome on the issue tracker too.
Asking first can avoid duplicate or conflicting work. Don't be afraid to ask;
we welcome contributions - AI assisted or not - and have a bias towards
accepting them.


## Start with a fork, not with a finished tree

Begin in a fork of this repository early on, and try to keep it close to
upstream while you work, rather than developing elsewhere and bringing the
result over at the end. Integration problems are cheap to fix one at a time
as they arise and expensive to fix all at once: a tree developed apart for
months meets every change of interface, every new test rule and every
platform difference on the day it is merged.

## Set up the fork

Fork the repository on GitHub, clone your fork, and add this repository as
the `upstream` remote:

```bash
git clone https://github.com/<you>/beebium.git
cd beebium
git remote add upstream https://github.com/rob-smallshire/beebium.git
git fetch upstream
./scripts/install-hooks
```

Treat your fork's `master` as a mirror of upstream's: avoid commit to it directly.
Bring it up to date with a fast-forward only, so that it can never diverge:

```bash
git fetch upstream
git switch master
git merge --ff-only upstream/master
git push origin master
```

If `--ff-only` refuses, something was committed to your `master`. Move that
work to a branch (`git branch rescue-work` before anything else), then reset
`master` to `upstream/master`.

## Work on a branch

Every change, however small, lives on its own branch cut from the current
upstream `master`:

```bash
git fetch upstream
git switch -c tube-z80 upstream/master
```

Name the branch for the change (`tube-z80`, `fix-1770-index-pulse`,
`linux-client-cassette-counter`). Commit as you go. Build and run the
targeted tests for the area you are changing after each step, and the full
suite before you ask for review.

Keep the branch current with upstream while you work. When you're about to commence a session, or
whenever upstream `master` moves under something you depend on, rebase:

```bash
git fetch upstream
git rebase upstream/master
git push --force-with-lease origin tube-z80
```

Rebasing keeps your commits as a clean sequence on top of the latest master,
and surfaces conflicts while they are small and while you remember the code. Use
`--force-with-lease`, never bare `--force`, so that a push cannot overwrite
commits you have not seen. Do not rebase or force-push a branch that someone
else is also committing to without agreeing it first.

Several branches can be in flight at once, each cut from `upstream/master`,
each independent. Do not stack one unmerged branch on another unless the
second genuinely needs the first; stacked branches have to be rebased in
order every time the lower one changes.

## Open the pull request early

Open the pull request as a draft as soon as the branch builds, not when it
is finished. A draft runs the same continuous integration as a finished pull
request, on every platform Beebium ships for: macOS on Apple Silicon and
Intel, Linux on amd64 and arm64, and Windows, with the C++ tests, the Python
client's integration tests against the built servers, and the packaging of
the release artefacts. Most contributors have one of these platforms; the
pull request gives them all five. A maintainer has to approve the first
workflow run from a new fork, so the first run may wait a little.

A pull request that stays green while it grows is easy to review and merge.
One that arrives red on three platforms is not. When a check fails, read the
job's log on the pull request's Checks tab: the failure is usually in the
last hundred lines.

Prefer several small pull requests to one large one. A new coprocessor can
land as the core and its unit tests first, then the plugin and its manifest,
then the presets and documentation. Each is reviewable in an hour; the whole
is not.

Describe in the pull request what the change does and why, what you tested
and on which platform, and anything you were unsure of. Link the issue it
addresses. Pull requests are merged with a merge commit by the maintainer;
there is no need to squash, and a readable commit history on the branch is
welcome.

## What the repository expects

These are the rules that continuous integration or review will hold a change
to. [CLAUDE.md](CLAUDE.md) has more detail on the architecture and the
working principles.

**Refactor in preparation.** Rather than cramming features in where the don't
really fit, refactor in preparation for new features. Make the change easy,
then make the easy change.

**Tests first.** We test to follow a test-led or test-driven approach. Write
a failing test before the code that makes it pass.
C++ tests are Catch2 under `tests/` or beside a plugin; Python integration
tests are under `clients/beebium-python-client/tests` and run against a
built server; the TypeScript client has its own suite.

**Python test module names are unique across the whole repository.** The
test folders have no `__init__.py`, so pytest imports each file by its bare
name, and two `test_debugger.py` files in different folders stop the whole
collection. Give a subsystem's tests a prefix, as `tests/aes/test_aes_*.py`
do. Shared helpers go in a uniquely named module, never imported from a
`conftest.py`.

**Catch2 test names and tags are 7-bit ASCII** and do not start with `--`,
because Windows CTest discovery matches them through the console code page
and the command line. Everything else, comments, literals and documentation,
may use any UTF-8.

**Names.** Variables, functions and constants holding paths end in
`_filename`, `_filepath`, `_dirpath` or `_dirname`, never the ambiguous
`_file` or `_dir`. C++ is C++20, the 6502 library is C11, the macOS client
is Swift, the Linux client is Qt 6 C++, and the test clients are Python and
TypeScript. File extensions are `.h` and `.c` for C, `.hpp` and `.cpp` for
C++.

**Errors are sentences.** Text a user may see says what happened and what to
do, as a sentence, not a code or a fragment. If errors are actionable by the
user tell them what to do. If errors are not actionable by the user, admit as
much.

**The server is the single source of truth.** Clients display and control
what the server reports; they do not keep their own model of the machine.

**No host identities.** Nothing committed, and nothing written on a pull
request or an issue, names your machines, your local paths, your network
addresses or your private domains. Use placeholders.

**Commit messages** are a sentence in the imperative, under about seventy
characters, saying what the change does ("Refuse a ROM path that is not a
regular file before reading it"), followed if useful by a paragraph on why.
No emoji. Plain ASCII. Mentioning that an AI assistant helped is fine and
needs no particular form.

## Copyright and licence

Beebium is licensed under the GNU General Public License, version 3 or any
later version ([COPYING.txt](COPYING.txt)). You keep the copyright in what
you write; contributing does not assign it. Each source file carries a
copyright line for each person who holds copyright in it and the GPL
notice. A file you create carries your line:

```cpp
// Copyright 2026 Ada Lovelace
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. ...
```

A file you change keeps every existing copyright line, and you may add
yours. A subsystem contributed whole from a project of its own may keep that
project's notice naming it, as the AES coprocessor's files say "This file is
part of AESulator, a plugin for Beebium". The pre-commit hook that
`scripts/install-hooks` installs checks these notices; it knows the
contributors by name, so the first pull request from a new contributor adds
their name to the list at the top of `scripts/pre-commit`. The hook will
never rewrite another contributor's line.

## Plugins

Peripherals, Econet transports and Tube coprocessors are plugins under
`src/extensions/`, loaded by manifest. A new coprocessor follows
[docs/coprocessor-extension-guide.md](docs/coprocessor-extension-guide.md),
which uses the AES as its worked example of a non-6502 processor; a
peripheral follows
[docs/peripheral-extension-framework.md](docs/peripheral-extension-framework.md).
A plugin's tests run under ctest beside the tree's own, and its Python
tests live in the Python client's suite, with a prefix, so that one command
runs everything. Do not keep a separate test harness or build script for a
plugin; CI will not run it.
