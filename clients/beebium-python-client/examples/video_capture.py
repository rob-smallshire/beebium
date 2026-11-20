"""Video: read the display configuration and capture a frame.

VideoService streams BGRA32 frames. Here we grab one and (optionally) save it
as a PNG -- PNG export needs Pillow, from the ``imaging`` extra.
"""

from __future__ import annotations

from _demo import run

from beebium.client import Beebium


def demo(bbc: Beebium) -> None:
    cfg = bbc.video.config
    print(f"display: {cfg.width}x{cfg.height} @ {cfg.framerate_hz} Hz")

    frame = bbc.video.capture_frame()
    print(
        f"frame #{frame.frame_number}: {frame.width}x{frame.height}, "
        f"{len(frame.pixels)} bytes BGRA, cycle {frame.cycle_count}"
    )

    try:
        frame.save_png("frame.png")
        print("saved frame.png")
    except ImportError:
        print("install the imaging extra to save PNGs: pip install 'beebium[imaging]'")


if __name__ == "__main__":
    run(demo, description="Read the video config and capture a frame.")
