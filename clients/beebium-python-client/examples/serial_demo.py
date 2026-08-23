"""Demonstrate the BBC Micro serial port (MC6850 ACIA + Serial ULA).

  Drives the serial port end to end via the rpc-serial extension, where the
  script is the device on the far end of the wire (no PTY or real FujiNet):

    1. Prints a live status snapshot of the ACIA and Serial ULA registers.
    2. device -> Beeb: injects bytes for the BBC to receive.
    3. Beeb -> device: the BBC transmits and the script collects the bytes.

  The server must own the serial port with the rpc-serial extension
  (--rpc-serial). Two ways to run it:

    # (a) Attach to an emulator you started yourself:
    beebium-model-b start --mos roms/acorn-mos_1_20.rom --rpc-serial --port 50071
    python examples/serial_demo.py --port 50071

    # (b) Let the demo launch (and stop) its own --rpc-serial server:
    python examples/serial_demo.py --server ../../build/src/server/beebium-model-b \
                                   --mos ../../roms/acorn-mos_1_20.rom

  With no --port, the demo self-launches; --mos defaults to
  <repo>/roms/acorn-mos_1_20.rom when that file exists.
  """

from __future__ import annotations

import argparse
from pathlib import Path

from beebium.client import Beebium
from beebium.ext.peripheral.rpc_serial import RpcSerial

# MC6850 control: /16 divide, 8N1, /RTS low, no TX IRQ -- the MOS serial config.
ACIA_8N1 = 0x15
# Serial ULA: RS423 select (carrier present), 19200 baud on TX and RX.
ULA_RS423_19200 = 0x40

ACIA_STATUS = 0xFE08
ACIA_DATA = 0xFE09
ULA_CONTROL = 0xFE10

SR_RDRF = 0x01
SR_TDRE = 0x02


def program_serial(bbc: Beebium):
    """Poke the ACIA + Serial ULA into the standard 19200 8N1 configuration."""
    bbc.memory.address.bus[ACIA_STATUS] = ACIA_8N1
    bbc.memory.address.bus[ULA_CONTROL] = ULA_RS423_19200


def _wait_for(bbc: Beebium, mask: int, budget: int = 8000, chunk: int = 100) -> bool:
    """Step the machine until a status bit is set (or the cycle budget runs out)."""
    stepped = 0
    while stepped < budget:
        if bbc.memory.address.peek[ACIA_STATUS] & mask:
            return True
        bbc.debugger.step_cycles(chunk)
        stepped += chunk
    return bbc.memory.address.peek[ACIA_STATUS] & mask != 0


def transmit_byte(bbc: Beebium, byte: int) -> None:
    """Send one byte, busy-waiting for TDRE first (exactly as fn-rom does)."""
    _wait_for(bbc, SR_TDRE)
    bbc.memory.address.bus[ACIA_DATA] = byte


def read_byte(bbc: Beebium) -> int | None:
    """Wait for a received byte (RDRF) and read it; None if none arrives."""
    if not _wait_for(bbc, SR_RDRF):
        return None
    return bbc.memory.address.bus[ACIA_DATA]


def receive_only(bbc: Beebium, count: int) -> bytes:
    """Read up to `count` bytes the BBC receives (one RDRF poll per byte)."""
    out = bytearray()
    for _ in range(count):
        received = read_byte(bbc)
        if received is None:
            break
        out.append(received)
    return bytes(out)


def print_status(bbc: Beebium):
    s = bbc.serial.status
    print("  Serial status:")
    print(f"    ACIA control/status: ${s.acia_control:02X} / ${s.acia_status:02X}")
    print(f"    TDRE={s.tdre}  RDRF={s.rdrf}  /DCD={s.not_dcd}  IRQ={s.irq_pending}")
    print(f"    ULA control        : ${s.ula_control:02X}  RS423={s.rs423_selected}  motor={s.motor_on}")
    print(f"    baud (tx/rx)       : {s.tx_baud} / {s.rx_baud}")


def run_demo(bbc: Beebium):
    """Run the rpc-serial demo against a connected Beebium instance."""
    # Work with the machine stopped so we control exactly how many cycles run.
    bbc.debugger.stop()
    program_serial(bbc)

    print("=== 1. Status snapshot ===")
    print_status(bbc)

    # device -> Beeb: inject bytes from the script; the BBC receives them.
    print("\n=== 2. device -> BBC (rpc-serial inject) ===")
    inbound = b"ExternalDevice"
    print(f"  Injecting {inbound!r} for the BBC to receive ...")
    bbc.extensions[RpcSerial].send(inbound)
    received_by_beeb = receive_only(bbc, len(inbound))
    print(f"  BBC received: {bytes(received_by_beeb)!r}")

    # Beeb -> device: the BBC transmits; the script collects the bytes.
    print("\n=== 3. BBC -> device (rpc-serial collect) ===")
    outbound = b"BBC_Acorn"
    print(f"  BBC transmitting {outbound!r} ...")
    for byte in outbound:
        transmit_byte(bbc, byte)
    bbc.debugger.step_cycles(2000)  # let the last character finish shifting out
    collected = bbc.extensions[RpcSerial].receive()
    print(f"  Script collected: {bytes(collected)!r}")

    print("\nDone. Resuming the machine.")
    bbc.debugger.run()


def _default_mos() -> Path | None:
    """Best-effort default MOS path: <repo>/roms/acorn-mos_1_20.rom."""
    repo_root = Path(__file__).resolve().parents[3]
    candidate = repo_root / "roms" / "acorn-mos_1_20.rom"
    return candidate if candidate.exists() else None


def main():
    parser = argparse.ArgumentParser(
        description="BBC serial port (MC6850 ACIA + Serial ULA) demo via rpc-serial.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=None,
        help="Connect to an already-running --rpc-serial server on this gRPC port. "
        "If omitted, the demo launches its own server.",
    )
    parser.add_argument(
        "--server",
        type=Path,
        default=None,
        help="Path to the beebium-server executable (self-launch mode). "
        "Defaults to auto-detection ($BEEBIUM_SERVER or PATH).",
    )
    parser.add_argument(
        "--mos",
        type=Path,
        default=None,
        help="Path to the MOS ROM (self-launch mode). Defaults to <repo>/roms/acorn-mos_1_20.rom when present.",
    )
    parser.add_argument(
        "--basic",
        type=Path,
        default=None,
        help="Optional path to the BASIC ROM (self-launch mode).",
    )
    args = parser.parse_args()

    if args.port is not None:
        # Attach to a server the user started themselves (with --rpc-serial).
        with Beebium.connect(target=f"localhost:{args.port}") as bbc:
            run_demo(bbc)
        return

    # Self-launch a server for the duration of the demo.
    mos = args.mos or _default_mos()
    if mos is None:
        parser.error(
            "self-launch mode needs --mos PATH "
            "(no default MOS ROM found at <repo>/roms/acorn-mos_1_20.rom). "
            "Alternatively start a server yourself and pass --port."
        )

    with Beebium.launch(
        mos_filepath=mos,
        basic_filepath=args.basic,
        server=args.server,
        extra_args=["--rpc-serial"],
    ) as bbc:
        run_demo(bbc)


if __name__ == "__main__":
    main()
