#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* getopt_long */
#ifdef _WIN32
#  include <getopt.h>
#else
#  include <getopt.h>
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"

#include "model.h"
#include "parser.h"
#include "metrics.h"
#include "draw.h"
#include "renderer.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <input.vox> [output.png] [options]\n"
        "\n"
        "Options:\n"
        "  --start-measure N        First measure to render (default: 1)\n"
        "  --end-measure   N        Last measure to render (default: end of chart)\n"
        "  --measures-per-column N  Measures per column (default: 5)\n"
        "  --column-gap N           Horizontal gap in pixels (default: 80)\n"
        "  --font PATH              Path to a TrueType font file\n"
        "  --help                   Show this help\n",
        prog);
}

/* Build output path: replace extension with .png */
static void default_output_path(const char *input, char *out, size_t out_sz) {
    strncpy(out, input, out_sz - 1);
    out[out_sz - 1] = '\0';
    /* find last '.' */
    char *dot = strrchr(out, '.');
    char *sep = strrchr(out, '/');
#ifdef _WIN32
    char *sep2 = strrchr(out, '\\');
    if (sep2 && (!sep || sep2 > sep)) sep = sep2;
#endif
    if (dot && (!sep || dot > sep)) {
        *dot = '\0';
    }
    strncat(out, ".png", out_sz - strlen(out) - 1);
}

/* mkdir -p for a file's parent directory (single level) */
static void ensure_parent_dir(const char *path) {
    char tmp[4096];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';
    char *last_sep = strrchr(tmp, '/');
#ifdef _WIN32
    char *last_sep2 = strrchr(tmp, '\\');
    if (last_sep2 && (!last_sep || last_sep2 > last_sep)) last_sep = last_sep2;
#endif
    if (!last_sep) return;
    *last_sep = '\0';
    (void)0; /* ensure_parent_dir: mkdir -p is a no-op placeholder */
}

int main(int argc, char **argv) {
    int start_measure = 1;
    int end_measure   = -1; /* -1 = auto */
    int measures_per_column = 5;
    int column_gap = 80;
    const char *font_path = NULL;

    static struct option long_opts[] = {
        {"start-measure",       required_argument, 0, 's'},
        {"end-measure",         required_argument, 0, 'e'},
        {"measures-per-column", required_argument, 0, 'm'},
        {"column-gap",          required_argument, 0, 'g'},
        {"font",                required_argument, 0, 'f'},
        {"help",                no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, opt_idx = 0;
    while ((opt = getopt_long(argc, argv, "s:e:m:g:f:h", long_opts, &opt_idx)) != -1) {
        switch (opt) {
            case 's': start_measure       = atoi(optarg); break;
            case 'e': end_measure         = atoi(optarg); break;
            case 'm': measures_per_column = atoi(optarg); break;
            case 'g': column_gap          = atoi(optarg); break;
            case 'f': font_path           = optarg;       break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: input .vox file required\n");
        usage(argv[0]);
        return 1;
    }

    const char *input_path = argv[optind];
    char output_path[4096];
    if (optind + 1 < argc) {
        strncpy(output_path, argv[optind + 1], sizeof(output_path) - 1);
        output_path[sizeof(output_path)-1] = '\0';
    } else {
        default_output_path(input_path, output_path, sizeof(output_path));
    }

    /* Validate arguments */
    if (start_measure < 1) {
        fprintf(stderr, "Error: start_measure must be >= 1\n");
        return 1;
    }
    if (measures_per_column < 1) {
        fprintf(stderr, "Error: measures_per_column must be >= 1\n");
        return 1;
    }
    if (column_gap < 0) {
        fprintf(stderr, "Error: column_gap must be >= 0\n");
        return 1;
    }

    /* Parse chart */
    VoxChart *chart = (VoxChart *)calloc(1, sizeof(VoxChart));
    if (!chart) { fprintf(stderr, "Out of memory\n"); return 1; }

    if (vox_parse(input_path, chart) != 0) {
        fprintf(stderr, "Error: failed to parse '%s'\n", input_path);
        free(chart);
        return 1;
    }

    /* Determine end measure */
    int last_measure = (end_measure > 0) ? end_measure : chart->end_position.measure;
    if (last_measure < start_measure) {
        fprintf(stderr, "Error: no measures to render (start=%d, end=%d)\n",
                start_measure, last_measure);
        free(chart);
        return 1;
    }

    /* Build metrics */
    ChartMetrics *metrics = (ChartMetrics *)calloc(1, sizeof(ChartMetrics));
    if (!metrics) { fprintf(stderr, "Out of memory\n"); free(chart); return 1; }
    metrics_init(metrics, chart);

    /* Init font (optional) */
    FontCtx *font = font_init(font_path);
    if (!font) {
        fprintf(stderr, "Warning: no TrueType font found; text labels will be omitted.\n");
    }

    /* Init renderer */
    Renderer *rend = (Renderer *)calloc(1, sizeof(Renderer));
    if (!rend) { fprintf(stderr, "Out of memory\n"); goto cleanup; }
    renderer_init(rend, chart, metrics, font);

    /* Render */
    ImgBuf result = renderer_render_chart(rend, start_measure, last_measure,
                                          measures_per_column, column_gap);

    /* Write PNG */
    ensure_parent_dir(output_path);
    if (!stbi_write_png(output_path, result.width, result.height, 4,
                        result.data, result.width * 4)) {
        fprintf(stderr, "Error: failed to write PNG to '%s'\n", output_path);
        imgbuf_free(&result);
        renderer_free(rend);
        goto cleanup;
    }
    imgbuf_free(&result);

    printf("%s\n", output_path);

    renderer_free(rend);
cleanup:
    if (font)    font_free(font);
    if (metrics) { metrics_free(metrics); free(metrics); }
    free(chart);
    free(rend);
    return 0;
}
