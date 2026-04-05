from __future__ import annotations

import bisect

from core.model import ButtonEvent, LaserNode, MeasureHoldEvent, MeasureLaserSegment, Signature, TimePoint, VoxChart


class ChartMetrics:
    def __init__(self, chart: VoxChart):
        self.chart = chart
        self._resolution = chart.beat_resolution
        self._signature_cache: dict[int, Signature] = {}
        self._measure_start_cache: dict[int, float] = {1: 0.0}
        self._measure_duration_cache: dict[int, float] = {}
        self._measure_time_segments_cache: dict[int, list[tuple[float, float, float, float]]] = {}

        # Build global BPM timeline: sorted list of (chart_units, bpm, units_per_beat)
        self._bpm_units: list[float] = []
        self._bpm_values: list[float] = []
        self._bpm_beat_units: list[float] = []
        if chart.bpms:
            for event in chart.bpms:
                self._bpm_units.append(self.chart_units(event.time))
                self._bpm_values.append(event.bpm)
                self._bpm_beat_units.append(self._resolution * 4 / event.division)
        else:
            self._bpm_units.append(0.0)
            self._bpm_values.append(120.0)
            self._bpm_beat_units.append(float(self._resolution))

    def signature_for_measure(self, measure: int) -> Signature:
        if measure in self._signature_cache:
            return self._signature_cache[measure]

        candidates = [start for start in self.chart.signatures if start <= measure]
        signature = self.chart.signatures[max(candidates)] if candidates else Signature(4, 4)
        self._signature_cache[measure] = signature
        return signature

    def local_units(self, time: TimePoint) -> float:
        signature = self.signature_for_measure(time.measure)
        return ((time.beat - 1) * signature.beat_units(self._resolution)) + time.offset

    def measure_fraction(self, time: TimePoint) -> float:
        return self.units_to_time_fraction(time.measure, self.local_units(time))

    def measure_start_units(self, measure: int) -> float:
        if measure in self._measure_start_cache:
            return self._measure_start_cache[measure]

        total = 0.0
        for current_measure in range(1, measure):
            total += self.signature_for_measure(current_measure).measure_units(self._resolution)

        self._measure_start_cache[measure] = total
        return total

    def chart_units(self, time: TimePoint) -> float:
        return self.measure_start_units(time.measure) + self.local_units(time)

    def bpm_at_units(self, abs_units: float) -> tuple[float, float]:
        idx = max(0, bisect.bisect_right(self._bpm_units, abs_units) - 1)
        return self._bpm_values[idx], self._bpm_beat_units[idx]

    def _measure_time_segments(self, measure: int) -> list[tuple[float, float, float, float]]:
        if measure in self._measure_time_segments_cache:
            return self._measure_time_segments_cache[measure]

        m_start = self.measure_start_units(measure)
        m_end = m_start + self.signature_for_measure(measure).measure_units(self._resolution)

        # Find all BPM change points within [m_start, m_end)
        lo = bisect.bisect_right(self._bpm_units, m_start) - 1
        hi = bisect.bisect_right(self._bpm_units, m_end)
        breakpoints: list[float] = [m_start]
        for i in range(max(0, lo), hi):
            u = self._bpm_units[i]
            if m_start < u < m_end:
                breakpoints.append(u)
        breakpoints.append(m_end)

        segments: list[tuple[float, float, float, float]] = []
        for a, b in zip(breakpoints, breakpoints[1:]):
            bpm, beat_units = self.bpm_at_units(a)
            local_a = a - m_start
            local_b = b - m_start
            segments.append((local_a, local_b, bpm, beat_units))

        self._measure_time_segments_cache[measure] = segments
        return segments

    def measure_duration(self, measure: int) -> float:
        if measure in self._measure_duration_cache:
            return self._measure_duration_cache[measure]

        total = 0.0
        for local_a, local_b, bpm, beat_units in self._measure_time_segments(measure):
            total += (local_b - local_a) * 60.0 / (beat_units * bpm)
        self._measure_duration_cache[measure] = total
        return total

    # Convert local measure units to a time fraction [0.0, 1.0] based on BPM changes within the measure
    def units_to_time_fraction(self, measure: int, local_units: float) -> float:
        total_duration = self.measure_duration(measure)
        if total_duration <= 0:
            return 0.0

        elapsed = 0.0
        for local_a, local_b, bpm, beat_units in self._measure_time_segments(measure):
            if local_units <= local_a:
                break
            seg_units = min(local_units, local_b) - local_a
            elapsed += seg_units * 60.0 / (beat_units * bpm)
            if local_units <= local_b:
                break
        return min(1.0, elapsed / total_duration)

    def beat_time_fractions(self, measure: int) -> list[float]:
        signature = self.signature_for_measure(measure)
        return [
            self.units_to_time_fraction(measure, beat * signature.beat_units(self._resolution))
            for beat in range(signature.beat)
        ]

    def split_hold_events(self, event: ButtonEvent) -> list[MeasureHoldEvent]:
        segments: list[MeasureHoldEvent] = []
        remaining = float(max(event.hold_length, 0))
        current_measure = event.time.measure
        current_units = self.local_units(event.time)

        while True:
            signature = self.signature_for_measure(current_measure)
            measure_units = signature.measure_units(self._resolution)
            start_fraction = self.units_to_time_fraction(current_measure, current_units)

            if remaining <= 0:
                chip_end_units = min(measure_units, current_units + 4.0)
                end_fraction = self.units_to_time_fraction(current_measure, chip_end_units)
                end_fraction = min(1.0, end_fraction)
                segments.append(
                    MeasureHoldEvent(
                        track=event.track,
                        measure=current_measure,
                        start_fraction=start_fraction,
                        end_fraction=end_fraction,
                        effect_code=event.effect_code,
                    )
                )
                return segments

            available = measure_units - current_units
            if remaining <= available:
                end_fraction = self.units_to_time_fraction(current_measure, current_units + remaining)
                segments.append(
                    MeasureHoldEvent(
                        track=event.track,
                        measure=current_measure,
                        start_fraction=start_fraction,
                        end_fraction=end_fraction,
                        effect_code=event.effect_code,
                    )
                )
                return segments

            segments.append(
                MeasureHoldEvent(
                    track=event.track,
                    measure=current_measure,
                    start_fraction=start_fraction,
                    end_fraction=1.0,
                    effect_code=event.effect_code,
                )
            )
            remaining -= available
            current_measure += 1
            current_units = 0.0

    def split_hold_segments(self, event: ButtonEvent) -> list[tuple[int, float, float]]:
        return [
            (segment.measure, segment.start_fraction, segment.end_fraction)
            for segment in self.split_hold_events(event)
        ]

    def split_laser_segments(self, nodes: list[LaserNode]) -> list[MeasureLaserSegment]:
        if len(nodes) < 2:
            return []

        segments: list[MeasureLaserSegment] = []
        for start_node, end_node in zip(nodes, nodes[1:]):
            if start_node.flag == 2 and end_node.flag == 1:
                continue

            start_units = self.chart_units(start_node.time)
            end_units = self.chart_units(end_node.time)
            if end_units < start_units:
                continue
            pair_is_horizontal = start_units == end_units

            start_measure = start_node.time.measure
            end_measure = end_node.time.measure

            for measure in range(start_measure, end_measure + 1):
                measure_start_units = self.measure_start_units(measure)
                measure_end_units = measure_start_units + self.signature_for_measure(measure).measure_units(self._resolution)
                segment_start_units = max(start_units, measure_start_units)
                segment_end_units = min(end_units, measure_end_units)

                if segment_end_units < segment_start_units:
                    continue

                if segment_end_units == segment_start_units and not pair_is_horizontal:
                    continue

                if segment_end_units > segment_start_units:
                    duration = end_units - start_units
                    start_ratio = (segment_start_units - start_units) / duration
                    end_ratio = (segment_end_units - start_units) / duration
                    start_fraction = self.units_to_time_fraction(measure, segment_start_units - measure_start_units)
                    end_fraction = self.units_to_time_fraction(measure, segment_end_units - measure_start_units)
                    start_position = start_node.position + (end_node.position - start_node.position) * start_ratio
                    end_position = start_node.position + (end_node.position - start_node.position) * end_ratio
                else:
                    start_fraction = self.units_to_time_fraction(measure, segment_start_units - measure_start_units)
                    end_fraction = start_fraction
                    start_position = start_node.position
                    end_position = end_node.position

                segments.append(
                    MeasureLaserSegment(
                        track=start_node.track,
                        measure=measure,
                        start_fraction=start_fraction,
                        end_fraction=end_fraction,
                        start_position=start_position,
                        end_position=end_position,
                        start_flag=start_node.flag,
                        end_flag=end_node.flag,
                        range_index=start_node.range_index,
                    )
                )

        return segments
