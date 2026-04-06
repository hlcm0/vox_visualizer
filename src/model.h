#pragma once

#include <stddef.h>

/* Track IDs */
#define VOL_L_TRACK 1
#define FX_L_TRACK  2
#define BT_A_TRACK  3
#define BT_B_TRACK  4
#define BT_C_TRACK  5
#define BT_D_TRACK  6
#define FX_R_TRACK  7
#define VOL_R_TRACK 8

/* Default beat resolution */
#define DEFAULT_BEAT_RESOLUTION 48

/* Maximum array sizes */
#define MAX_SIGNATURES   512
#define MAX_BPMS         4096
#define MAX_BUTTONS      65536
#define MAX_LASER_NODES  32768   /* per track (2 tracks) */
#define MAX_HOLD_SEGS    131072
#define MAX_LASER_SEGS   131072

typedef struct {
    int measure, beat, offset;
} TimePoint;

typedef struct {
    int beat, note;
} Signature;

typedef struct {
    TimePoint time;
    double bpm;
    int division;
} BpmEvent;

typedef struct {
    int track;
    TimePoint time;
    int hold_length;
    int effect_code;
} ButtonEvent;

typedef struct {
    int track;
    TimePoint time;
    double position;
    int flag;
    int impact;
    int filter_index;
    int range_index;
    int order;
} LaserNode;

typedef struct {
    int track;
    int measure;
    double start_frac;
    double end_frac;
    int effect_code;
} MeasureHoldEvent;

typedef struct {
    int track;
    int measure;
    double start_frac;
    double end_frac;
    double start_pos;
    double end_pos;
    int start_flag;
    int end_flag;
    int range_index;
} MeasureLaserSegment;

/* Signature lookup: parallel arrays indexed 0..num_signatures-1 */
typedef struct {
    int           num_sigs;
    int           sig_measure[MAX_SIGNATURES];
    Signature     sig_value[MAX_SIGNATURES];

    int version;
    int beat_resolution;
    TimePoint end_position;

    int       num_bpms;
    BpmEvent  bpms[MAX_BPMS];

    int          num_buttons;
    ButtonEvent  buttons[MAX_BUTTONS];

    /* laser nodes: index 0 = VOL_L (track 1), index 1 = VOL_R (track 8) */
    int       num_laser_nodes[2];
    LaserNode laser_nodes[2][MAX_LASER_NODES];
} VoxChart;

static inline int is_laser_track(int t) { return t == VOL_L_TRACK || t == VOL_R_TRACK; }
static inline int is_fx_track(int t)    { return t == FX_L_TRACK  || t == FX_R_TRACK;  }
static inline int is_bt_track(int t)    { return t >= BT_A_TRACK  && t <= BT_D_TRACK;  }
static inline int is_button_track(int t){ return is_fx_track(t) || is_bt_track(t); }

/* laser_idx: VOL_L -> 0, VOL_R -> 1 */
static inline int laser_track_index(int t) { return (t == VOL_R_TRACK) ? 1 : 0; }

/* BT column index: BT_A->0 .. BT_D->3 */
static inline int bt_col_index(int t) { return t - BT_A_TRACK; }
