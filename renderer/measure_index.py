from __future__ import annotations

from collections import defaultdict

from core.metrics import ChartMetrics
from core.model import ButtonEvent, MeasureHoldEvent, BpmEvent, MeasureLaserSegment, VoxChart
from core.tracks import BUTTON_TRACKS

# index of events by measure
class MeasureEventIndex:
    def __init__(self, chart: VoxChart, metrics: ChartMetrics):
        self._chips_by_measure, self._holds_by_measure = self._build_button_index(chart, metrics)
        self._laser_segments_by_measure = self._build_laser_index(chart, metrics)
        self._bpm_events_by_measure = self._build_bpm_index(chart)

    @staticmethod
    def _build_button_index(
        chart: VoxChart,
        metrics: ChartMetrics,
    ) -> tuple[dict[int, list[ButtonEvent]], dict[int, list[MeasureHoldEvent]]]:
        chips_by_measure: defaultdict[int, list[ButtonEvent]] = defaultdict(list)
        holds_by_measure: defaultdict[int, list[MeasureHoldEvent]] = defaultdict(list)

        for event in chart.buttons:
            if event.track not in BUTTON_TRACKS:
                continue
            if event.hold_length > 0:
                for segment in metrics.split_hold_events(event):
                    holds_by_measure[segment.measure].append(segment)
                continue
            chips_by_measure[event.time.measure].append(event)

        return dict(chips_by_measure), dict(holds_by_measure)

    @staticmethod
    def _build_laser_index(chart: VoxChart, metrics: ChartMetrics) -> dict[int, list[MeasureLaserSegment]]:
        laser_segments_by_measure: defaultdict[int, list[MeasureLaserSegment]] = defaultdict(list)

        for nodes in chart.lasers.values():
            for segment in metrics.split_laser_segments(nodes):
                laser_segments_by_measure[segment.measure].append(segment)

        return dict(laser_segments_by_measure)
    
    @staticmethod
    def _build_bpm_index(chart: VoxChart) -> dict[int, list[BpmEvent]]:
        bpms_by_measure: defaultdict[int, list[BpmEvent]] = defaultdict(list)

        for event in chart.bpms:
            bpms_by_measure[event.time.measure].append(event)

        return dict(bpms_by_measure)

    def measure_events(self, measure: int) -> tuple[list[ButtonEvent], list[MeasureHoldEvent]]:
        return list(self._chips_by_measure.get(measure, ())), list(self._holds_by_measure.get(measure, ()))

    def measure_lasers(self, measure: int) -> list[MeasureLaserSegment]:
        return list(self._laser_segments_by_measure.get(measure, ()))
    
    def measure_bpms(self, measure: int) -> list[BpmEvent]:
        return list(self._bpm_events_by_measure.get(measure, ()))
