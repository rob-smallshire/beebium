# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""bbc.cassette and Beebium.boot_tape against a server (#208)."""

from __future__ import annotations

import contextlib
import subprocess
import sys
import threading
import time
import wave
from pathlib import Path

import pytest
import tape_builder

from beebium.client import Beebium, CassetteError, CreateTapeError, ServerInstallation
from beebium.client.cassette import CassetteErrorKind, CreateTapeErrorKind, TapeTransport
from beebium.client.exceptions import ServerNotFoundError

ASSETS_DIRPATH = Path(__file__).resolve().parents[3] / "tests" / "assets"


@pytest.fixture
def bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(mos_filepath=mos_filepath, basic_filepath=basic_filepath,
                            server=beebium_server_filepath) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_insert_leaves_the_deck_stopped_at_the_start(bbc, tmp_path: Path):
    tape = tape_builder.write(tmp_path / "tone.uef", tape_builder.uef_of_carrier(2.0))
    metadata = bbc.cassette.insert(tape)
    assert metadata.name == "tone.uef"
    assert metadata.format == "UEF"
    assert abs(metadata.duration_seconds - 2.0) < 0.001

    status = bbc.cassette.status()
    assert status.has_cassette_interface
    assert status.tape is not None and status.tape.name == "tone.uef"
    assert status.transport == TapeTransport.STOPPED
    assert status.position_seconds == 0.0
    assert status.ula_part == "ferranti"


def test_the_deck_keys_round_trip(bbc, tmp_path: Path):
    tape = tape_builder.write(tmp_path / "tone.uef", tape_builder.uef_of_carrier(10.0))
    bbc.cassette.insert(tape)
    bbc.cassette.play()
    assert bbc.cassette.status().transport == TapeTransport.PLAYING
    bbc.cassette.stop()
    assert bbc.cassette.status().transport == TapeTransport.STOPPED
    bbc.cassette.seek(4.5)
    assert abs(bbc.cassette.status().position_seconds - 4.5) < 0.001
    bbc.cassette.seek(0)
    assert bbc.cassette.status().position_seconds == 0.0
    bbc.cassette.eject()
    assert bbc.cassette.status().tape is None


def test_the_deck_winds_at_thirty_times_real_speed(bbc, tmp_path: Path):
    tape = tape_builder.write(tmp_path / "long.uef", tape_builder.uef_of_carrier(90.0))
    bbc.cassette.insert(tape)
    bbc.cassette.fast_forward()
    assert bbc.cassette.status().transport == TapeTransport.FAST_FORWARDING
    bbc.run_for_emulated_seconds(1.0)
    status = bbc.cassette.status()
    assert status.transport == TapeTransport.FAST_FORWARDING
    assert 25.0 < status.position_seconds < 35.0
    bbc.cassette.rewind()
    assert bbc.cassette.status().transport == TapeTransport.REWINDING
    bbc.run_for_emulated_seconds(1.5)
    status = bbc.cassette.status()
    assert status.transport == TapeTransport.STOPPED
    assert status.position_seconds == 0.0


def test_errors_carry_a_kind_and_the_servers_sentence(bbc, tmp_path: Path):
    with pytest.raises(CassetteError) as empty:
        bbc.cassette.play()
    assert empty.value.kind == CassetteErrorKind.DECK_EMPTY
    assert str(empty.value) == "There is no tape in the deck."

    with pytest.raises(CassetteError) as missing:
        bbc.cassette.insert(tmp_path / "absent.uef")
    assert missing.value.kind == CassetteErrorKind.CANNOT_OPEN
    assert "absent.uef" in str(missing.value)

    notes = tmp_path / "notes.txt"
    notes.write_text("hello")
    with pytest.raises(CassetteError) as unsupported:
        bbc.cassette.insert(notes)
    assert unsupported.value.kind == CassetteErrorKind.UNSUPPORTED_FORMAT

    tape = tape_builder.write(tmp_path / "one.uef", tape_builder.uef_of_carrier(1.0))
    other = tape_builder.write(tmp_path / "two.uef", tape_builder.uef_of_carrier(1.0))
    bbc.cassette.insert(tape)
    with pytest.raises(CassetteError) as occupied:
        bbc.cassette.insert(other)
    assert occupied.value.kind == CassetteErrorKind.DECK_OCCUPIED
    assert "one.uef" in str(occupied.value) and "two.uef" in str(occupied.value)

    with pytest.raises(CassetteError) as before_start:
        bbc.cassette.seek(-1.0)
    assert before_start.value.kind == CassetteErrorKind.INVALID_POSITION


def test_the_status_stream_starts_with_the_current_status_then_changes(bbc, tmp_path: Path):
    tape = tape_builder.write(tmp_path / "tone.uef", tape_builder.uef_of_carrier(2.0))
    events = bbc.cassette.events(min_interval_ms=50)
    try:
        first = next(events)
        assert first.tape is None
        bbc.cassette.insert(tape)
        for status in events:
            if status.tape is not None:
                assert status.tape.name == "tone.uef"
                break
    finally:
        events.close()


def _tokenised(tmp_path: Path) -> bytes:
    tok = tmp_path / "tape_ok.tok"
    result = subprocess.run(
        ["uvx", "oaknut-basic", "tokenise", str(ASSETS_DIRPATH / "cassette" / "tape_ok.bas"), str(tok)],
        capture_output=True, text=True)
    if result.returncode != 0 or not tok.exists():
        pytest.skip("oaknut-basic is not available through uvx, so the test program cannot be tokenised.")
    return tok.read_bytes()


def test_boot_tape_loads_and_runs_a_basic_program(bbc, tmp_path: Path):
    program = _tokenised(tmp_path)
    tape = tape_builder.write(tmp_path / "tape_ok.uef",
                              tape_builder.uef_of_file("TAPEOK", 0xFFFF0E00, 0xFFFF8023, program))
    bbc.boot_tape(tape)
    # The program writes 42 to &70 when it runs; the tape takes about eight
    # seconds of emulated time.
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if bbc.memory.address.peek[0x70] == 42:
            break
        time.sleep(0.5)
    assert bbc.memory.address.peek[0x70] == 42
    # The program sets &70 before its PRINT has drawn the line, so the screen
    # is polled too, within the same deadline.
    text = bbc.video.screen_text().text
    while "TAPE OK 42" not in text and time.monotonic() < deadline:
        time.sleep(0.1)
        text = bbc.video.screen_text().text
    assert "TAPE OK 42" in text


def test_blank_tapes_are_listed_and_one_is_created(bbc, tmp_path: Path):
    tapes = bbc.cassette.list_blank_tapes()
    assert [t.label for t in tapes] == ["C15", "C30", "C60", "C90"]
    assert [t.duration_seconds for t in tapes] == [900.0, 1800.0, 3600.0, 5400.0]
    assert all(t.kind == "tape" and t.filename == t.label + ".uef" for t in tapes)

    created = bbc.cassette.create_tape("C15", tmp_path / "side-a")
    assert created.resolve() == (tmp_path / "side-a.uef").resolve()
    with pytest.raises(CreateTapeError) as exists:
        bbc.cassette.create_tape("C15", created)
    assert exists.value.kind == CreateTapeErrorKind.DESTINATION_EXISTS
    assert bbc.cassette.create_tape("C30", created, overwrite=True) == created
    with pytest.raises(CreateTapeError) as unknown:
        bbc.cassette.create_tape("C120", tmp_path / "other.uef")
    assert unknown.value.kind == CreateTapeErrorKind.UNKNOWN_LABEL
    with pytest.raises(CreateTapeError) as wrong:
        bbc.cassette.create_tape("C15", tmp_path / "other.csw")
    assert wrong.value.kind == CreateTapeErrorKind.WRONG_EXTENSION

    metadata = bbc.cassette.insert(created)
    assert metadata.recordable
    assert metadata.duration_seconds == 1800.0


def test_record_is_a_transport_and_a_csw_is_read_only(bbc, tmp_path: Path):
    csw = tape_builder.write(tmp_path / "game.csw", tape_builder.csw_of_pulses([18] * 2000))
    metadata = bbc.cassette.insert(csw)
    assert not metadata.recordable
    with pytest.raises(CassetteError) as read_only:
        bbc.cassette.record()
    assert read_only.value.kind == CassetteErrorKind.READ_ONLY
    assert "game.csw" in str(read_only.value) and "CSW" in str(read_only.value)
    bbc.cassette.eject()

    bbc.cassette.insert(bbc.cassette.create_tape("C15", tmp_path / "blank"))
    bbc.cassette.record()
    assert bbc.cassette.status().transport == TapeTransport.RECORDING
    bbc.cassette.stop()
    assert bbc.cassette.status().transport == TapeTransport.STOPPED


def test_a_wav_inserts_with_its_rate_and_channel(bbc, tmp_path: Path):
    wav = tape_builder.write(tmp_path / "take.wav", tape_builder.wav_of_carrier(1.0, stereo_signal_channel=1))
    louder = bbc.cassette.insert(wav)
    assert louder.format == "WAV"
    assert louder.sample_rate == 48000
    assert louder.channel == "right"
    assert not louder.recordable
    bbc.cassette.eject()
    chosen = bbc.cassette.insert(wav, channel="left")
    assert chosen.channel == "left"
    bbc.cassette.eject()
    with pytest.raises(CassetteError) as bad:
        bbc.cassette.insert(wav, channel="centre")
    assert str(bad.value) == "A WAV's channel is left or right, not centre."


def _wait_for_signal_map(bbc, generation: int):
    """The signal map once the server has published one after `generation`."""
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        status = bbc.cassette.status()
        if status.signal_map_ready and status.signal_map_generation > generation:
            return bbc.cassette.signal_map(), status
        time.sleep(0.02)
    raise AssertionError("The signal map was not ready within 30 seconds.")


def test_signal_map_of_a_tape_and_of_a_recording(bbc, tmp_path: Path):
    with pytest.raises(CassetteError) as empty:
        bbc.cassette.signal_map()
    assert empty.value.kind == CassetteErrorKind.DECK_EMPTY

    # 2.5 s of gap, 2.5 s of carrier, 1 s of gap, 2.5 s of carrier: 8.5 s.
    tape = tape_builder.write(tmp_path / "spans.uef", tape_builder.uef_of_spans(
        [("gap", 2.5), ("carrier", 2.5), ("gap", 1.0), ("carrier", 2.5)]))
    before = bbc.cassette.status().signal_map_generation
    bbc.cassette.insert(tape)
    signal_map, status = _wait_for_signal_map(bbc, before)
    assert signal_map.ready
    assert signal_map.seconds == 9 == len(signal_map.levels)
    expected = [0, 0, 128, 255, 255, 0, 255, 255, 255]
    levels = list(signal_map.levels)
    assert all(abs(level - want) <= 10 for level, want in zip(levels, expected, strict=True)), levels
    bbc.cassette.eject()

    # A blank C15 is silent; a SAVE recorded on it is not.
    bbc.expect("BASIC")
    blank = bbc.cassette.create_tape("C15", tmp_path / "recorded")
    before = bbc.cassette.status().signal_map_generation
    bbc.cassette.insert(blank)
    signal_map, status = _wait_for_signal_map(bbc, before)
    assert signal_map.seconds == 900 and not any(signal_map.levels)

    bbc.keyboard.type('10 PRINT "HELLO FROM TAPE"\r*TAPE\rSAVE "HELLO"\r')
    bbc.expect("RECORD then RETURN")
    bbc.cassette.record()
    bbc.keyboard.press_return()
    with contextlib.closing(bbc.cassette.events()) as events:
        for event in events:
            if not event.motor_on and event.position_seconds > 0:
                break
    # While it recorded, the map followed the head; the relay is open now, so
    # the head is still and the next map is the walk of the image written back.
    before = bbc.cassette.status().signal_map_generation
    assert before > status.signal_map_generation
    bbc.cassette.stop()
    signal_map, status = _wait_for_signal_map(bbc, before)
    assert status.signal_map_generation == before + 1
    end = int(status.position_seconds)
    assert sum(1 for level in signal_map.levels[:end + 1] if level >= 128) >= 5
    assert not any(signal_map.levels[end + 2:])


def _blank_tape_template_filepath(installation: ServerInstallation, filename: str) -> Path | None:
    """The template the server reads: beside the executables in a build tree,
    under share/beebium in an installed one, or None where neither holds it."""
    candidates = [installation.bin_dirpath / "blank-tapes" / filename]
    if installation.root_dirpath is not None:
        candidates.append(installation.root_dirpath / "share" / "beebium" / "blank-tapes" / filename)
    return next((c for c in candidates if c.is_file()), None)


def test_a_blank_tape_template_is_never_inserted(bbc, server_installation: ServerInstallation, tmp_path: Path):
    template = _blank_tape_template_filepath(server_installation, "C15.uef")
    if template is None:
        pytest.skip(f"The server from {server_installation.origin} has no C15 template where the test can find it.")
    with pytest.raises(CassetteError) as refused:
        bbc.cassette.insert(template)
    assert refused.value.kind == CassetteErrorKind.INVALID_IMAGE
    assert "blank tape templates" in str(refused.value) and "create-tape" in str(refused.value)
    assert bbc.cassette.status().tape is None

    metadata = bbc.cassette.insert(bbc.cassette.create_tape("C15", tmp_path / "copy"))
    assert metadata.recordable and metadata.record_refusal == ""


@pytest.mark.skipif(sys.platform == "win32", reason="chmod does not make a file read-only to its owner on Windows")
def test_a_read_only_tape_is_not_recordable_and_says_why(bbc, tmp_path: Path):
    tape = tape_builder.write(tmp_path / "ro.uef", tape_builder.uef_of_spans([("gap", 10.0)]))
    tape.chmod(0o444)
    try:
        metadata = bbc.cassette.insert(tape)
        assert not metadata.recordable
        assert metadata.record_refusal == ("Beebium cannot write the tape ro.uef or the folder it is in, so it does "
                                           "not record on it; record on a copy you can write.")
        with pytest.raises(CassetteError) as refused:
            bbc.cassette.record()
        assert refused.value.kind == CassetteErrorKind.READ_ONLY
        assert str(refused.value) == metadata.record_refusal
    finally:
        tape.chmod(0o644)


def test_seek_to_append_point_lands_past_the_last_recording(bbc, tmp_path: Path):
    with pytest.raises(CassetteError) as empty:
        bbc.cassette.seek_to_append_point()
    assert empty.value.kind == CassetteErrorKind.DECK_EMPTY
    assert bbc.cassette.status().append_refusal == str(empty.value)

    # Recorded to 8.5 s, then ten seconds blank: the point is 8 + 1 + 2 = 11 s.
    tape = tape_builder.write(tmp_path / "appendable.uef", tape_builder.uef_of_spans(
        [("gap", 2.0), ("carrier", 3.0), ("gap", 1.0), ("carrier", 2.5), ("gap", 10.0)]))
    before = bbc.cassette.status().signal_map_generation
    bbc.cassette.insert(tape)
    _wait_for_signal_map(bbc, before)
    assert bbc.cassette.status().append_refusal == ""
    point = bbc.cassette.seek_to_append_point()
    assert abs(point.position_seconds - 11.0) < 1e-6 and not point.at_end
    assert abs(bbc.cassette.status().position_seconds - 11.0) < 1e-3
    bbc.cassette.eject()

    # A blank tape: the start.
    before = bbc.cassette.status().signal_map_generation
    bbc.cassette.insert(bbc.cassette.create_tape("C15", tmp_path / "blank"))
    _wait_for_signal_map(bbc, before)
    bbc.cassette.seek(30.0)
    point = bbc.cassette.seek_to_append_point()
    assert point.position_seconds == 0.0 and not point.at_end
    bbc.cassette.eject()


def test_seek_to_append_point_refuses_a_map_not_ready_and_a_live_tape(bbc, tmp_path: Path):
    # Ten minutes of silence at 22050 Hz takes a while to map.
    slow = tmp_path / "slow.wav"
    with wave.open(str(slow), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(bytes(2 * 22050 * 600))
    bbc.cassette.insert(slow)
    if not bbc.cassette.status().signal_map_ready:
        with pytest.raises(CassetteError) as not_ready:
            bbc.cassette.seek_to_append_point()
        assert not_ready.value.kind == CassetteErrorKind.NOT_READY
        assert str(not_ready.value) == "The tape's signal map is not ready yet; try again in a moment."
    bbc.cassette.eject()

    gate = threading.Event()

    def silence():
        while not gate.is_set():
            yield bytes(2 * 480)
            time.sleep(0.01)

    thread = threading.Thread(target=lambda: bbc.cassette.stream_audio(silence(), 48000, sample_format="s16",
                                                                       device_name="Line in"))
    thread.start()
    try:
        deadline = time.monotonic() + 10
        while bbc.cassette.status().tape is None and time.monotonic() < deadline:
            time.sleep(0.02)
        with pytest.raises(CassetteError) as live:
            bbc.cassette.seek_to_append_point()
        assert live.value.kind == CassetteErrorKind.LIVE_SOURCE
        assert str(live.value) == "A live input has no end to append to."
        assert bbc.cassette.status().append_refusal == str(live.value)
    finally:
        gate.set()
        thread.join(10)
