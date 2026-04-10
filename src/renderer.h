#pragma once

#include "model.h"
#include "metrics.h"
#include "draw.h"

/* Pre-built per-measure event index */
typedef struct {
    /* Chips and holds per measure */
    int          *chips_idx;     /* chips_idx[m] = first chip for measure m in chips[] */
    int          *chips_cnt;     /* count */
    ButtonEvent  *chips;
    int           total_chips;

    int              *holds_idx;
    int              *holds_cnt;
    MeasureHoldEvent *holds;
    int               total_holds;

    /* Laser segments per measure */
    int                *laser_idx;
    int                *laser_cnt;
    MeasureLaserSegment *lasers;
    int                  total_lasers;

    /* BPM events per measure */
    int        *bpm_idx;
    int        *bpm_cnt;
    BpmEvent   *bpms_arr;
    int         total_bpms;

    int max_measure;
} EventIndex;

/* Build an event index for measures [1..max_measure]. */
int  event_index_build(EventIndex *idx,
                       const VoxChart *chart, ChartMetrics *metrics,
                       int max_measure);
void event_index_free(EventIndex *idx);

/* Rendering context */
typedef struct {
    const VoxChart *chart;
    ChartMetrics   *metrics;
    EventIndex      idx;
    FontCtx        *font;    /* may be NULL */
    /* Pre-allocated layer buffers (reused across measures) */
    ImgBuf layer_buf[7];     /* bg, fx_long, bt_long, fx_chip, bt_chip, laser_l, laser_r */
    int    layer_alloc_h;    /* height for which layers are allocated */
} Renderer;

int  renderer_init(Renderer *r, const VoxChart *chart, ChartMetrics *metrics,
                   FontCtx *font);
void renderer_free(Renderer *r);

/* Compute image dimensions for a single measure.
 * *out_w, *out_h = total image width/height including margins. */
void renderer_measure_size(Renderer *r, int measure, int *out_w, int *out_h);

/* Render a single measure into a newly-allocated ImgBuf.
 * Caller owns the returned buffer (imgbuf_free it). */
ImgBuf renderer_render_measure(Renderer *r, int measure);

/* Render the full chart (all columns) into a single ImgBuf.
 * start_measure / end_measure: 1-based, inclusive.
 * measures_per_column: how many measures per column.
 * column_gap: horizontal gap in pixels between columns.
 * Returns allocated ImgBuf (caller must imgbuf_free). */
ImgBuf renderer_render_chart(Renderer *r,
                              int start_measure, int end_measure,
                              int measures_per_column, int column_gap);
