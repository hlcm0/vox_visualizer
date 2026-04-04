from __future__ import annotations

from core.tracks import BT_COLUMN_INDEX_BY_TRACK, FX_L_TRACK, FX_R_TRACK
from renderer.style import LANE_WIDTH, MARGIN


class MeasureLayout:
    def __init__(self, lane_height: int = 400):
        self.lane_width = LANE_WIDTH
        self.lane_height = lane_height
        self.margin = MARGIN

        #  margin   lane/2   lane (4bt tracks)   lane/2  margin
        # |------|---------|------------------|---------|------|

        # ^^ horizontal layout ^^
        # there are extra spaces on the left and right of the 4 bt tracks, which are used for lasers
        # the margin is the same on all sides, so the lane starts at margin and ends at lane_height + margin

    @property
    def image_size(self) -> tuple[int, int]:
        width = self.lane_width * 2 + self.margin * 2
        height = self.lane_height + self.margin * 2
        return width, height

    @property
    def lane_vertical_bounds(self) -> tuple[int, int]:
        top = self.margin
        bottom = self.lane_height + self.margin
        return top, bottom

    def compute_bt_columns(self) -> list[tuple[int, int]]:
        left = self.margin + self.lane_width // 2
        right = self.margin + 3 * self.lane_width // 2
        total_width = right - left
        bt_width = total_width // 4
        columns: list[tuple[int, int]] = []
        for index in range(4):
            column_left = left + index * bt_width
            column_right = left + (index + 1) * bt_width if index < 3 else right
            columns.append((column_left, column_right))
        return columns

    def lane_bounds_for_track(self, track: int, bt_columns: list[tuple[int, int]]) -> tuple[int, int]:
        if track == FX_L_TRACK:
            return bt_columns[0][0], bt_columns[1][1]
        if track == FX_R_TRACK:
            return bt_columns[2][0], bt_columns[3][1]
        return bt_columns[BT_COLUMN_INDEX_BY_TRACK[track]]

    def lane_bounds_for_laser(self, range_index: int, laser_width: int) -> tuple[int, int]:
        if range_index == 1:
            return (
                self.margin + self.lane_width // 2 - laser_width // 2,
                self.margin + 3 * self.lane_width // 2 + laser_width // 2,
            )
        return (self.margin, self.margin + 2 * self.lane_width)

    @staticmethod
    def fraction_to_y(top: int, bottom: int, fraction: float) -> float:
        return top + (bottom - top) * max(0.0, min(1.0, fraction))
