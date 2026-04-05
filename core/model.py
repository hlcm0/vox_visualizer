from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from core.tracks import LASER_TRACKS

DEFAULT_BEAT_RESOLUTION = 48


@dataclass(frozen=True)
class TimePoint:
    measure: int
    beat: int
    offset: int

    @classmethod
    def parse(cls, value: str) -> "TimePoint":
        measure_str, beat_str, offset_str = value.split(",")
        return cls(int(measure_str), int(beat_str), int(offset_str))


@dataclass(frozen=True)
class Signature:
    beat: int = 4
    note: int = 4

    def beat_units(self, beat_resolution: int = DEFAULT_BEAT_RESOLUTION) -> float:
        return beat_resolution * 4 / self.note

    def measure_units(self, beat_resolution: int = DEFAULT_BEAT_RESOLUTION) -> float:
        return self.beat * self.beat_units(beat_resolution)


@dataclass(frozen=True)
class ButtonEvent:
    track: int
    time: TimePoint
    hold_length: int
    effect_code: int


@dataclass(frozen=True)
class BpmEvent:
    time: TimePoint
    bpm: float
    division: int = 4


@dataclass(frozen=True)
class MeasureHoldEvent:
    track: int
    measure: int
    start_fraction: float
    end_fraction: float
    effect_code: int


@dataclass(frozen=True)
class MeasureLaserSegment:
    track: int
    measure: int
    start_fraction: float
    end_fraction: float
    start_position: float
    end_position: float
    start_flag: int
    end_flag: int
    range_index: int


@dataclass(frozen=True)
class LaserNode:
    track: int
    time: TimePoint
    position: float
    flag: int
    impact: int
    filter_index: int
    range_index: int
    order: int


@dataclass
class VoxChart:
    source_path: Path | None = None
    version: int = 10
    beat_resolution: int = DEFAULT_BEAT_RESOLUTION
    end_position: TimePoint = field(default_factory=lambda: TimePoint(1, 1, 0))
    signatures: dict[int, Signature] = field(default_factory=lambda: {1: Signature(4, 4)})
    bpms: list[BpmEvent] = field(default_factory=list)
    buttons: list[ButtonEvent] = field(default_factory=list)
    lasers: dict[int, list[LaserNode]] = field(default_factory=lambda: {track: [] for track in LASER_TRACKS})
