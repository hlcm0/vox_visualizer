#pragma once

#include "model.h"

/* Pre-computed metrics for a chart. */
typedef struct {
    const VoxChart *chart;
    int resolution;

    /* BPM timeline (sorted by chart_units) */
    int     num_bpm_entries;
    double *bpm_units;       /* absolute chart units of each BPM change */
    double *bpm_values;      /* BPM value */
    double *bpm_beat_units;  /* units per beat at that BPM change */

    /* Cached measure start units (1-indexed; index 0 unused).
     * Allocated to (end_measure+2) entries. */
    int     cache_size;
    double *measure_start_cache;  /* measure_start_cache[m] = chart units at start of measure m */
    double *measure_duration_cache; /* measure_duration_cache[m], or -1 if not yet computed */
} ChartMetrics;

void metrics_init(ChartMetrics *m, const VoxChart *chart);
void metrics_free(ChartMetrics *m);

/* Signature for a given measure number (1-based). */
Signature metrics_signature(const ChartMetrics *m, int measure);

/* beat_units = resolution * 4 / sig.note */
double sig_beat_units(Signature sig, int resolution);

/* measure_units for a given measure */
double sig_measure_units(Signature sig, int resolution);

/* Sum of units for measures 1..(measure-1) */
double metrics_measure_start_units(ChartMetrics *m, int measure);

/* Local units within measure for a TimePoint (beat/offset part) */
double metrics_local_units(ChartMetrics *m, TimePoint tp);

/* Absolute chart units for a TimePoint */
double metrics_chart_units(ChartMetrics *m, TimePoint tp);

/* Duration (seconds) of a measure */
double metrics_measure_duration(ChartMetrics *m, int measure);

/* Convert local measure units -> time fraction [0,1] */
double metrics_units_to_frac(ChartMetrics *m, int measure, double local_units);

/* Time fraction for a TimePoint within its measure */
double metrics_measure_frac(ChartMetrics *m, TimePoint tp);

/* Beat fractions for a measure (one entry per beat, returns via out array).
 * out must have room for at least sig.beat entries. Returns number written. */
int metrics_beat_fracs(ChartMetrics *m, int measure, double *out, int out_cap);

/* Split a ButtonEvent into hold segments.
 * out_segs must have room for enough MeasureHoldEvent entries.
 * Returns number of segments. */
int metrics_split_hold(ChartMetrics *m, const ButtonEvent *ev,
                       MeasureHoldEvent *out_segs, int out_cap);

/* Split laser nodes into segments.
 * Returns number of segments. */
int metrics_split_laser(ChartMetrics *m,
                        const LaserNode *nodes, int n_nodes,
                        MeasureLaserSegment *out_segs, int out_cap);
