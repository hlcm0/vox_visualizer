from __future__ import annotations

import argparse
from pathlib import Path
from typing import Sequence

from PIL import Image

from core.metrics import ChartMetrics
from parser.vox_parser import VoxParser
from renderer.style import MARGIN


def _resolved_input_path(path: Path) -> Path:
    input_path = path.resolve()
    if not input_path.exists():
        raise SystemExit(f"Input file not found: {input_path}")
    return input_path


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render a full VOX chart")
    parser.add_argument("input", type=Path, help="Path to the .vox file")
    parser.add_argument("output", type=Path, nargs="?", help="Output .png path")
    parser.add_argument("--start-measure", type=int, default=1, help="First 1-based measure to render")
    parser.add_argument("--end-measure", type=int, default=None, help="Last 1-based measure to render")
    parser.add_argument("--measures-per-column", type=int, default=5, help="How many measures are stitched into one column")
    parser.add_argument("--column-gap", type=int, default=80, help="Horizontal gap between columns in pixels")
    return parser.parse_args(argv)


def render_chart(
    input_path: Path,
    output_path: Path,
    start_measure: int = 1,
    end_measure: int | None = None,
    measures_per_column: int = 5,
    column_gap: int = 80,
) -> Path:
    from renderer.measure_renderer import MeasureRenderer

    chart = VoxParser.parse(input_path)
    metrics = ChartMetrics(chart)
    renderer = MeasureRenderer(chart, metrics)

    last_measure = end_measure if end_measure is not None else chart.end_position.measure
    if start_measure < 1:
        raise ValueError("start_measure must be >= 1")
    if measures_per_column < 1:
        raise ValueError("measures_per_column must be >= 1")
    if column_gap < 0:
        raise ValueError("column_gap must be >= 0")
    measure_count = last_measure - start_measure + 1
    if measure_count < 1:
        raise ValueError("No measures to render")

    measures = list(range(start_measure, last_measure + 1))
    # Pre-compute lane_height (without margins) for each measure
    measure_lane_heights = [renderer.image_size_for_measure(m)[1] - 2 * MARGIN for m in measures]

    # Split measures into columns
    columns: list[list[int]] = []
    for i in range(0, measure_count, measures_per_column):
        columns.append(list(range(i, min(i + measures_per_column, measure_count))))

    # Compute each column's total height (sum of lane_heights + top/bottom margin)
    column_heights: list[int] = []
    for col_indices in columns:
        col_lane_sum = sum(measure_lane_heights[i] for i in col_indices)
        column_heights.append(col_lane_sum + 2 * MARGIN)

    image_width = renderer.image_size_for_measure(measures[0])[0]  # width is constant
    total_width = len(columns) * image_width + max(0, len(columns) - 1) * column_gap
    total_height = max(column_heights)

    composed = Image.new("RGBA", (total_width, total_height), (0, 0, 0, 0))

    for col_idx, col_indices in enumerate(columns):
        x_offset = col_idx * (image_width + column_gap)
        # Visual order: last measure in column at top, first at bottom
        # Compute y_offsets top-down
        reversed_indices = list(reversed(col_indices))
        y_offsets: dict[int, int] = {}
        y_cursor = 0
        for idx in reversed_indices:
            y_offsets[idx] = y_cursor
            y_cursor += measure_lane_heights[idx]
        # Composite in music order (bottom to top) so upper measures' grid lines overlay lower lanes
        for idx in col_indices:
            measure = measures[idx]
            measure_image = renderer.render_measure(measure)
            composed.alpha_composite(measure_image, (x_offset, y_offsets[idx]))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    composed.save(output_path, format="PNG")
    return output_path


def main(argv: Sequence[str] | None = None) -> None:
    args = parse_args(argv)
    input_path = _resolved_input_path(args.input)
    output_path = args.output.resolve() if args.output else input_path.with_name(f"{input_path.stem}.png")
    try:
        output_path = render_chart(
            input_path,
            output_path,
            args.start_measure,
            args.end_measure,
            args.measures_per_column,
            args.column_gap,
        )
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print(output_path)


if __name__ == "__main__":
    main()
