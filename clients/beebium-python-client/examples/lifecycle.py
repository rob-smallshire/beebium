"""Lifecycle: obtain a client, run emulated time, read the machine identity.

Illustrates Beebium.connect / Beebium.launch (via the shared helper), the
emulated-time run helpers on the client, and SystemService's machine identity,
including the machine's name template and the placeholders it can use.
"""

from __future__ import annotations

from _demo import run

from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    ident = bbc.system.identity
    print(f"model   : {ident.model_name} ({ident.model_type})")
    print(f"name    : {ident.name}")
    print(f"template: {ident.name_template}")
    print(f"uuid    : {ident.uuid}")
    print(f"clock   : {bbc.system.clock_speed_hz / 1e6:.2f} MHz")
    print(f"clients : {bbc.system.client_count}")

    # The placeholders a name template can use come from the server, each with
    # its current value; the client knows none of its own.
    print("\nname placeholders:")
    for p in bbc.system.list_name_placeholders():
        value = repr(p.value) if p.applicable else "(not applicable)"
        print(f"  [{p.group}] {p.insertion:<20} {p.label}: {value}")

    # Render a template without renaming the machine.
    preview = bbc.system.preview_machine_name(f"{ident.name_template} {{no-such-key}}")
    print(f"preview : {preview.name!r} (unknown keys: {list(preview.report.unknown_keys)})")

    # Advance a fixed slice of *emulated* time (independent of the wall clock).
    bbc.debugger.ensure_stopped()
    start = bbc.debugger.cycle_count
    bbc.run_for_emulated_seconds(0.5)
    print(f"\nran {bbc.debugger.cycle_count - start} cycles (~0.5s emulated)")

    # Or run until a predicate holds, bounded by an emulated-time budget.
    in_os = bbc.run_until_or_timeout(lambda: bbc.cpu.pc >= 0xC000, emulated_seconds=1.0)
    print(f"PC reached the OS ROM: {in_os} (PC=${bbc.cpu.pc:04X})")


if __name__ == "__main__":
    run(demo, description="Client lifecycle, emulated-time helpers, identity.")
