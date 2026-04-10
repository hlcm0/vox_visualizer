#include "metrics.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- helpers ---- */

double sig_beat_units(Signature sig, int resolution) {
    return (double)resolution * 4.0 / (double)sig.note;
}

double sig_measure_units(Signature sig, int resolution) {
    return (double)sig.beat * sig_beat_units(sig, resolution);
}

/* Binary search: find index of last entry in sorted array where values[i] <= key.
 * Returns -1 if key < values[0]. */
static int bisect_right_minus1(const double *values, int n, double key) {
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (values[mid] <= key) lo = mid + 1;
        else hi = mid;
    }
    return lo - 1;
}

/* ---- init / free ---- */

void metrics_init(ChartMetrics *m, const VoxChart *chart) {
    memset(m, 0, sizeof(*m));
    m->chart = chart;
    m->resolution = chart->beat_resolution;

    /* Build BPM timeline */
    if (chart->num_bpms > 0) {
        m->num_bpm_entries = chart->num_bpms;
    } else {
        m->num_bpm_entries = 1;
    }
    m->bpm_units      = (double *)calloc((size_t)m->num_bpm_entries, sizeof(double));
    m->bpm_values     = (double *)calloc((size_t)m->num_bpm_entries, sizeof(double));
    m->bpm_beat_units = (double *)calloc((size_t)m->num_bpm_entries, sizeof(double));

    if (chart->num_bpms > 0) {
        /* We must compute chart_units for BPM events which requires the signature info.
         * Build a temporary local ChartMetrics with just signature data (no BPM cache)
         * to compute measure_start_units without the BPM cache. */
        /* For now, store only the measure/beat/offset -> chart_units mapping.
         * We use a forward pass since signatures don't depend on BPM. */

        /* accumulate measure start units from scratch for BPM events */
        /* Build measure_start_units inline for the BPM timeline build */
        int max_bpm_measure = 0;
        for (int i = 0; i < chart->num_bpms; i++) {
            if (chart->bpms[i].time.measure > max_bpm_measure)
                max_bpm_measure = chart->bpms[i].time.measure;
        }

        /* Compute chart_units for each BPM event:
         * We walk measure by measure accumulating units. */
        /* Simple O(M * B) pass: for each BPM event compute its chart_units. */
        /* This is called once so O(M) is fine. */
        for (int i = 0; i < chart->num_bpms; i++) {
            TimePoint tp = chart->bpms[i].time;
            /* compute measure_start_units(tp.measure) fresh */
            double mstart = 0.0;
            for (int mu = 1; mu < tp.measure; mu++) {
                /* find sig for mu */
                Signature s = {4, 4};
                int best = -1;
                for (int si = 0; si < chart->num_sigs; si++) {
                    if (chart->sig_measure[si] <= mu) {
                        if (best < 0 || chart->sig_measure[si] > chart->sig_measure[best])
                            best = si;
                    }
                }
                if (best >= 0) s = chart->sig_value[best];
                mstart += sig_measure_units(s, m->resolution);
            }
            /* find sig for tp.measure */
            Signature sig = {4, 4};
            {
                int best = -1;
                for (int si = 0; si < chart->num_sigs; si++) {
                    if (chart->sig_measure[si] <= tp.measure) {
                        if (best < 0 || chart->sig_measure[si] > chart->sig_measure[best])
                            best = si;
                    }
                }
                if (best >= 0) sig = chart->sig_value[best];
            }
            double local = ((double)(tp.beat - 1)) * sig_beat_units(sig, m->resolution) + (double)tp.offset;
            m->bpm_units[i] = mstart + local;
            m->bpm_values[i] = chart->bpms[i].bpm;
            m->bpm_beat_units[i] = (double)m->resolution * 4.0 / (double)chart->bpms[i].division;
        }
    } else {
        m->bpm_units[0]      = 0.0;
        m->bpm_values[0]     = 120.0;
        m->bpm_beat_units[0] = (double)m->resolution;
    }

    /* Allocate measure start cache up front for a reasonable max */
    m->cache_size = 4096;
    m->measure_start_cache   = (double *)malloc((size_t)m->cache_size * sizeof(double));
    m->measure_duration_cache = (double *)malloc((size_t)m->cache_size * sizeof(double));
    for (int i = 0; i < m->cache_size; i++) {
        m->measure_start_cache[i]    = -1.0;
        m->measure_duration_cache[i] = -1.0;
    }
    m->measure_start_cache[1] = 0.0; /* measure 1 starts at 0 */
}

void metrics_free(ChartMetrics *m) {
    free(m->bpm_units);
    free(m->bpm_values);
    free(m->bpm_beat_units);
    free(m->measure_start_cache);
    free(m->measure_duration_cache);
}

/* ---- signature lookup ---- */

Signature metrics_signature(const ChartMetrics *m, int measure) {
    Signature result = {4, 4};
    int best_measure = -1;
    for (int i = 0; i < m->chart->num_sigs; i++) {
        int sm = m->chart->sig_measure[i];
        if (sm <= measure && sm > best_measure) {
            best_measure = sm;
            result = m->chart->sig_value[i];
        }
    }
    return result;
}

/* ---- measure start units (cached) ---- */

double metrics_measure_start_units(ChartMetrics *m, int measure) {
    if (measure < 1) return 0.0;

    /* grow cache if needed */
    if (measure >= m->cache_size) {
        int new_size = m->cache_size;
        while (new_size <= measure) new_size *= 2;
        m->measure_start_cache    = (double *)realloc(m->measure_start_cache, (size_t)new_size * sizeof(double));
        m->measure_duration_cache = (double *)realloc(m->measure_duration_cache, (size_t)new_size * sizeof(double));
        for (int i = m->cache_size; i < new_size; i++) {
            m->measure_start_cache[i]    = -1.0;
            m->measure_duration_cache[i] = -1.0;
        }
        m->cache_size = new_size;
    }

    if (m->measure_start_cache[measure] >= 0.0) {
        return m->measure_start_cache[measure];
    }

    /* compute iteratively from last cached measure */
    int start = 1;
    while (start < measure && m->measure_start_cache[start] >= 0.0) start++;
    /* start is the first measure we need to compute */
    double total = (start > 1) ? m->measure_start_cache[start - 1] : 0.0;
    /* add sig_measure_units for each measure from (start-1) up */
    for (int cur = start - 1; cur < measure; cur++) {
        if (cur < 1) { total = 0.0; continue; }
        Signature sig = metrics_signature(m, cur);
        double mu = sig_measure_units(sig, m->resolution);
        total += mu;
        if (cur + 1 < m->cache_size)
            m->measure_start_cache[cur + 1] = total;
    }

    m->measure_start_cache[measure] = total;
    return total;
}

/* ---- local units ---- */

double metrics_local_units(ChartMetrics *m, TimePoint tp) {
    Signature sig = metrics_signature(m, tp.measure);
    return (double)(tp.beat - 1) * sig_beat_units(sig, m->resolution) + (double)tp.offset;
}

double metrics_chart_units(ChartMetrics *m, TimePoint tp) {
    return metrics_measure_start_units(m, tp.measure) + metrics_local_units(m, tp);
}

/* ---- BPM at a given absolute chart_units ---- */

static void bpm_at_units(const ChartMetrics *m, double abs_units,
                         double *out_bpm, double *out_beat_units) {
    int idx = bisect_right_minus1(m->bpm_units, m->num_bpm_entries, abs_units);
    if (idx < 0) idx = 0;
    *out_bpm        = m->bpm_values[idx];
    *out_beat_units = m->bpm_beat_units[idx];
}

/* ---- measure time segments ---- */

/* A segment: [local_a, local_b) within the measure, with constant BPM. */
typedef struct { double local_a, local_b, bpm, beat_units; } MeasureSegment;

/* Fill segments[] for measure; returns count. */
static int get_measure_segments(ChartMetrics *m, int measure,
                                MeasureSegment *segs, int cap) {
    double m_start = metrics_measure_start_units(m, measure);
    Signature sig = metrics_signature(m, measure);
    double m_dur = sig_measure_units(sig, m->resolution);
    double m_end = m_start + m_dur;

    /* collect BPM change points inside (m_start, m_end) */
    double bp[256];
    int nbp = 0;
    bp[nbp++] = m_start;

    int lo = bisect_right_minus1(m->bpm_units, m->num_bpm_entries, m_start);
    if (lo < 0) lo = 0;
    /* include lo since it might equal m_start */
    for (int i = lo; i < m->num_bpm_entries && m->bpm_units[i] <= m_end; i++) {
        double u = m->bpm_units[i];
        if (u > m_start && u < m_end) {
            if (nbp < 255) bp[nbp++] = u;
        }
    }
    bp[nbp++] = m_end;

    int n = 0;
    for (int i = 0; i < nbp - 1 && n < cap; i++) {
        double a = bp[i], b = bp[i+1];
        double bpm, beat_u;
        bpm_at_units(m, a, &bpm, &beat_u);
        segs[n].local_a     = a - m_start;
        segs[n].local_b     = b - m_start;
        segs[n].bpm         = bpm;
        segs[n].beat_units  = beat_u;
        n++;
    }
    return n;
}

/* ---- measure duration ---- */

double metrics_measure_duration(ChartMetrics *m, int measure) {
    if (measure >= 1 && measure < m->cache_size && m->measure_duration_cache[measure] >= 0.0) {
        return m->measure_duration_cache[measure];
    }

    MeasureSegment segs[256];
    int n = get_measure_segments(m, measure, segs, 256);
    double total = 0.0;
    for (int i = 0; i < n; i++) {
        total += (segs[i].local_b - segs[i].local_a) * 60.0 / (segs[i].beat_units * segs[i].bpm);
    }
    if (measure >= 1 && measure < m->cache_size)
        m->measure_duration_cache[measure] = total;
    return total;
}

/* ---- units to time fraction ---- */

double metrics_units_to_frac(ChartMetrics *m, int measure, double local_units) {
    double total_dur = metrics_measure_duration(m, measure);
    if (total_dur <= 0.0) return 0.0;

    MeasureSegment segs[256];
    int n = get_measure_segments(m, measure, segs, 256);

    double elapsed = 0.0;
    for (int i = 0; i < n; i++) {
        double la = segs[i].local_a, lb = segs[i].local_b;
        if (local_units <= la) break;
        double seg_u = (local_units < lb ? local_units : lb) - la;
        elapsed += seg_u * 60.0 / (segs[i].beat_units * segs[i].bpm);
        if (local_units <= lb) break;
    }
    double frac = elapsed / total_dur;
    return frac < 1.0 ? frac : 1.0;
}

double metrics_measure_frac(ChartMetrics *m, TimePoint tp) {
    return metrics_units_to_frac(m, tp.measure, metrics_local_units(m, tp));
}

/* ---- beat fractions ---- */

int metrics_beat_fracs(ChartMetrics *m, int measure, double *out, int out_cap) {
    Signature sig = metrics_signature(m, measure);
    int n = sig.beat < out_cap ? sig.beat : out_cap;
    double bu = sig_beat_units(sig, m->resolution);
    for (int i = 0; i < n; i++) {
        out[i] = metrics_units_to_frac(m, measure, (double)i * bu);
    }
    return n;
}

/* ---- split hold events ---- */

int metrics_split_hold(ChartMetrics *m, const ButtonEvent *ev,
                       MeasureHoldEvent *out_segs, int out_cap) {
    int n = 0;
    double remaining = (double)(ev->hold_length > 0 ? ev->hold_length : 0);
    int cur_measure = ev->time.measure;
    double cur_units = metrics_local_units(m, ev->time);

    while (n < out_cap) {
        Signature sig = metrics_signature(m, cur_measure);
        double measure_units_total = sig_measure_units(sig, m->resolution);
        double start_frac = metrics_units_to_frac(m, cur_measure, cur_units);

        if (remaining <= 0.0) {
            double chip_end = cur_units + 4.0;
            if (chip_end > measure_units_total) chip_end = measure_units_total;
            double end_frac = metrics_units_to_frac(m, cur_measure, chip_end);
            if (end_frac > 1.0) end_frac = 1.0;
            out_segs[n].track       = ev->track;
            out_segs[n].measure     = cur_measure;
            out_segs[n].start_frac  = start_frac;
            out_segs[n].end_frac    = end_frac;
            out_segs[n].effect_code = ev->effect_code;
            n++;
            return n;
        }

        double available = measure_units_total - cur_units;
        if (remaining <= available) {
            double end_frac = metrics_units_to_frac(m, cur_measure, cur_units + remaining);
            out_segs[n].track       = ev->track;
            out_segs[n].measure     = cur_measure;
            out_segs[n].start_frac  = start_frac;
            out_segs[n].end_frac    = end_frac;
            out_segs[n].effect_code = ev->effect_code;
            n++;
            return n;
        }

        out_segs[n].track       = ev->track;
        out_segs[n].measure     = cur_measure;
        out_segs[n].start_frac  = start_frac;
        out_segs[n].end_frac    = 1.0;
        out_segs[n].effect_code = ev->effect_code;
        n++;
        remaining -= available;
        cur_measure++;
        cur_units = 0.0;
    }
    return n;
}

/* ---- split laser segments ---- */

int metrics_split_laser(ChartMetrics *m,
                        const LaserNode *nodes, int n_nodes,
                        MeasureLaserSegment *out_segs, int out_cap) {
    int n_out = 0;

    for (int i = 0; i + 1 < n_nodes && n_out < out_cap; i++) {
        const LaserNode *sn = &nodes[i];
        const LaserNode *en = &nodes[i + 1];

        if (sn->flag == 2 && en->flag == 1) continue;

        double start_units = metrics_chart_units(m, sn->time);
        double end_units   = metrics_chart_units(m, en->time);
        if (end_units < start_units) continue;
        int pair_is_horizontal = (start_units == end_units);

        int start_measure = sn->time.measure;
        int end_measure   = en->time.measure;

        for (int measure = start_measure; measure <= end_measure && n_out < out_cap; measure++) {
            double ms_units = metrics_measure_start_units(m, measure);
            Signature sig = metrics_signature(m, measure);
            double me_units = ms_units + sig_measure_units(sig, m->resolution);

            double seg_start_u = start_units > ms_units ? start_units : ms_units;
            double seg_end_u   = end_units   < me_units ? end_units   : me_units;

            if (seg_end_u < seg_start_u) continue;
            if (seg_end_u == seg_start_u && !pair_is_horizontal) continue;

            MeasureLaserSegment *seg = &out_segs[n_out];
            seg->track      = sn->track;
            seg->measure    = measure;
            seg->start_flag = sn->flag;
            seg->end_flag   = en->flag;
            seg->range_index = sn->range_index;

            if (seg_end_u > seg_start_u) {
                double duration   = end_units - start_units;
                double start_ratio = (seg_start_u - start_units) / duration;
                double end_ratio   = (seg_end_u   - start_units) / duration;
                seg->start_frac = metrics_units_to_frac(m, measure, seg_start_u - ms_units);
                seg->end_frac   = metrics_units_to_frac(m, measure, seg_end_u   - ms_units);
                seg->start_pos  = sn->position + (en->position - sn->position) * start_ratio;
                seg->end_pos    = sn->position + (en->position - sn->position) * end_ratio;
            } else {
                /* horizontal segment */
                seg->start_frac = metrics_units_to_frac(m, measure, seg_start_u - ms_units);
                seg->end_frac   = seg->start_frac;
                seg->start_pos  = sn->position;
                seg->end_pos    = en->position;
            }
            n_out++;
        }
    }
    return n_out;
}
