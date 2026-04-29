#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


def read_vlq(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not (byte & 0x80):
            return value, pos


def midi_note_to_hz(note: int) -> int:
    return int(round(440.0 * (2.0 ** ((note - 69) / 12.0))))


def parse_midi(path: Path):
    data = path.read_bytes()
    if data[:4] != b"MThd":
        raise ValueError("not a MIDI file")

    header_len = struct.unpack(">I", data[4:8])[0]
    fmt, track_count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("SMPTE time division is not supported")

    pos = 8 + header_len
    tracks: list[bytes] = []
    for _ in range(track_count):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("bad track header")
        track_len = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        tracks.append(data[pos + 8:pos + 8 + track_len])
        pos += 8 + track_len

    events: list[tuple[int, str, int, int]] = []
    tempo_events: list[tuple[int, int]] = [(0, 500000)]

    for track in tracks:
        track_pos = 0
        running_status = None
        abs_ticks = 0
        while track_pos < len(track):
            delta, track_pos = read_vlq(track, track_pos)
            abs_ticks += delta

            status = track[track_pos]
            if status < 0x80:
                if running_status is None:
                    raise ValueError("running status without previous status")
                status = running_status
            else:
                track_pos += 1
                running_status = status

            if status == 0xFF:
                meta_type = track[track_pos]
                track_pos += 1
                meta_len, track_pos = read_vlq(track, track_pos)
                payload = track[track_pos:track_pos + meta_len]
                track_pos += meta_len
                if meta_type == 0x51 and meta_len == 3:
                    tempo = int.from_bytes(payload, "big")
                    tempo_events.append((abs_ticks, tempo))
                continue

            if status in (0xF0, 0xF7):
                sysex_len, track_pos = read_vlq(track, track_pos)
                track_pos += sysex_len
                continue

            event_type = status & 0xF0
            if event_type in (0xC0, 0xD0):
                track_pos += 1
                continue

            note = track[track_pos]
            velocity = track[track_pos + 1]
            track_pos += 2

            if event_type == 0x90 and velocity > 0:
                events.append((abs_ticks, "on", note, velocity))
            elif event_type in (0x80, 0x90):
                events.append((abs_ticks, "off", note, velocity))

    tempo_events.sort()
    events.sort(key=lambda event: (event[0], 0 if event[1] == "off" else 1))
    return fmt, division, tempo_events, events


def ticks_to_ms(ticks: int, tempo_events: list[tuple[int, int]], division: int) -> int:
    total_us = 0
    last_tick = 0
    last_tempo = tempo_events[0][1]

    for tempo_tick, tempo in tempo_events[1:]:
        if tempo_tick >= ticks:
            break
        span = tempo_tick - last_tick
        total_us += span * last_tempo / division
        last_tick = tempo_tick
        last_tempo = tempo

    total_us += (ticks - last_tick) * last_tempo / division
    return int(round(total_us / 1000.0))


def build_monophonic_steps(tempo_events: list[tuple[int, int]], division: int,
                           events: list[tuple[int, str, int, int]],
                           min_freq_hz: int = 0,
                           max_step_ms: int = 0,
                           max_total_ms: int = 0):
    active: set[int] = set()
    steps: list[tuple[int, int]] = []
    last_tick = 0
    current_note = 0
    total_ms = 0

    for tick, kind, note, _velocity in events:
        if tick > last_tick:
            duration_ms = ticks_to_ms(tick, tempo_events, division) - ticks_to_ms(last_tick, tempo_events, division)
            if duration_ms > 0:
                raw_freq = midi_note_to_hz(current_note) if current_note else 0
                # Replace inaudibly low notes with silence
                freq = raw_freq if raw_freq >= min_freq_hz else 0
                # Clamp individual step duration
                if max_step_ms > 0:
                    duration_ms = min(duration_ms, max_step_ms)
                # Merge consecutive identical frequencies
                if steps and steps[-1][0] == freq:
                    steps[-1] = (freq, steps[-1][1] + duration_ms)
                else:
                    steps.append((freq, duration_ms))
                total_ms += duration_ms
                # Stop if we have reached the requested total duration
                if max_total_ms > 0 and total_ms >= max_total_ms:
                    break
            last_tick = tick

        if kind == "on":
            active.add(note)
        else:
            active.discard(note)

        current_note = max(active) if active else 0

    return steps


def write_header(output_path: Path, symbol: str, steps: list[tuple[int, int]], source_name: str):
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"/* Auto-generated from {source_name} by scripts/midi_to_piezo.py */",
        f"static const uint16_t {symbol}[][2] = {{",
    ]
    for freq_hz, duration_ms in steps:
        lines.append(f"    {{{freq_hz}, {duration_ms}}},")
    lines.extend([
        "};",
        "",
        f"static const int {symbol}_count = (int)(sizeof({symbol}) / sizeof({symbol}[0]));",
        "",
    ])
    output_path.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert a MIDI file into a monophonic piezo note table header.")
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--symbol", default="explode_midi_piezo")
    parser.add_argument("--min-freq-hz", type=int, default=150,
                        help="Replace notes below this frequency with silence (default: 150)")
    parser.add_argument("--max-step-ms", type=int, default=0,
                        help="Clamp each step to at most this duration in ms (0 = unlimited)")
    parser.add_argument("--max-total-ms", type=int, default=0,
                        help="Stop generating steps after this total ms (0 = unlimited)")
    args = parser.parse_args()

    _fmt, division, tempo_events, events = parse_midi(args.input)
    steps = build_monophonic_steps(tempo_events, division, events,
                                   min_freq_hz=args.min_freq_hz,
                                   max_step_ms=args.max_step_ms,
                                   max_total_ms=args.max_total_ms)
    write_header(args.output, args.symbol, steps, args.input.name)

    total_ms = sum(duration_ms for _freq, duration_ms in steps)
    print(f"wrote {args.output} with {len(steps)} steps, total {total_ms} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())