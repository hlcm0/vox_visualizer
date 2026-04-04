from __future__ import annotations

from PIL import Image, ImageDraw, ImageFont

from core.metrics import ChartMetrics
from core.model import BpmEvent, ButtonEvent, MeasureHoldEvent, MeasureLaserSegment, VoxChart
from core.tracks import VOL_L_TRACK, VOL_R_TRACK, is_bt_track, is_fx_track
from renderer.layout import MeasureLayout
from renderer.measure_index import MeasureEventIndex
from renderer.style import (
    BACKGROUND,
    BT_CHIP_FILL,
    BT_CHIP_INSET,
    BT_CHIP_OUTLINE,
    BT_CHIP_OUTLINE_WIDTH,
    BT_LONG_FILL,
    BT_LONG_INSET,
    BT_LONG_SIDE_OUTLINE,
    BT_LONG_SIDE_OUTLINE_WIDTH,
    CHIP_HEIGHT,
    FX_CHIP_FILL,
    FX_CHIP_INSET,
    FX_CHIP_OUTLINE,
    FX_CHIP_OUTLINE_WIDTH,
    FX_LONG_FILL,
    FX_LONG_INSET,
    FX_LONG_SIDE_OUTLINE,
    FX_LONG_SIDE_OUTLINE_WIDTH,
    GRID,
    GRID_WIDTH,
    LABEL_FONT_SIZE,
    BPM_LABEL_COLOR,
    BPM_LABEL_X_OFFSET,
    MEASURE_LABEL_COLOR,
    MEASURE_LABEL_X_OFFSET,
    PIXELS_PER_SECOND,
    SUB_GRID,
    SUB_GRID_WIDTH,
    LANE,
    LASER_HEIGHT,
    LASER_L_FILL,
    LASER_R_FILL,
    LASER_START_INDICATOR_COLOR,
    LASER_START_INDICATOR_WIDTH,
    LASER_WIDTH,
)


class MeasureRenderer:
    def __init__(self, chart: VoxChart, metrics: ChartMetrics):
        self.chart = chart
        self.metrics = metrics
        self._layout_cache: dict[int, MeasureLayout] = {}
        self.layout = MeasureLayout()  # default, overwritten per render_measure call
        self._current_measure: int = 1
        self.measure_index = MeasureEventIndex(chart, metrics)
        try:
            self.font = ImageFont.load_default(size=LABEL_FONT_SIZE)
        except TypeError:
            self.font = ImageFont.load_default()

    def _get_layout(self, lane_height: int) -> MeasureLayout:
        if lane_height not in self._layout_cache:
            self._layout_cache[lane_height] = MeasureLayout(lane_height)
        return self._layout_cache[lane_height]

    def layout_for_measure(self, measure: int) -> MeasureLayout:
        duration = self.metrics.measure_duration(measure)
        lane_height = max(20, round(duration * PIXELS_PER_SECOND))
        return self._get_layout(lane_height)

    def image_size_for_measure(self, measure: int) -> tuple[int, int]:
        return self.layout_for_measure(measure).image_size

    # three helper function to flip the y coordinate
    # so that the origin (0, 0) is at the bottom-left corner instead of top-left
    def sy(self, y: float) -> float:
        return self.layout.image_size[1] - y

    def sy_pair(self, y1: float, y2: float) -> tuple[float, float]:
        mirrored_y1 = self.sy(y1)
        mirrored_y2 = self.sy(y2)
        return min(mirrored_y1, mirrored_y2), max(mirrored_y1, mirrored_y2)

    def sy_points(self, points: tuple[tuple[float, float], ...]) -> tuple[tuple[float, float], ...]:
        return tuple((x, self.sy(y)) for x, y in points)

    # draw the background of the measure, including the lanes and grid lines
    # return the top and bottom y coordinates of the lane area, and the left and right x coordinates of each BT lane
    def draw_background(self, draw: ImageDraw.ImageDraw) -> tuple[int, int, list[tuple[int, int]]]:
        width, height = self.layout.image_size
        top, bottom = self.layout.lane_vertical_bounds
        bt_columns = self.layout.compute_bt_columns()
        left = bt_columns[0][0]
        right = bt_columns[-1][1]

        draw.rectangle((0, 0, width, height), fill=BACKGROUND)

        for lane_left, lane_right in bt_columns:
            draw.rectangle((lane_left, top, lane_right, bottom), fill=LANE)

        for lane_left, _lane_right in bt_columns:
            mirrored_top, mirrored_bottom = self.sy_pair(top, bottom)
            draw.line((lane_left, mirrored_top, lane_left, mirrored_bottom), fill=SUB_GRID, width=SUB_GRID_WIDTH)
        mirrored_top, mirrored_bottom = self.sy_pair(top, bottom)
        draw.line((right, mirrored_top, right, mirrored_bottom), fill=SUB_GRID, width=SUB_GRID_WIDTH)

        beat_fractions = self.metrics.beat_time_fractions(self._current_measure)
        for step, frac in enumerate(beat_fractions):
            y = self.layout.fraction_to_y(top, bottom, frac)
            width_px = SUB_GRID_WIDTH // 2 if step == 0 else SUB_GRID_WIDTH
            mirrored_y = self.sy(y)
            draw.line((left, mirrored_y, right, mirrored_y), fill=SUB_GRID, width=int(width_px))
        draw.line((left, self.sy(top), right, self.sy(top)), fill=GRID, width=GRID_WIDTH)

        return top, bottom, bt_columns

    # convert a position (0.0 to 1.0) within a laser range to an x coordinate on the image
    def position_to_x(self, range_index: int, position: float) -> float:
        lane_left, lane_right = self.layout.lane_bounds_for_laser(range_index, LASER_WIDTH)
        return lane_left + (lane_right - lane_left) * max(0.0, min(1.0, position))

    # when an inward tilted laser segment connects to a horizontal segment
    # we need to cut the horizontal segment's polygon to make a smooth connection
    def _horizontal_polygon(
        self,
        start_x: float,
        end_x: float,
        start_y: float,
        next_segment: MeasureLaserSegment | None,
        top: int,
        bottom: int,
    ) -> tuple[tuple[float, float], ...]:
        x_left = min(start_x, end_x)
        x_right = max(start_x, end_x)
        base_polygon = (
            (x_left - LASER_WIDTH / 2, start_y),
            (x_right + LASER_WIDTH / 2, start_y),
            (x_right + LASER_WIDTH / 2, start_y + LASER_HEIGHT),
            (x_left - LASER_WIDTH / 2, start_y + LASER_HEIGHT),
        )

        if next_segment is None or next_segment.start_fraction == next_segment.end_fraction:
            return base_polygon

        next_start_x = self.position_to_x(next_segment.range_index, next_segment.start_position)
        next_end_x = self.position_to_x(next_segment.range_index, next_segment.end_position)
        next_start_y = self.layout.fraction_to_y(top, bottom, next_segment.start_fraction)
        next_end_y = self.layout.fraction_to_y(top, bottom, next_segment.end_fraction)
        delta_y = next_end_y - next_start_y
        if delta_y <= 0:
            return base_polygon

        shift = (next_end_x - next_start_x) * (LASER_HEIGHT / delta_y)
        connection_at_right = end_x >= start_x

        if connection_at_right and shift < 0:
            return (
                (x_left - LASER_WIDTH / 2, start_y),
                (x_right + LASER_WIDTH / 2, start_y),
                (x_right + LASER_WIDTH / 2 + shift, start_y + LASER_HEIGHT),
                (x_left - LASER_WIDTH / 2, start_y + LASER_HEIGHT),
            )

        if (not connection_at_right) and shift > 0:
            return (
                (x_left - LASER_WIDTH / 2, start_y),
                (x_right + LASER_WIDTH / 2, start_y),
                (x_right + LASER_WIDTH / 2, start_y + LASER_HEIGHT),
                (x_left - LASER_WIDTH / 2 + shift, start_y + LASER_HEIGHT),
            )

        return base_polygon

    # draw a laser segment
    def draw_laser(
        self,
        draw: ImageDraw.ImageDraw,
        segment: MeasureLaserSegment,
        top: int,
        bottom: int,
        next_segment: MeasureLaserSegment | None = None,
    ) -> None:
        start_x = self.position_to_x(segment.range_index, segment.start_position)
        end_x = self.position_to_x(segment.range_index, segment.end_position)
        start_y = self.layout.fraction_to_y(top, bottom, segment.start_fraction)
        end_y = self.layout.fraction_to_y(top, bottom, segment.end_fraction)
        fill = LASER_L_FILL if segment.track == VOL_L_TRACK else LASER_R_FILL
        is_horizontal = segment.start_fraction == segment.end_fraction

        if is_horizontal:
            join_segment = next_segment if next_segment and (segment.end_flag == 0) else None
            draw.polygon(self.sy_points(self._horizontal_polygon(start_x, end_x, start_y, join_segment, top, bottom)), fill=fill)
            
            # extends the horizontal segment vertically to indicate the end
            if segment.end_flag == 2:
                end_top, end_bottom = self.sy_pair(end_y + LASER_HEIGHT, end_y + LASER_HEIGHT * 2)
                draw.rectangle(
                    (
                        end_x - LASER_WIDTH / 2,
                        end_top,
                        end_x + LASER_WIDTH / 2,
                        end_bottom,
                    ),
                    fill=fill,
                )
        else:
            draw.polygon(
                self.sy_points((
                    (start_x - LASER_WIDTH / 2, start_y),
                    (start_x + LASER_WIDTH / 2, start_y),
                    (end_x + LASER_WIDTH / 2, end_y),
                    (end_x - LASER_WIDTH / 2, end_y),
                )),
                fill=fill,
            )

        # draw start indicator
        if segment.start_flag == 1:
            mirrored_start_y = self.sy(start_y)
            draw.line(
                (start_x - LASER_WIDTH / 2, mirrored_start_y, start_x + LASER_WIDTH / 2, mirrored_start_y),
                fill=LASER_START_INDICATOR_COLOR,
                width=LASER_START_INDICATOR_WIDTH,
            )
            draw.line(
                (
                    start_x - LASER_WIDTH / 2,
                    self.sy(start_y - LASER_WIDTH / 2 - LASER_START_INDICATOR_WIDTH / 2),
                    start_x,
                    self.sy(start_y - LASER_START_INDICATOR_WIDTH / 2),
                ),
                fill=fill,
                width=LASER_START_INDICATOR_WIDTH,
            )
            draw.line(
                (
                    start_x,
                    self.sy(start_y - LASER_START_INDICATOR_WIDTH / 2),
                    start_x + LASER_WIDTH / 2,
                    self.sy(start_y - LASER_WIDTH / 2 - LASER_START_INDICATOR_WIDTH / 2),
                ),
                fill=fill,
                width=LASER_START_INDICATOR_WIDTH,
            )

    # draw a chip
    def draw_chip(
        self,
        draw: ImageDraw.ImageDraw,
        event: ButtonEvent,
        top: int,
        bottom: int,
        bt_columns: list[tuple[int, int]],
    ) -> None:
        lane_left, lane_right = self.layout.lane_bounds_for_track(event.track, bt_columns)
        note_inset = BT_CHIP_INSET if is_bt_track(event.track) else FX_CHIP_INSET
        fill = BT_CHIP_FILL if is_bt_track(event.track) else FX_CHIP_FILL
        outline = BT_CHIP_OUTLINE if is_bt_track(event.track) else FX_CHIP_OUTLINE
        outline_width = BT_CHIP_OUTLINE_WIDTH if is_bt_track(event.track) else FX_CHIP_OUTLINE_WIDTH
        center_y = self.layout.fraction_to_y(top, bottom, self.metrics.measure_fraction(event.time)) + (CHIP_HEIGHT / 2)
        mirrored_top, mirrored_bottom = self.sy_pair(
            center_y,
            center_y + (CHIP_HEIGHT + outline_width),
        )
        bounds = (
            lane_left + note_inset,
            mirrored_top,
            lane_right - note_inset,
            mirrored_bottom,
        )
        draw.rectangle(
            (bounds[0], bounds[1] + outline_width, bounds[2], bounds[3] + outline_width),
            fill=fill,
            outline=outline,
            width=outline_width,
        )

    # draw a long
    def draw_long(
        self,
        draw: ImageDraw.ImageDraw,
        event: MeasureHoldEvent,
        top: int,
        bottom: int,
        bt_columns: list[tuple[int, int]],
    ) -> None:
        lane_left, lane_right = self.layout.lane_bounds_for_track(event.track, bt_columns)
        note_inset = BT_LONG_INSET if is_bt_track(event.track) else FX_LONG_INSET
        fill = BT_LONG_FILL if is_bt_track(event.track) else FX_LONG_FILL
        outline = BT_LONG_SIDE_OUTLINE if is_bt_track(event.track) else FX_LONG_SIDE_OUTLINE
        start_y = self.layout.fraction_to_y(top, bottom, event.start_fraction)
        end_y = self.layout.fraction_to_y(top, bottom, event.end_fraction)
        mirrored_top, mirrored_bottom = self.sy_pair(start_y, end_y)
        bounds = (lane_left + note_inset, mirrored_top, lane_right - note_inset, mirrored_bottom)
        outline_width = BT_LONG_SIDE_OUTLINE_WIDTH if is_bt_track(event.track) else FX_LONG_SIDE_OUTLINE_WIDTH
        draw.rectangle((bounds[0], bounds[1], bounds[2], bounds[3]), fill=fill)
        draw.line((bounds[0] + outline_width / 2, bounds[1], bounds[0] + outline_width / 2, bounds[3]), fill=outline, width=outline_width)
        draw.line((bounds[2] - outline_width / 2, bounds[1], bounds[2] - outline_width / 2, bounds[3]), fill=outline, width=outline_width)

    # collect all button events and hold events in the measure, and return them as separate lists
    def collect_measure_events(self, measure: int) -> tuple[list[ButtonEvent], list[MeasureHoldEvent]]:
        return self.measure_index.measure_events(measure)

    # collect all laser segments in the measure
    def collect_measure_lasers(self, measure: int) -> list[MeasureLaserSegment]:
        return self.measure_index.measure_lasers(measure)

    # render all laser segments in the measure
    def render_laser_segments(
        self,
        draw: ImageDraw.ImageDraw,
        laser_segments: list[MeasureLaserSegment],
        top: int,
        bottom: int,
    ) -> None:
        for index, segment in enumerate(laser_segments):
            next_segment = laser_segments[index + 1] if index + 1 < len(laser_segments) else None
            self.draw_laser(draw, segment, top, bottom, next_segment)

    @staticmethod
    def _format_bpm(bpm: float) -> str:
        rounded = round(bpm)
        if abs(bpm - rounded) < 1e-6:
            return str(int(rounded))
        return f"{bpm:.1f}".rstrip("0").rstrip(".")

    # draw the measure number on the left side of the lanes
    def draw_measure_number(
        self,
        draw: ImageDraw.ImageDraw,
        measure: int,
        top: int,
        bt_columns: list[tuple[int, int]],
    ) -> None:
        label = str(measure)
        left, upper, right, lower = draw.textbbox((0, 0), label, font=self.font)
        text_width = right - left
        text_height = lower - upper
        lane_left = bt_columns[0][0]
        x = lane_left - MEASURE_LABEL_X_OFFSET - text_width
        draw.text((x, self.sy(top) - text_height), label, fill=MEASURE_LABEL_COLOR, font=self.font)

    # draw BPM labels for all BPM events in the measure, aligned to the right of the lanes
    def draw_bpm_labels(
        self,
        draw: ImageDraw.ImageDraw,
        measure: int,
        top: int,
        bottom: int,
        bt_columns: list[tuple[int, int]],
    ) -> None:
        lane_right = bt_columns[-1][1]
        for event in self.measure_index.measure_bpms(measure):
            label = self._format_bpm(event.bpm)
            left, upper, right, lower = draw.textbbox((0, 0), label, font=self.font)
            text_height = lower - upper
            y = self.sy(self.layout.fraction_to_y(top, bottom, self.metrics.measure_fraction(event.time))) - text_height
            x = lane_right + BPM_LABEL_X_OFFSET
            draw.text((x, y), label, fill=BPM_LABEL_COLOR, font=self.font)

    # render a measure
    def render_measure(self, measure: int) -> Image.Image:
        self._current_measure = measure
        self.layout = self.layout_for_measure(measure)
        width, height = self.layout.image_size
        image = Image.new("RGBA", (width, height), (0, 0, 0, 0))

        background_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        fx_long_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        bt_long_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        fx_chip_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        bt_chip_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        laser_l_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))
        laser_r_overlay = Image.new("RGBA", (width, height), (0, 0, 0, 0))

        background_draw = ImageDraw.Draw(background_overlay)
        fx_long_draw = ImageDraw.Draw(fx_long_overlay)
        bt_long_draw = ImageDraw.Draw(bt_long_overlay)
        fx_chip_draw = ImageDraw.Draw(fx_chip_overlay)
        bt_chip_draw = ImageDraw.Draw(bt_chip_overlay)
        laser_l_draw = ImageDraw.Draw(laser_l_overlay)
        laser_r_draw = ImageDraw.Draw(laser_r_overlay)
        top, bottom, bt_columns = self.draw_background(background_draw)

        chips, hold_events = self.collect_measure_events(measure)
        laser_segments = self.collect_measure_lasers(measure)
        fx_holds = [event for event in hold_events if is_fx_track(event.track)]
        bt_holds = [event for event in hold_events if is_bt_track(event.track)]
        fx_chips = [event for event in chips if is_fx_track(event.track)]
        bt_chips = [event for event in chips if is_bt_track(event.track)]

        for event in fx_holds:
            self.draw_long(fx_long_draw, event, top, bottom, bt_columns)

        for event in fx_chips:
            self.draw_chip(fx_chip_draw, event, top, bottom, bt_columns)

        for event in bt_holds:
            self.draw_long(bt_long_draw, event, top, bottom, bt_columns)

        for event in bt_chips:
            self.draw_chip(bt_chip_draw, event, top, bottom, bt_columns)

        left_laser_segments = [segment for segment in laser_segments if segment.track == VOL_L_TRACK]
        right_laser_segments = [segment for segment in laser_segments if segment.track == VOL_R_TRACK]

        self.render_laser_segments(laser_l_draw, left_laser_segments, top, bottom)
        self.render_laser_segments(laser_r_draw, right_laser_segments, top, bottom)

        composed = Image.alpha_composite(image, background_overlay)
        composed = Image.alpha_composite(composed, fx_long_overlay)
        composed = Image.alpha_composite(composed, bt_long_overlay)
        composed = Image.alpha_composite(composed, fx_chip_overlay)
        composed = Image.alpha_composite(composed, bt_chip_overlay)
        composed = Image.alpha_composite(composed, laser_l_overlay)
        composed = Image.alpha_composite(composed, laser_r_overlay)
        composed_draw = ImageDraw.Draw(composed)
        self.draw_measure_number(composed_draw, measure, top, bt_columns)
        self.draw_bpm_labels(composed_draw, measure, top, bottom, bt_columns)
        return composed
