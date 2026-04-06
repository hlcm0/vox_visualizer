#include "renderer.h"
#include "style.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ===== Layout helpers ===== */

/* Image width is constant: LANE_WIDTH*2 + MARGIN*2 = 200 */
static int image_width(void) { return LANE_WIDTH * 2 + MARGIN * 2; }

static int lane_height_for_measure(ChartMetrics *m, int measure) {
    double dur = metrics_measure_duration(m, measure);
    int h = (int)round(dur * PIXELS_PER_SECOND);
    return h < 20 ? 20 : h;
}

/* BT column x bounds: 4 columns, returns (x_left, x_right) pairs */
static void compute_bt_columns(int bt[4][2]) {
    int left  = MARGIN + LANE_WIDTH / 2;         /* 60 */
    int right = MARGIN + 3 * LANE_WIDTH / 2;     /* 140 */
    int total_w = right - left;                  /* 80 */
    int bt_w    = total_w / 4;                   /* 20 */
    for (int i = 0; i < 3; i++) {
        bt[i][0] = left + i * bt_w;
        bt[i][1] = left + (i+1) * bt_w;
    }
    bt[3][0] = left + 3 * bt_w;
    bt[3][1] = right;
}

static void track_lane_bounds(int track, int bt[4][2], int *out_left, int *out_right) {
    if (track == FX_L_TRACK) { *out_left = bt[0][0]; *out_right = bt[1][1]; return; }
    if (track == FX_R_TRACK) { *out_left = bt[2][0]; *out_right = bt[3][1]; return; }
    /* BT track */
    int ci = bt_col_index(track);
    *out_left  = bt[ci][0];
    *out_right = bt[ci][1];
}

static void laser_lane_bounds(int range_index, int *out_left, int *out_right) {
    if (range_index == 1) {
        *out_left  = MARGIN + LANE_WIDTH/2 - LASER_WIDTH/2;   /* 60-8=52 */
        *out_right = MARGIN + 3*LANE_WIDTH/2 + LASER_WIDTH/2; /* 140+8=148 */
    } else {
        *out_left  = MARGIN;
        *out_right = MARGIN + 2 * LANE_WIDTH;                  /* 180 */
    }
}

/* Convert fraction [0,1] to y pixel (top-y increases downward) */
static float frac_to_y(int top, int bottom, double frac) {
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return (float)(top + (bottom - top) * frac);
}

/* Flip y: image has y=0 at top, music has low fractions (early notes) at bottom.
   sy() maps from "music y" to image y. */
static float sy(float y, int img_height) {
    return (float)img_height - y;
}

/* sy for a pair, returning (min, max) */
static void sy_pair(float y1, float y2, int img_h, float *out_top, float *out_bottom) {
    float a = (float)img_h - y1;
    float b = (float)img_h - y2;
    *out_top    = a < b ? a : b;
    *out_bottom = a > b ? a : b;
}

/* Position [0,1] to x pixel within laser range */
static float laser_pos_to_x(int range_index, double pos) {
    int ll, lr;
    laser_lane_bounds(range_index, &ll, &lr);
    if (pos < 0.0) pos = 0.0;
    if (pos > 1.0) pos = 1.0;
    return (float)ll + (float)(lr - ll) * (float)pos;
}

/* ===== Background drawing ===== */

static void draw_background(ImgBuf *buf, int img_h,
                            int top, int bottom,
                            int bt[4][2],
                            ChartMetrics *metrics, int measure) {
    int w = buf->width;
    Color bg = (Color)COL_BACKGROUND;
    Color lane_c = (Color)COL_LANE;
    Color sub_g = (Color)COL_SUB_GRID;
    Color grid_c = (Color)COL_GRID;

    /* clear to transparent */
    draw_rect_fill(buf, 0, 0, w-1, img_h-1, bg);

    /* lane fill */
    for (int i = 0; i < 4; i++) {
        draw_rect_fill(buf, bt[i][0], top, bt[i][1], bottom, lane_c);
    }

    int left  = bt[0][0];
    int right = bt[3][1];

    /* vertical sub-grid lines at left edge of each bt column */
    for (int i = 0; i < 4; i++) {
        float vx = (float)bt[i][0];
        draw_vline(buf, vx, (float)(img_h - bottom), (float)(img_h - top), sub_g, SUB_GRID_WIDTH);
    }
    /* right edge of last bt column */
    draw_vline(buf, (float)right, (float)(img_h - bottom), (float)(img_h - top), sub_g, SUB_GRID_WIDTH);

    /* horizontal beat lines */
    double beat_fracs[32];
    int n_beats = metrics_beat_fracs(metrics, measure, beat_fracs, 32);
    for (int i = 0; i < n_beats; i++) {
        float y_music = frac_to_y(top, bottom, beat_fracs[i]);
        float y_img   = sy(y_music, img_h);
        int lw = (i == 0) ? (SUB_GRID_WIDTH / 2 > 0 ? SUB_GRID_WIDTH / 2 : 1) : SUB_GRID_WIDTH;
        draw_hline(buf, (float)left, (float)right, y_img, sub_g, lw);
    }

    /* measure-start grid line: at sy(top) = img_h - top (bottom of visual lane) */
    draw_hline(buf, (float)left, (float)right, (float)(img_h - top), grid_c, GRID_WIDTH);
}

/* ===== Button event drawing ===== */

static void draw_chip(ImgBuf *buf, int img_h,
                      int top, int bottom,
                      int bt[4][2],
                      int track, double frac,
                      int is_bt) {
    int ll, lr;
    track_lane_bounds(track, bt, &ll, &lr);

    int inset   = is_bt ? BT_CHIP_INSET  : FX_CHIP_INSET;
    Color fill  = is_bt ? (Color)COL_BT_CHIP_FILL  : (Color)COL_FX_CHIP_FILL;
    Color outl  = is_bt ? (Color)COL_BT_CHIP_OUTLINE : (Color)COL_FX_CHIP_OUTLINE;
    int   ow    = is_bt ? BT_CHIP_OUTLINE_WIDTH : FX_CHIP_OUTLINE_WIDTH;

    float center_y  = frac_to_y(top, bottom, frac) + (float)CHIP_HEIGHT * 0.5f;
    /* mirrored_top, mirrored_bottom from sy_pair */
    float m_top, m_bottom;
    sy_pair(center_y, center_y + (float)(CHIP_HEIGHT + ow), img_h, &m_top, &m_bottom);

    int rx1 = ll + inset;
    int rx2 = lr - inset;
    int ry1 = (int)(m_top    + ow);
    int ry2 = (int)(m_bottom + ow);

    /* PIL draw.rectangle with outline uses a combined draw:
     * fill the whole rect, then draw left/right side lines.
     * We replicate by filling rect then drawing side outlines. */
    draw_rect_fill(buf, rx1, ry1, rx2, ry2, fill);
    /* left side outline */
    draw_rect_fill(buf, rx1, ry1, rx1 + ow - 1, ry2, outl);
    /* right side outline */
    draw_rect_fill(buf, rx2 - ow + 1, ry1, rx2, ry2, outl);
    /* top/bottom outline */
    draw_rect_fill(buf, rx1, ry1, rx2, ry1 + ow - 1, outl);
    draw_rect_fill(buf, rx1, ry2 - ow + 1, rx2, ry2, outl);
}

static void draw_long(ImgBuf *buf, int img_h,
                      int top, int bottom,
                      int bt[4][2],
                      int track,
                      double start_frac, double end_frac,
                      int is_bt) {
    int ll, lr;
    track_lane_bounds(track, bt, &ll, &lr);

    int   inset   = is_bt ? BT_LONG_INSET  : FX_LONG_INSET;
    Color fill    = is_bt ? (Color)COL_BT_LONG_FILL      : (Color)COL_FX_LONG_FILL;
    Color side_c  = is_bt ? (Color)COL_BT_LONG_SIDE_OUTLINE : (Color)COL_FX_LONG_SIDE_OUTLINE;
    int   ow      = is_bt ? BT_LONG_SIDE_OUTLINE_WIDTH : FX_LONG_SIDE_OUTLINE_WIDTH;

    float start_y = frac_to_y(top, bottom, start_frac);
    float end_y   = frac_to_y(top, bottom, end_frac);
    float m_top, m_bottom;
    sy_pair(start_y, end_y, img_h, &m_top, &m_bottom);

    int rx1 = ll + inset;
    int rx2 = lr - inset;
    int ry1 = (int)m_top;
    int ry2 = (int)m_bottom;

    draw_rect_fill(buf, rx1, ry1, rx2, ry2, fill);
    /* left side line */
    draw_vline(buf, (float)(rx1) + (float)ow * 0.5f, (float)ry1, (float)ry2, side_c, ow);
    /* right side line */
    draw_vline(buf, (float)(rx2) - (float)ow * 0.5f, (float)ry1, (float)ry2, side_c, ow);
}

/* ===== Laser drawing ===== */

/* Compute the polygon for a horizontal laser segment.
 * out_xs, out_ys: arrays of length >= 4 to receive polygon vertices. */
static void horizontal_laser_polygon(
    float start_x, float end_x, float start_y,
    const MeasureLaserSegment *next_seg, /* may be NULL */
    int top, int bottom, int img_h,
    float *out_xs, float *out_ys)
{
    float x_left  = start_x < end_x ? start_x : end_x;
    float x_right = start_x > end_x ? start_x : end_x;
    float lw2 = (float)LASER_WIDTH * 0.5f;
    float lh  = (float)LASER_HEIGHT;

    out_xs[0] = x_left  - lw2;  out_ys[0] = start_y;
    out_xs[1] = x_right + lw2;  out_ys[1] = start_y;
    out_xs[2] = x_right + lw2;  out_ys[2] = start_y + lh;
    out_xs[3] = x_left  - lw2;  out_ys[3] = start_y + lh;

    if (!next_seg || next_seg->start_frac == next_seg->end_frac) return;

    float nsx = laser_pos_to_x(next_seg->range_index, next_seg->start_pos);
    float nex = laser_pos_to_x(next_seg->range_index, next_seg->end_pos);
    float nsy = frac_to_y(top, bottom, next_seg->start_frac);
    float ney = frac_to_y(top, bottom, next_seg->end_frac);
    float delta_y = ney - nsy;
    if (delta_y <= 0.0f) return;

    float shift = (nex - nsx) * (lh / delta_y);
    int conn_at_right = (end_x >= start_x);

    if (conn_at_right && shift < 0.0f) {
        out_xs[2] = x_right + lw2 + shift;
        return;
    }
    if (!conn_at_right && shift > 0.0f) {
        out_xs[3] = x_left - lw2 + shift;
        return;
    }
}

static void draw_laser_segment(ImgBuf *buf, int img_h,
                               int top, int bottom,
                               const MeasureLaserSegment *seg,
                               const MeasureLaserSegment *next_seg) {
    float start_x = laser_pos_to_x(seg->range_index, seg->start_pos);
    float end_x   = laser_pos_to_x(seg->range_index, seg->end_pos);
    float start_y = frac_to_y(top, bottom, seg->start_frac);
    float end_y   = frac_to_y(top, bottom, seg->end_frac);

    Color fill = (seg->track == VOL_L_TRACK) ? (Color)COL_LASER_L_FILL : (Color)COL_LASER_R_FILL;
    int is_horizontal = (seg->start_frac == seg->end_frac);

    if (is_horizontal) {
        /* Check if we should pass next_seg to horizontal_polygon */
        const MeasureLaserSegment *join_seg =
            (next_seg && seg->end_flag == 0) ? next_seg : NULL;

        float pxs[4], pys[4];
        horizontal_laser_polygon(start_x, end_x, start_y,
                                 join_seg, top, bottom, img_h,
                                 pxs, pys);
        /* Flip y for drawing */
        float draw_xs[4], draw_ys[4];
        for (int i = 0; i < 4; i++) {
            draw_xs[i] = pxs[i];
            draw_ys[i] = sy(pys[i], img_h);
        }
        draw_poly_fill(buf, draw_xs, draw_ys, 4, fill);

        /* extends the horizontal segment vertically to indicate the end */
        if (seg->end_flag == 2) {
            float et, eb;
            sy_pair(end_y + (float)LASER_HEIGHT, end_y + (float)LASER_HEIGHT * 2, img_h, &et, &eb);
            float lw2 = (float)LASER_WIDTH * 0.5f;
            draw_rect_fill(buf,
                           (int)(end_x - lw2), (int)et,
                           (int)(end_x + lw2), (int)eb,
                           fill);
        }
    } else {
        /* Diagonal laser polygon */
        float lw2 = (float)LASER_WIDTH * 0.5f;
        float pxs[4] = {
            start_x - lw2,
            start_x + lw2,
            end_x   + lw2,
            end_x   - lw2,
        };
        float pys[4] = {
            sy(start_y, img_h),
            sy(start_y, img_h),
            sy(end_y,   img_h),
            sy(end_y,   img_h),
        };
        draw_poly_fill(buf, pxs, pys, 4, fill);
    }

    /* Start indicator */
    if (seg->start_flag == 1) {
        float m_start_y = sy(start_y, img_h);
        Color ind_c = (Color)COL_LASER_START_INDICATOR;
        float lw2 = (float)LASER_WIDTH * 0.5f;
        int ind_w = LASER_START_INDICATOR_WIDTH;

        /* horizontal white line */
        draw_hline(buf, start_x - lw2, start_x + lw2, m_start_y, ind_c, ind_w);

        /* left diagonal line */
        draw_line(buf,
            start_x - lw2,
            sy(start_y - lw2 - (float)ind_w * 0.5f, img_h),
            start_x,
            sy(start_y - (float)ind_w * 0.5f, img_h),
            fill, ind_w);

        /* right diagonal line */
        draw_line(buf,
            start_x,
            sy(start_y - (float)ind_w * 0.5f, img_h),
            start_x + lw2,
            sy(start_y - lw2 - (float)ind_w * 0.5f, img_h),
            fill, ind_w);
    }
}

/* ===== Label drawing ===== */

static void format_bpm(double bpm, char *out, int out_sz) {
    long rounded = (long)round(bpm);
    if (fabs(bpm - (double)rounded) < 1e-6) {
        snprintf(out, (size_t)out_sz, "%ld", rounded);
    } else {
        /* one decimal, strip trailing zeros and dot */
        snprintf(out, (size_t)out_sz, "%.1f", bpm);
        int len = (int)strlen(out);
        while (len > 1 && out[len-1] == '0') out[--len] = '\0';
        if (len > 1 && out[len-1] == '.') out[--len] = '\0';
    }
}

static void draw_measure_number(ImgBuf *buf, int img_h,
                                FontCtx *font, int measure,
                                int top, int bt[4][2]) {
    if (!font) return;
    char label[32];
    snprintf(label, sizeof(label), "%d", measure);
    float tw, th;
    font_measure(font, label, (float)LABEL_FONT_SIZE, &tw, &th);
    int lane_left = bt[0][0];
    float x = (float)(lane_left - MEASURE_LABEL_X_OFFSET) - tw;
    float top_y_img = sy(frac_to_y(top, top + (img_h - 2*MARGIN), 1.0f), img_h);
    /* match Python: draw.text((x, self.sy(top) - text_height), ...) */
    float draw_y = sy((float)top, img_h) - th;
    Color c = (Color)COL_MEASURE_LABEL;
    font_draw(buf, font, label, (float)LABEL_FONT_SIZE, x, draw_y, c);
    (void)top_y_img;
}

static void draw_bpm_labels(ImgBuf *buf, int img_h,
                            FontCtx *font,
                            ChartMetrics *metrics,
                            int measure, int top, int bottom,
                            int bt[4][2],
                            const EventIndex *idx) {
    if (!font) return;
    if (measure > idx->max_measure) return;
    int base = idx->bpm_idx[measure];
    int cnt  = idx->bpm_cnt[measure];
    int lane_right = bt[3][1];
    Color c = (Color)COL_BPM_LABEL;

    for (int i = 0; i < cnt; i++) {
        const BpmEvent *ev = &idx->bpms_arr[base + i];
        char label[32];
        format_bpm(ev->bpm, label, sizeof(label));
        float tw, th;
        font_measure(font, label, (float)LABEL_FONT_SIZE, &tw, &th);
        double frac = metrics_measure_frac(metrics, ev->time);
        float music_y = frac_to_y(top, bottom, frac);
        float img_y   = sy(music_y, img_h) - th;
        float x = (float)(lane_right + BPM_LABEL_X_OFFSET);
        font_draw(buf, font, label, (float)LABEL_FONT_SIZE, x, img_y, c);
    }
}

/* ===== EventIndex ===== */

int event_index_build(EventIndex *eidx,
                      const VoxChart *chart, ChartMetrics *metrics,
                      int max_measure) {
    memset(eidx, 0, sizeof(*eidx));
    eidx->max_measure = max_measure;
    int sz = max_measure + 2;

    eidx->chips_idx = (int *)calloc((size_t)sz, sizeof(int));
    eidx->chips_cnt = (int *)calloc((size_t)sz, sizeof(int));
    eidx->holds_idx = (int *)calloc((size_t)sz, sizeof(int));
    eidx->holds_cnt = (int *)calloc((size_t)sz, sizeof(int));
    eidx->laser_idx = (int *)calloc((size_t)sz, sizeof(int));
    eidx->laser_cnt = (int *)calloc((size_t)sz, sizeof(int));
    eidx->bpm_idx   = (int *)calloc((size_t)sz, sizeof(int));
    eidx->bpm_cnt   = (int *)calloc((size_t)sz, sizeof(int));

    /* -- Chips -- */
    eidx->chips = (ButtonEvent *)malloc((size_t)(chart->num_buttons + 1) * sizeof(ButtonEvent));
    eidx->total_chips = 0;
    /* -- Holds -- */
    /* Worst-case holds: each button could span many measures */
    eidx->holds = (MeasureHoldEvent *)malloc((size_t)MAX_HOLD_SEGS * sizeof(MeasureHoldEvent));
    eidx->total_holds = 0;

    /* Temporary per-measure bucket arrays for chips and holds */
    /* We'll do a two-pass: count then fill */

    /* First pass: count per measure */
    for (int i = 0; i < chart->num_buttons; i++) {
        const ButtonEvent *ev = &chart->buttons[i];
        if (!is_button_track(ev->track)) continue;
        if (ev->hold_length > 0) {
            /* holds */
            MeasureHoldEvent tmp_segs[4096];
            int n = metrics_split_hold(metrics, ev, tmp_segs, 4096);
            for (int j = 0; j < n; j++) {
                int m = tmp_segs[j].measure;
                if (m >= 1 && m <= max_measure) eidx->holds_cnt[m]++;
                if (eidx->total_holds < MAX_HOLD_SEGS) {
                    eidx->holds[eidx->total_holds++] = tmp_segs[j];
                }
            }
        } else {
            /* chips */
            int m = ev->time.measure;
            if (m >= 1 && m <= max_measure) eidx->chips_cnt[m]++;
            eidx->chips[eidx->total_chips++] = *ev;
        }
    }

    /* But we need chips/holds sorted by measure in the arrays.
     * Rebuild properly: sort chips by measure then track, then build idx. */
    /* Chips are already in track/time order from chart->buttons sort.
     * We just need to group by measure. Re-collect in measure order. */

    /* Reset and redo properly */
    memset(eidx->chips_cnt, 0, (size_t)sz * sizeof(int));
    memset(eidx->holds_cnt, 0, (size_t)sz * sizeof(int));
    eidx->total_chips = 0;
    eidx->total_holds = 0;

    /* Pass 1: count */
    for (int i = 0; i < chart->num_buttons; i++) {
        const ButtonEvent *ev = &chart->buttons[i];
        if (!is_button_track(ev->track)) continue;
        if (ev->hold_length > 0) {
            MeasureHoldEvent tmp[4096];
            int n = metrics_split_hold(metrics, ev, tmp, 4096);
            for (int j = 0; j < n; j++) {
                int m = tmp[j].measure;
                if (m >= 1 && m <= max_measure) eidx->holds_cnt[m]++;
                eidx->total_holds++;
            }
        } else {
            int m = ev->time.measure;
            if (m >= 1 && m <= max_measure) eidx->chips_cnt[m]++;
            eidx->total_chips++;
        }
    }

    /* Build start indices */
    int chip_off = 0, hold_off = 0;
    for (int m = 1; m <= max_measure; m++) {
        eidx->chips_idx[m] = chip_off; chip_off += eidx->chips_cnt[m];
        eidx->holds_idx[m] = hold_off; hold_off += eidx->holds_cnt[m];
        eidx->chips_cnt[m] = 0; /* reset to reuse as fill counter */
        eidx->holds_cnt[m] = 0;
    }
    /* Reallocate to exact size */
    free(eidx->chips);
    eidx->chips = (ButtonEvent *)malloc((size_t)(eidx->total_chips + 1) * sizeof(ButtonEvent));
    free(eidx->holds);
    eidx->holds = (MeasureHoldEvent *)malloc((size_t)(eidx->total_holds + 1) * sizeof(MeasureHoldEvent));
    eidx->total_chips = 0;
    eidx->total_holds = 0;

    /* Pass 2: fill */
    for (int i = 0; i < chart->num_buttons; i++) {
        const ButtonEvent *ev = &chart->buttons[i];
        if (!is_button_track(ev->track)) continue;
        if (ev->hold_length > 0) {
            MeasureHoldEvent tmp[4096];
            int n = metrics_split_hold(metrics, ev, tmp, 4096);
            for (int j = 0; j < n; j++) {
                int m = tmp[j].measure;
                if (m >= 1 && m <= max_measure) {
                    int pos = eidx->holds_idx[m] + eidx->holds_cnt[m]++;
                    eidx->holds[pos] = tmp[j];
                }
                eidx->total_holds++;
            }
        } else {
            int m = ev->time.measure;
            if (m >= 1 && m <= max_measure) {
                int pos = eidx->chips_idx[m] + eidx->chips_cnt[m]++;
                eidx->chips[pos] = *ev;
            }
            eidx->total_chips++;
        }
    }
    /* chips_cnt/holds_cnt now hold actual per-measure counts again */

    /* -- Lasers -- */
    eidx->lasers = (MeasureLaserSegment *)malloc((size_t)MAX_LASER_SEGS * sizeof(MeasureLaserSegment));
    eidx->total_lasers = 0;

    /* Count */
    for (int li = 0; li < 2; li++) {
        MeasureLaserSegment tmp[MAX_LASER_SEGS];
        int n = metrics_split_laser(metrics,
                                    chart->laser_nodes[li],
                                    chart->num_laser_nodes[li],
                                    tmp, MAX_LASER_SEGS);
        for (int j = 0; j < n; j++) {
            int m = tmp[j].measure;
            if (m >= 1 && m <= max_measure) eidx->laser_cnt[m]++;
        }
        eidx->total_lasers += n;
    }
    int laser_off = 0;
    for (int m = 1; m <= max_measure; m++) {
        eidx->laser_idx[m] = laser_off;
        laser_off += eidx->laser_cnt[m];
        eidx->laser_cnt[m] = 0;
    }

    /* Fill */
    for (int li = 0; li < 2; li++) {
        MeasureLaserSegment *tmp = (MeasureLaserSegment *)malloc((size_t)MAX_LASER_SEGS * sizeof(MeasureLaserSegment));
        int n = metrics_split_laser(metrics,
                                    chart->laser_nodes[li],
                                    chart->num_laser_nodes[li],
                                    tmp, MAX_LASER_SEGS);
        for (int j = 0; j < n; j++) {
            int m = tmp[j].measure;
            if (m >= 1 && m <= max_measure) {
                int pos = eidx->laser_idx[m] + eidx->laser_cnt[m]++;
                eidx->lasers[pos] = tmp[j];
            }
        }
        free(tmp);
    }
    /* Note: laser_cnt now holds actual counts per measure */

    /* -- BPMs -- */
    eidx->bpms_arr = (BpmEvent *)malloc((size_t)(chart->num_bpms + 1) * sizeof(BpmEvent));
    eidx->total_bpms = 0;

    for (int i = 0; i < chart->num_bpms; i++) {
        int m = chart->bpms[i].time.measure;
        if (m >= 1 && m <= max_measure) eidx->bpm_cnt[m]++;
        eidx->total_bpms++;
    }
    int bpm_off = 0;
    for (int m = 1; m <= max_measure; m++) {
        eidx->bpm_idx[m] = bpm_off;
        bpm_off += eidx->bpm_cnt[m];
        eidx->bpm_cnt[m] = 0;
    }
    for (int i = 0; i < chart->num_bpms; i++) {
        int m = chart->bpms[i].time.measure;
        if (m >= 1 && m <= max_measure) {
            int pos = eidx->bpm_idx[m] + eidx->bpm_cnt[m]++;
            eidx->bpms_arr[pos] = chart->bpms[i];
        }
    }

    return 0;
}

void event_index_free(EventIndex *idx) {
    free(idx->chips_idx); free(idx->chips_cnt); free(idx->chips);
    free(idx->holds_idx); free(idx->holds_cnt); free(idx->holds);
    free(idx->laser_idx); free(idx->laser_cnt); free(idx->lasers);
    free(idx->bpm_idx);   free(idx->bpm_cnt);   free(idx->bpms_arr);
}

/* ===== Renderer ===== */

int renderer_init(Renderer *r, const VoxChart *chart, ChartMetrics *metrics, FontCtx *font) {
    r->chart   = chart;
    r->metrics = metrics;
    r->font    = font;
    int max_m = chart->end_position.measure + 1;
    if (max_m < 2) max_m = 2;
    return event_index_build(&r->idx, chart, metrics, max_m);
}

void renderer_free(Renderer *r) {
    event_index_free(&r->idx);
}

void renderer_measure_size(Renderer *r, int measure, int *out_w, int *out_h) {
    int lh = lane_height_for_measure(r->metrics, measure);
    *out_w = image_width();
    *out_h = lh + 2 * MARGIN;
}

/* Render a single measure */
ImgBuf renderer_render_measure(Renderer *r, int measure) {
    int lh = lane_height_for_measure(r->metrics, measure);
    int w  = image_width();
    int h  = lh + 2 * MARGIN;
    int top    = MARGIN;
    int bottom = lh + MARGIN;

    int bt[4][2];
    compute_bt_columns(bt);

    /* Allocate layer buffers */
    ImgBuf bg       = imgbuf_alloc(w, h);
    ImgBuf fx_long  = imgbuf_alloc(w, h);
    ImgBuf bt_long  = imgbuf_alloc(w, h);
    ImgBuf fx_chip  = imgbuf_alloc(w, h);
    ImgBuf bt_chip  = imgbuf_alloc(w, h);
    ImgBuf laser_l  = imgbuf_alloc(w, h);
    ImgBuf laser_r  = imgbuf_alloc(w, h);

    /* Background */
    draw_background(&bg, h, top, bottom, bt, r->metrics, measure);

    /* Button events */
    const EventIndex *idx = &r->idx;
    if (measure <= idx->max_measure) {
        /* Chips */
        int ci_base = idx->chips_idx[measure];
        int ci_cnt  = idx->chips_cnt[measure];
        for (int i = 0; i < ci_cnt; i++) {
            const ButtonEvent *ev = &idx->chips[ci_base + i];
            double frac = metrics_measure_frac(r->metrics, ev->time);
            int is_bt = is_bt_track(ev->track);
            draw_chip(is_bt ? &bt_chip : &fx_chip, h, top, bottom, bt,
                      ev->track, frac, is_bt);
        }
        /* Holds */
        int hi_base = idx->holds_idx[measure];
        int hi_cnt  = idx->holds_cnt[measure];
        for (int i = 0; i < hi_cnt; i++) {
            const MeasureHoldEvent *ev = &idx->holds[hi_base + i];
            int is_bt = is_bt_track(ev->track);
            draw_long(is_bt ? &bt_long : &fx_long, h, top, bottom, bt,
                      ev->track, ev->start_frac, ev->end_frac, is_bt);
        }
        /* Lasers */
        int li_base = idx->laser_idx[measure];
        int li_cnt  = idx->laser_cnt[measure];
        for (int i = 0; i < li_cnt; i++) {
            const MeasureLaserSegment *seg  = &idx->lasers[li_base + i];
            const MeasureLaserSegment *next = (i + 1 < li_cnt) ? &idx->lasers[li_base + i + 1] : NULL;
            ImgBuf *laser_buf = (seg->track == VOL_L_TRACK) ? &laser_l : &laser_r;
            draw_laser_segment(laser_buf, h, top, bottom, seg, next);
        }
    }

    /* Composite layers: bg -> fx_long -> bt_long -> fx_chip -> bt_chip -> laser_l -> laser_r */
    ImgBuf composed = imgbuf_alloc(w, h);
    imgbuf_composite(&composed, &bg);
    imgbuf_composite(&composed, &fx_long);
    imgbuf_composite(&composed, &bt_long);
    imgbuf_composite(&composed, &fx_chip);
    imgbuf_composite(&composed, &bt_chip);
    imgbuf_composite(&composed, &laser_l);
    imgbuf_composite(&composed, &laser_r);

    /* Labels (drawn directly onto composed) */
    draw_measure_number(&composed, h, r->font, measure, top, bt);
    draw_bpm_labels(&composed, h, r->font, r->metrics, measure, top, bottom, bt, idx);

    /* Free layer buffers */
    imgbuf_free(&bg);     imgbuf_free(&fx_long); imgbuf_free(&bt_long);
    imgbuf_free(&fx_chip);imgbuf_free(&bt_chip);
    imgbuf_free(&laser_l);imgbuf_free(&laser_r);

    return composed;
}

/* Render the full chart */
ImgBuf renderer_render_chart(Renderer *r,
                              int start_measure, int end_measure,
                              int measures_per_column, int column_gap) {
    int measure_count = end_measure - start_measure + 1;

    /* Pre-compute lane heights for each measure */
    int *lane_heights = (int *)malloc((size_t)measure_count * sizeof(int));
    for (int i = 0; i < measure_count; i++) {
        lane_heights[i] = lane_height_for_measure(r->metrics, start_measure + i);
    }

    int img_w = image_width();

    /* Build column structure */
    int num_cols = (measure_count + measures_per_column - 1) / measures_per_column;

    /* Compute column heights */
    int *col_heights = (int *)malloc((size_t)num_cols * sizeof(int));
    for (int c = 0; c < num_cols; c++) {
        int col_start = c * measures_per_column;
        int col_end   = col_start + measures_per_column;
        if (col_end > measure_count) col_end = measure_count;
        int sum = 0;
        for (int i = col_start; i < col_end; i++) sum += lane_heights[i];
        col_heights[c] = sum + 2 * MARGIN;
    }

    int max_height = 0;
    for (int c = 0; c < num_cols; c++) {
        if (col_heights[c] > max_height) max_height = col_heights[c];
    }

    int total_width  = num_cols * img_w + (num_cols > 1 ? (num_cols - 1) * column_gap : 0);
    int total_height = max_height;

    ImgBuf final_buf = imgbuf_alloc(total_width, total_height);

    for (int col = 0; col < num_cols; col++) {
        int x_offset   = col * (img_w + column_gap);
        int col_start  = col * measures_per_column;
        int col_end    = col_start + measures_per_column;
        if (col_end > measure_count) col_end = measure_count;

        /* y_offsets: reversed (last measure in column at top) */
        /* reversed_indices for this column: col_end-1 down to col_start */
        /* y_cursor accumulates top-down */
        int *y_offsets = (int *)malloc((size_t)measure_count * sizeof(int));
        int y_cursor = 0;
        for (int i = col_end - 1; i >= col_start; i--) {
            y_offsets[i] = y_cursor;
            y_cursor += lane_heights[i];
        }

        /* Composite in music order (col_start to col_end-1) */
        for (int i = col_start; i < col_end; i++) {
            int measure = start_measure + i;
            ImgBuf m_img = renderer_render_measure(r, measure);
            imgbuf_composite_at(&final_buf, &m_img, x_offset, y_offsets[i]);
            imgbuf_free(&m_img);
        }
        free(y_offsets);
    }

    free(lane_heights);
    free(col_heights);
    return final_buf;
}
