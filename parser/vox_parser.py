from __future__ import annotations

from pathlib import Path

from core.model import BpmEvent, ButtonEvent, LaserNode, Signature, TimePoint, VoxChart
from core.tracks import TRACK_IDS, is_laser_track


class VoxParser:
    SECTION_NAMES = {
        "FORMAT VERSION",
        "BEAT INFO",
        "BPM INFO",
        "END POSITION",
        "TRACK1",
        "TRACK2",
        "TRACK3",
        "TRACK4",
        "TRACK5",
        "TRACK6",
        "TRACK7",
        "TRACK8",
    }

    TRACK_SECTIONS = {f"TRACK{track_id}" for track_id in TRACK_IDS}

    @staticmethod
    def _parse_int(value: str, default: int = 0) -> int:
        try:
            return int(value)
        except ValueError:
            try:
                return int(float(value))
            except ValueError:
                return default

    # before version 11 (assumed), laser positions are from 0 to 127 (right->left)
    # from version 11, laser positions are from 0.0 to 1.0 (left->right)
    @staticmethod
    def _parse_laser_position(value: str, version: int) -> float:
        try:
            parsed = float(value)
        except ValueError:
            return 0.0

        if version >= 11:
            return max(0.0, min(1.0, parsed))

        return 1.0 - max(0.0, min(1.0, parsed / 127.0))

    # before version 11 (assumed), track order is flipped
    @staticmethod
    def _canonical_track_id(raw_track_id: int, version: int) -> int:
        if version >= 11:
            return raw_track_id
        return 9 - raw_track_id

    @classmethod
    def parse(cls, path: Path) -> VoxChart:
        chart = VoxChart(source_path=path)
        current_section: str | None = None
        line_order = 0
        for codec in ("shift_jis", "cp932", "shift_jisx0213"):
            try:
                text = path.read_text(encoding=codec)
                break
            except UnicodeDecodeError:
                continue
        else:
            text = path.read_text(encoding="utf-8", errors="replace")

        for raw_line in text.splitlines():
            line = raw_line.strip().strip("\t")
            if not line or line == "//" or line.startswith("//"):
                continue

            line_order += 1

            if line.startswith("#"):
                section_name = line[1:].strip().upper()
                current_section = None if line == "#END" else (section_name if section_name in cls.SECTION_NAMES else None)
                continue

            if current_section == "FORMAT VERSION":
                try:
                    chart.version = int(line)
                except ValueError:
                    pass
                continue

            if current_section == "BEAT INFO":
                parts = [part.strip() for part in line.split("\t")]
                if len(parts) != 3:
                    continue
                time = TimePoint.parse(parts[0])
                chart.signatures[time.measure] = Signature(int(parts[1]), int(parts[2]))
                continue

            if current_section == "BPM INFO":
                parts = [part.strip() for part in line.split("\t")]
                if len(parts) < 2:
                    continue
                try:
                    bpm = float(parts[1])
                except ValueError:
                    continue
                division = 4
                if len(parts) >= 3:
                    try:
                        division = int(parts[2].rstrip("-"))
                    except ValueError:
                        pass
                chart.bpms.append(BpmEvent(time=TimePoint.parse(parts[0]), bpm=bpm, division=division))
                continue

            if current_section == "END POSITION":
                chart.end_position = TimePoint.parse(line)
                continue

            if current_section not in cls.TRACK_SECTIONS:
                continue

            raw_track_id = int(current_section.replace("TRACK", ""))
            track_id = cls._canonical_track_id(raw_track_id, chart.version)
            parts = [part.strip() for part in line.split("\t")]
            if is_laser_track(track_id):
                if len(parts) < 6:
                    continue
                chart.lasers[track_id].append(
                    LaserNode(
                        track=track_id,
                        time=TimePoint.parse(parts[0]),
                        position=cls._parse_laser_position(parts[1], chart.version),
                        flag=cls._parse_int(parts[2]),
                        impact=cls._parse_int(parts[3]),
                        filter_index=cls._parse_int(parts[4]),
                        range_index=cls._parse_int(parts[5]),
                        order=line_order,
                    )
                )
                continue

            if len(parts) < 3:
                continue
            chart.buttons.append(
                ButtonEvent(
                    track=track_id,
                    time=TimePoint.parse(parts[0]),
                    hold_length=cls._parse_int(parts[1]),
                    effect_code=cls._parse_int(parts[2]),
                )
            )

        for track_nodes in chart.lasers.values():
            track_nodes.sort(key=lambda node: (node.time.measure, node.time.beat, node.time.offset, node.order))
        chart.bpms.sort(key=lambda event: (event.time.measure, event.time.beat, event.time.offset))
        chart.buttons.sort(key=lambda event: (event.time.measure, event.time.beat, event.time.offset, event.track))
        return chart
