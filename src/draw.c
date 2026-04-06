#include "draw.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- ImgBuf ---- */

ImgBuf imgbuf_alloc(int w, int h) {
    ImgBuf b;
    b.width  = w;
    b.height = h;
    b.data   = (uint8_t *)calloc((size_t)(w * h), 4);
    return b;
}

void imgbuf_free(ImgBuf *buf) {
    free(buf->data);
    buf->data = NULL;
}

void imgbuf_clear(ImgBuf *buf) {
    memset(buf->data, 0, (size_t)(buf->width * buf->height * 4));
}

/* ---- pixel helpers ---- */

static inline void blend_at(uint8_t *dst, Color c) {
    uint32_t sa = c.a;
    if (sa == 0) return;
    if (sa == 255) {
        dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = 255;
        return;
    }
    uint32_t da  = dst[3];
    uint32_t inv = 255 - sa;
    uint32_t out_a = sa + (da * inv) / 255;
    if (out_a == 0) return;
    dst[0] = (uint8_t)((c.r * sa + (uint32_t)dst[0] * da * inv / 255) / out_a);
    dst[1] = (uint8_t)((c.g * sa + (uint32_t)dst[1] * da * inv / 255) / out_a);
    dst[2] = (uint8_t)((c.b * sa + (uint32_t)dst[2] * da * inv / 255) / out_a);
    dst[3] = (uint8_t)out_a;
}

/* PIL's ImageDraw paints directly (overwrite, no blend with canvas). */
static inline void put_at(uint8_t *dst, Color c) {
    dst[0] = c.r; dst[1] = c.g; dst[2] = c.b; dst[3] = c.a;
}

void draw_pixel(ImgBuf *dst, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= dst->width || y >= dst->height) return;
    put_at(dst->data + (y * dst->width + x) * 4, c);
}

/* ---- rectangle fill ---- */

void draw_rect_fill(ImgBuf *dst, int x1, int y1, int x2, int y2, Color c) {
    if (x1 > x2) { int t=x1; x1=x2; x2=t; }
    if (y1 > y2) { int t=y1; y1=y2; y2=t; }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= dst->width)  x2 = dst->width  - 1;
    if (y2 >= dst->height) y2 = dst->height - 1;
    if (x1 > x2 || y1 > y2) return;

    for (int y = y1; y <= y2; y++) {
        uint8_t *row = dst->data + (y * dst->width + x1) * 4;
        for (int x = x1; x <= x2; x++, row += 4) {
            row[0] = c.r; row[1] = c.g; row[2] = c.b; row[3] = c.a;
        }
    }
}

void draw_rect_outline(ImgBuf *dst, int x1, int y1, int x2, int y2, Color c, int lw) {
    if (lw <= 0) return;
    if (x1 > x2) { int t=x1; x1=x2; x2=t; }
    if (y1 > y2) { int t=y1; y1=y2; y2=t; }
    draw_rect_fill(dst, x1, y1, x2, y1+lw-1, c);          /* top */
    draw_rect_fill(dst, x1, y2-lw+1, x2, y2, c);           /* bottom */
    draw_rect_fill(dst, x1, y1, x1+lw-1, y2, c);           /* left */
    draw_rect_fill(dst, x2-lw+1, y1, x2, y2, c);           /* right */
}

void draw_rect(ImgBuf *dst, int x1, int y1, int x2, int y2,
               Color fill, Color outline, int outline_w) {
    draw_rect_fill(dst, x1, y1, x2, y2, fill);
    if (outline_w > 0)
        draw_rect_outline(dst, x1, y1, x2, y2, outline, outline_w);
}

/* ---- lines ---- */

void draw_hline(ImgBuf *dst, float x1f, float x2f, float pyf, Color c, int width) {
    int x1 = (int)floorf(x1f < x2f ? x1f : x2f);
    int x2 = (int)ceilf (x1f < x2f ? x2f : x1f);
    /* PIL: for width=2 at y, draws at y and y+1; i.e., from y-(width-1)/2 to y+width/2 */
    int y1 = (int)floorf(pyf) - (width - 1) / 2;
    int y2 = y1 + width - 1;
    draw_rect_fill(dst, x1, y1, x2, y2, c);
}

void draw_vline(ImgBuf *dst, float pxf, float y1f, float y2f, Color c, int width) {
    int y1 = (int)floorf(y1f < y2f ? y1f : y2f);
    int y2 = (int)ceilf (y1f < y2f ? y2f : y1f);
    /* PIL: for width=2 at x, draws at x and x+1; i.e., from x-(width-1)/2 to x+width/2 */
    int x1 = (int)floorf(pxf) - (width - 1) / 2;
    int x2 = x1 + width - 1;
    draw_rect_fill(dst, x1, y1, x2, y2, c);
}

/* Draw a line from (x0,y0) to (x1,y1) with given pixel width.
 * Uses a rectangle perpendicular to the line direction. */
void draw_line(ImgBuf *dst, float x0, float y0, float x1, float y1, Color c, int width) {
    if (width <= 0) return;
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx*dx + dy*dy);

    if (len < 0.5f) {
        /* Degenerate: just a dot */
        int xi = (int)roundf(x0);
        int yi = (int)roundf(y0);
        int half = width / 2;
        draw_rect_fill(dst, xi - half, yi - half, xi + half, yi + half, c);
        return;
    }

    /* Perpendicular unit vector */
    float nx = -dy / len;
    float ny =  dx / len;
    float hw = (float)width * 0.5f;

    /* Four corners of the thick line rectangle */
    float px[4], py[4];
    px[0] = x0 + nx * hw;  py[0] = y0 + ny * hw;
    px[1] = x0 - nx * hw;  py[1] = y0 - ny * hw;
    px[2] = x1 - nx * hw;  py[2] = y1 - ny * hw;
    px[3] = x1 + nx * hw;  py[3] = y1 + ny * hw;

    draw_poly_fill(dst, px, py, 4, c);
}

/* ---- polygon fill (scanline) ---- */

void draw_poly_fill(ImgBuf *dst, const float *xs, const float *ys, int n, Color c) {
    if (n < 3) return;

    float ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }

    int iy_min = (int)floorf(ymin);
    int iy_max = (int)ceilf(ymax);
    if (iy_min < 0) iy_min = 0;
    if (iy_max >= dst->height) iy_max = dst->height - 1;

    for (int iy = iy_min; iy <= iy_max; iy++) {
        float fy = (float)iy + 0.5f;
        float xi[32];
        int ni = 0;

        for (int j = 0; j < n && ni < 31; j++) {
            int k = (j + 1) % n;
            float y0 = ys[j], y1 = ys[k];
            float x0 = xs[j], x1 = xs[k];

            if ((y0 <= fy && fy < y1) || (y1 <= fy && fy < y0)) {
                float t = (fy - y0) / (y1 - y0);
                xi[ni++] = x0 + t * (x1 - x0);
            }
        }

        /* Sort intersections (small n, use insertion sort) */
        for (int a = 1; a < ni; a++) {
            float v = xi[a];
            int b = a - 1;
            while (b >= 0 && xi[b] > v) { xi[b+1] = xi[b]; b--; }
            xi[b+1] = v;
        }

        /* Fill between pairs */
        for (int p = 0; p + 1 < ni; p += 2) {
            int ix0 = (int)floorf(xi[p]);
            int ix1 = (int)ceilf (xi[p+1]);
            if (ix0 < 0) ix0 = 0;
            if (ix1 >= dst->width) ix1 = dst->width - 1;
            uint8_t *row = dst->data + (iy * dst->width + ix0) * 4;
            for (int x = ix0; x <= ix1; x++, row += 4) {
                row[0] = c.r; row[1] = c.g; row[2] = c.b; row[3] = c.a;
            }
        }
    }
}

/* ---- alpha compositing ---- */

void imgbuf_composite(ImgBuf *dst, const ImgBuf *src) {
    int n = dst->width * dst->height;
    uint8_t *d = dst->data;
    const uint8_t *s = src->data;
    for (int i = 0; i < n; i++, d += 4, s += 4) {
        uint32_t sa = s[3];
        if (sa == 0) continue;
        if (sa == 255) {
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            continue;
        }
        uint32_t da  = d[3];
        uint32_t inv = 255 - sa;
        uint32_t out_a = sa + (da * inv) / 255;
        if (out_a == 0) continue;
        d[0] = (uint8_t)((s[0] * sa + (uint32_t)d[0] * da * inv / 255) / out_a);
        d[1] = (uint8_t)((s[1] * sa + (uint32_t)d[1] * da * inv / 255) / out_a);
        d[2] = (uint8_t)((s[2] * sa + (uint32_t)d[2] * da * inv / 255) / out_a);
        d[3] = (uint8_t)out_a;
    }
}

void imgbuf_composite_at(ImgBuf *dst, const ImgBuf *src, int ox, int oy) {
    for (int sy = 0; sy < src->height; sy++) {
        int dy = oy + sy;
        if (dy < 0 || dy >= dst->height) continue;
        for (int sx = 0; sx < src->width; sx++) {
            int dx = ox + sx;
            if (dx < 0 || dx >= dst->width) continue;
            const uint8_t *s = src->data + (sy * src->width + sx) * 4;
            uint8_t       *d = dst->data + (dy * dst->width + dx) * 4;
            uint32_t sa = s[3];
            if (sa == 0) continue;
            if (sa == 255) {
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
                continue;
            }
            uint32_t da  = d[3];
            uint32_t inv = 255 - sa;
            uint32_t out_a = sa + (da * inv) / 255;
            if (out_a == 0) continue;
            d[0] = (uint8_t)((s[0] * sa + (uint32_t)d[0] * da * inv / 255) / out_a);
            d[1] = (uint8_t)((s[1] * sa + (uint32_t)d[1] * da * inv / 255) / out_a);
            d[2] = (uint8_t)((s[2] * sa + (uint32_t)d[2] * da * inv / 255) / out_a);
            d[3] = (uint8_t)out_a;
        }
    }
}

/* ---- font / text (stb_truetype) ---- */

#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/stb_truetype.h"

struct FontCtx {
    stbtt_fontinfo info;
    unsigned char *font_data;
};

static const char *font_search_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Arial.ttf",
    NULL
};

FontCtx *font_init(const char *font_path) {
    const char *path_to_try = font_path;
    unsigned char *buf = NULL;
    long len = 0;

    if (path_to_try) {
        FILE *f = fopen(path_to_try, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
            buf = (unsigned char *)malloc((size_t)len);
            if (buf) {
                size_t nr = fread(buf, 1, (size_t)len, f);
                if (nr != (size_t)len) { free(buf); buf = NULL; }
            }
            fclose(f);
        }
    }

    if (!buf) {
        for (int i = 0; font_search_paths[i]; i++) {
            FILE *f = fopen(font_search_paths[i], "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
            buf = (unsigned char *)malloc((size_t)len);
            if (buf) {
                size_t nr = fread(buf, 1, (size_t)len, f);
                if (nr != (size_t)len) { free(buf); buf = NULL; fclose(f); continue; }
                fclose(f); break;
            }
            fclose(f);
        }
    }

    if (!buf) return NULL;

    FontCtx *ctx = (FontCtx *)malloc(sizeof(FontCtx));
    if (!ctx) { free(buf); return NULL; }
    ctx->font_data = buf;
    if (!stbtt_InitFont(&ctx->info, buf, 0)) {
        free(buf); free(ctx); return NULL;
    }
    return ctx;
}

void font_free(FontCtx *ctx) {
    if (!ctx) return;
    free(ctx->font_data);
    free(ctx);
}

void font_measure(FontCtx *ctx, const char *text, float scale_px,
                  float *out_w, float *out_h) {
    if (!ctx || !text) { *out_w = 0; *out_h = 0; return; }
    float scale = stbtt_ScaleForPixelHeight(&ctx->info, scale_px);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&ctx->info, &ascent, &descent, &line_gap);
    *out_h = (float)(ascent - descent) * scale;

    float w = 0;
    for (const char *p = text; *p; p++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&ctx->info, (int)(unsigned char)*p, &adv, &lsb);
        w += (float)adv * scale;
        /* kern */
        if (*(p+1)) {
            w += (float)stbtt_GetCodepointKernAdvance(&ctx->info,
                    (int)(unsigned char)*p, (int)(unsigned char)*(p+1)) * scale;
        }
    }
    *out_w = w;
}

void font_draw(ImgBuf *dst, FontCtx *ctx, const char *text,
               float scale_px, float x, float y, Color c) {
    if (!ctx || !text) return;
    float scale = stbtt_ScaleForPixelHeight(&ctx->info, scale_px);
    int ascent, descent;
    stbtt_GetFontVMetrics(&ctx->info, &ascent, &descent, NULL);
    float baseline_y = y + (float)ascent * scale;

    float cx = x;
    for (const char *p = text; *p; p++) {
        int cp = (int)(unsigned char)*p;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&ctx->info, cp, &adv, &lsb);

        int bx0, by0, bx1, by1;
        stbtt_GetCodepointBitmapBox(&ctx->info, cp, scale, scale, &bx0, &by0, &bx1, &by1);
        int bw = bx1 - bx0, bh = by1 - by0;
        if (bw > 0 && bh > 0) {
            unsigned char *bitmap = (unsigned char *)malloc((size_t)(bw * bh));
            if (bitmap) {
                stbtt_MakeCodepointBitmap(&ctx->info, bitmap, bw, bh, bw, scale, scale, cp);
                for (int gy = 0; gy < bh; gy++) {
                    for (int gx = 0; gx < bw; gx++) {
                        int px = (int)(cx + bx0) + gx;
                        int py = (int)baseline_y + by0 + gy;
                        if (px < 0 || py < 0 || px >= dst->width || py >= dst->height) continue;
                        uint8_t alpha = bitmap[gy * bw + gx];
                        if (alpha == 0) continue;
                        Color pc = {c.r, c.g, c.b, (uint8_t)((uint32_t)c.a * alpha / 255)};
                        blend_at(dst->data + (py * dst->width + px) * 4, pc);
                    }
                }
                free(bitmap);
            }
        }

        cx += (float)adv * scale;
        if (*(p+1)) {
            cx += (float)stbtt_GetCodepointKernAdvance(&ctx->info, cp, (int)(unsigned char)*(p+1)) * scale;
        }
    }
}
