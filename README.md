# VOX Visualizer

Renders VOX chart files into columnar PNG images.

## Dependencies

- Python 3.10+
- Pillow

```bash
pip install pillow
```

## Usage

```bash
python cli.py <input.vox> [output.png] [options]
```

If `output.png` is omitted, the output defaults to `<input_stem>.png` in the same directory.

### Options

| Option | Default | Description |
| --- | --- | --- |
| `input` | (required) | Path to a `.vox` chart file |
| `output` | `<input>_columns.png` | Output PNG path |
| `--start-measure` | `1` | First measure to render (1-based) |
| `--end-measure` | end of chart | Last measure to render |
| `--measures-per-column` | `5` | Number of measures per column |
| `--column-gap` | `80` | Horizontal gap between columns in pixels |

## Examples

```bash
# Basic usage, output to default path
python cli.py chart.vox

# Specify output path
python cli.py chart.vox output.png

# Render only measures 10 through 30
python cli.py chart.vox output.png --start-measure 10 --end-measure 30

# 6 measures per column, 40px gap
python cli.py chart.vox output.png --measures-per-column 6 --column-gap 40
```

## Example Output

```bash
python3 cli.py example/1.vox
```
![example output for 1.vox](example/1.png)