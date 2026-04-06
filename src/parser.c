#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ---------- helpers ---------- */

static int parse_int_field(const char *s, int def) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s) {
        /* try float truncation */
        double d = strtod(s, &end);
        if (end == s) return def;
        return (int)d;
    }
    return (int)v;
}

static double parse_laser_position(const char *s, int version) {
    char *end;
    double v = strtod(s, &end);
    if (end == s) return 0.0;
    if (version >= 11) {
        return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    }
    v /= 127.0;
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

static TimePoint parse_timepoint(const char *s) {
    TimePoint tp = {1, 1, 0};
    /* format: "measure,beat,offset" */
    char buf[64];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';
    char *p = buf;
    int fields[3] = {1, 1, 0};
    int i = 0;
    while (*p && i < 3) {
        /* parse integer */
        char *start = p;
        /* skip sign */
        if (*p == '-' || *p == '+') p++;
        while (*p && *p != ',') p++;
        int v = atoi(start);
        fields[i++] = v;
        if (*p == ',') p++;
    }
    tp.measure = fields[0];
    tp.beat    = fields[1];
    tp.offset  = fields[2];
    return tp;
}

/* ---------- sorting comparators ---------- */

static int cmp_laser_node(const void *a, const void *b) {
    const LaserNode *la = (const LaserNode *)a;
    const LaserNode *lb = (const LaserNode *)b;
    if (la->time.measure != lb->time.measure) return la->time.measure - lb->time.measure;
    if (la->time.beat    != lb->time.beat)    return la->time.beat    - lb->time.beat;
    if (la->time.offset  != lb->time.offset)  return la->time.offset  - lb->time.offset;
    return la->order - lb->order;
}

static int cmp_bpm_event(const void *a, const void *b) {
    const BpmEvent *ba = (const BpmEvent *)a;
    const BpmEvent *bb = (const BpmEvent *)b;
    if (ba->time.measure != bb->time.measure) return ba->time.measure - bb->time.measure;
    if (ba->time.beat    != bb->time.beat)    return ba->time.beat    - bb->time.beat;
    return ba->time.offset - bb->time.offset;
}

static int cmp_button_event(const void *a, const void *b) {
    const ButtonEvent *ba = (const ButtonEvent *)a;
    const ButtonEvent *bb = (const ButtonEvent *)b;
    if (ba->time.measure != bb->time.measure) return ba->time.measure - bb->time.measure;
    if (ba->time.beat    != bb->time.beat)    return ba->time.beat    - bb->time.beat;
    if (ba->time.offset  != bb->time.offset)  return ba->time.offset  - bb->time.offset;
    return ba->track - bb->track;
}

/* ---------- section name matching ---------- */

typedef enum {
    SEC_NONE = 0,
    SEC_FORMAT_VERSION,
    SEC_BEAT_RESOLUTION,
    SEC_BEAT_INFO,
    SEC_BPM_INFO,
    SEC_END_POSITION,
    SEC_TRACK1,
    SEC_TRACK2,
    SEC_TRACK3,
    SEC_TRACK4,
    SEC_TRACK5,
    SEC_TRACK6,
    SEC_TRACK7,
    SEC_TRACK8,
} Section;

static Section section_from_name(const char *name) {
    if (strcmp(name, "FORMAT VERSION") == 0) return SEC_FORMAT_VERSION;
    if (strcmp(name, "BEAT RESOLUTION") == 0) return SEC_BEAT_RESOLUTION;
    if (strcmp(name, "BEAT INFO") == 0)       return SEC_BEAT_INFO;
    if (strcmp(name, "BPM INFO") == 0)        return SEC_BPM_INFO;
    if (strcmp(name, "END POSITION") == 0)    return SEC_END_POSITION;
    if (strcmp(name, "TRACK1") == 0)          return SEC_TRACK1;
    if (strcmp(name, "TRACK2") == 0)          return SEC_TRACK2;
    if (strcmp(name, "TRACK3") == 0)          return SEC_TRACK3;
    if (strcmp(name, "TRACK4") == 0)          return SEC_TRACK4;
    if (strcmp(name, "TRACK5") == 0)          return SEC_TRACK5;
    if (strcmp(name, "TRACK6") == 0)          return SEC_TRACK6;
    if (strcmp(name, "TRACK7") == 0)          return SEC_TRACK7;
    if (strcmp(name, "TRACK8") == 0)          return SEC_TRACK8;
    return SEC_NONE;
}

static int section_to_track(Section sec) {
    switch (sec) {
        case SEC_TRACK1: return 1;
        case SEC_TRACK2: return 2;
        case SEC_TRACK3: return 3;
        case SEC_TRACK4: return 4;
        case SEC_TRACK5: return 5;
        case SEC_TRACK6: return 6;
        case SEC_TRACK7: return 7;
        case SEC_TRACK8: return 8;
        default: return 0;
    }
}

/* ---------- file reading ---------- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* ---------- line splitting ---------- */

/* Split by \t; fills parts[0..max_parts-1] with pointers into line (NUL-terminated in-place).
 * Returns number of parts found. */
static int split_tabs(char *line, char **parts, int max_parts) {
    int n = 0;
    char *p = line;
    while (n < max_parts) {
        parts[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

/* Trim leading/trailing whitespace and tabs in-place; returns pointer to trimmed start. */
static char *trim(char *s) {
    while (*s && (isspace((unsigned char)*s) || *s == '\t')) s++;
    char *end = s + strlen(s);
    while (end > s && (isspace((unsigned char)*(end-1)) || *(end-1) == '\t')) end--;
    *end = '\0';
    return s;
}

/* Convert section header to uppercase ASCII in-place. */
static void to_upper_ascii(char *s) {
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
    }
}

/* ---------- main parser ---------- */

int vox_parse(const char *path, VoxChart *chart) {
    /* Initialize chart to defaults */
    memset(chart, 0, sizeof(*chart));
    chart->version = 10;
    chart->beat_resolution = DEFAULT_BEAT_RESOLUTION;
    chart->end_position.measure = 1;
    chart->end_position.beat   = 1;
    chart->end_position.offset = 0;
    /* default 4/4 signature for measure 1 */
    chart->num_sigs = 1;
    chart->sig_measure[0] = 1;
    chart->sig_value[0].beat = 4;
    chart->sig_value[0].note = 4;

    size_t file_len = 0;
    char *file_buf = read_file(path, &file_len);
    if (!file_buf) {
        fprintf(stderr, "vox_parse: cannot open '%s'\n", path);
        return 1;
    }

    Section current_section = SEC_NONE;
    int line_order = 0;

    char *pos = file_buf;
    char *end = file_buf + file_len;

    while (pos < end) {
        /* find end of line */
        char *line_start = pos;
        char *newline = pos;
        while (newline < end && *newline != '\n' && *newline != '\r') newline++;
        /* NUL-terminate this line temporarily */
        char saved = *newline;
        *newline = '\0';

        /* advance pos past line ending */
        pos = newline;
        if (pos < end) {
            if (*pos == '\r' && *(pos+1) == '\n') pos += 2;
            else pos++;
        }

        /* trim the line */
        char *line = trim(line_start);
        if (!*line) goto next_line;

        /* skip comment lines */
        if (line[0] == '/' && line[1] == '/') goto next_line;

        line_order++;

        /* section header */
        if (line[0] == '#') {
            char *sec_name = trim(line + 1);
            if (strcmp(sec_name, "END") == 0) {
                current_section = SEC_NONE;
            } else {
                to_upper_ascii(sec_name);
                current_section = section_from_name(sec_name);
            }
            goto next_line;
        }

        /* parse based on current section */
        switch (current_section) {
            case SEC_FORMAT_VERSION: {
                int v = atoi(line);
                if (v > 0) chart->version = v;
                break;
            }
            case SEC_BEAT_RESOLUTION: {
                int v = atoi(line);
                if (v > 0) chart->beat_resolution = v;
                break;
            }
            case SEC_BEAT_INFO: {
                /* format: "measure,beat,offset\tbeat_num\tbeat_denom" */
                char *parts[4]; char tmp[256];
                strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
                int n = split_tabs(tmp, parts, 3);
                if (n < 3) break;
                for (int i = 0; i < n; i++) parts[i] = trim(parts[i]);
                TimePoint tp = parse_timepoint(parts[0]);
                int bnum = atoi(parts[1]);
                int bdenom = atoi(parts[2]);
                if (bnum <= 0 || bdenom <= 0) break;
                int m = tp.measure;
                /* find or insert signature */
                int found = 0;
                for (int i = 0; i < chart->num_sigs; i++) {
                    if (chart->sig_measure[i] == m) {
                        chart->sig_value[i].beat = bnum;
                        chart->sig_value[i].note = bdenom;
                        found = 1;
                        break;
                    }
                }
                if (!found && chart->num_sigs < MAX_SIGNATURES) {
                    chart->sig_measure[chart->num_sigs] = m;
                    chart->sig_value[chart->num_sigs].beat = bnum;
                    chart->sig_value[chart->num_sigs].note = bdenom;
                    chart->num_sigs++;
                }
                break;
            }
            case SEC_BPM_INFO: {
                /* format: "measure,beat,offset\tbpm[.xxx]\t[division[-]] */
                char *parts[4]; char tmp[256];
                strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
                int n = split_tabs(tmp, parts, 3);
                if (n < 2) break;
                for (int i = 0; i < n; i++) parts[i] = trim(parts[i]);
                double bpm = strtod(parts[1], NULL);
                if (bpm <= 0.0) break;
                int division = 4;
                if (n >= 3) {
                    char divbuf[32];
                    strncpy(divbuf, parts[2], sizeof(divbuf)-1);
                    divbuf[sizeof(divbuf)-1] = '\0';
                    /* strip trailing '-' */
                    int dl = (int)strlen(divbuf);
                    while (dl > 0 && divbuf[dl-1] == '-') divbuf[--dl] = '\0';
                    int d = atoi(divbuf);
                    if (d > 0) division = d;
                }
                if (chart->num_bpms < MAX_BPMS) {
                    BpmEvent *ev = &chart->bpms[chart->num_bpms++];
                    ev->time = parse_timepoint(parts[0]);
                    ev->bpm = bpm;
                    ev->division = division;
                }
                break;
            }
            case SEC_END_POSITION: {
                chart->end_position = parse_timepoint(line);
                break;
            }
            default: {
                int track_id = section_to_track(current_section);
                if (track_id == 0) break;

                char *parts[8]; char tmp[512];
                strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
                int n = split_tabs(tmp, parts, 7);
                for (int i = 0; i < n; i++) parts[i] = trim(parts[i]);

                if (is_laser_track(track_id)) {
                    if (n < 6) break;
                    int li = laser_track_index(track_id);
                    if (chart->num_laser_nodes[li] >= MAX_LASER_NODES) break;
                    LaserNode *node = &chart->laser_nodes[li][chart->num_laser_nodes[li]++];
                    node->track        = track_id;
                    node->time         = parse_timepoint(parts[0]);
                    node->position     = parse_laser_position(parts[1], chart->version);
                    node->flag         = parse_int_field(parts[2], 0);
                    node->impact       = parse_int_field(parts[3], 0);
                    node->filter_index = parse_int_field(parts[4], 0);
                    node->range_index  = parse_int_field(parts[5], 0);
                    node->order        = line_order;
                } else if (is_button_track(track_id)) {
                    if (n < 3) break;
                    if (chart->num_buttons >= MAX_BUTTONS) break;
                    ButtonEvent *ev = &chart->buttons[chart->num_buttons++];
                    ev->track       = track_id;
                    ev->time        = parse_timepoint(parts[0]);
                    ev->hold_length = parse_int_field(parts[1], 0);
                    ev->effect_code = parse_int_field(parts[2], 0);
                }
                break;
            }
        }

    next_line:
        *newline = saved; /* restore */
        (void)0;
    }

    free(file_buf);

    /* sort everything */
    qsort(chart->bpms, (size_t)chart->num_bpms, sizeof(BpmEvent), cmp_bpm_event);
    qsort(chart->buttons, (size_t)chart->num_buttons, sizeof(ButtonEvent), cmp_button_event);
    for (int li = 0; li < 2; li++) {
        qsort(chart->laser_nodes[li], (size_t)chart->num_laser_nodes[li], sizeof(LaserNode), cmp_laser_node);
    }

    return 0;
}
