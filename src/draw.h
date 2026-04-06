#pragma once

#include <stdint.h>

/* RGBA pixel */
typedef struct { uint8_t r, g, b, a; } Color;

/* Image buffer: flat RGBA row-major, (0,0) top-left. */
typedef struct {
    uint8_t *data;
    int width;
    int height;
} ImgBuf;

ImgBuf imgbuf_alloc(int w, int h);
void   imgbuf_free(ImgBuf *buf);
void   imgbuf_clear(ImgBuf *buf); /* fill with transparent black */

/* --- drawing primitives --- */

/* Blend src color over single pixel at (x,y). Clips to image bounds. */
void draw_pixel(ImgBuf *dst, int x, int y, Color c);

/* Fill axis-aligned rectangle. x1/y1 inclusive, x2/y2 inclusive. */
void draw_rect_fill(ImgBuf *dst, int x1, int y1, int x2, int y2, Color c);

/* Draw an outline rectangle (just the border) with given line width. */
void draw_rect_outline(ImgBuf *dst, int x1, int y1, int x2, int y2, Color c, int lw);

/* Draw a filled rectangle with outline. */
void draw_rect(ImgBuf *dst, int x1, int y1, int x2, int y2,
               Color fill, Color outline, int outline_w);

/* Draw a horizontal line y=py, x in [x1,x2], with given pixel width (thickness in y). */
void draw_hline(ImgBuf *dst, float x1, float x2, float py, Color c, int width);

/* Draw a vertical line x=px, y in [y1,y2], with given pixel width (thickness in x). */
void draw_vline(ImgBuf *dst, float px, float y1, float y2, Color c, int width);

/* Draw an anti-aliased line from (x0,y0) to (x1,y1) with given width. */
void draw_line(ImgBuf *dst, float x0, float y0, float x1, float y1, Color c, int width);

/* Fill a convex or general polygon with n vertices. */
void draw_poly_fill(ImgBuf *dst, const float *xs, const float *ys, int n, Color c);

/* Alpha-composite src over dst (Porter-Duff over). Both must be same size. */
void imgbuf_composite(ImgBuf *dst, const ImgBuf *src);

/* Alpha-composite src over dst at offset (ox,oy). Clips to dst bounds. */
void imgbuf_composite_at(ImgBuf *dst, const ImgBuf *src, int ox, int oy);

/* ---- text rendering ---- */

/* Opaque context for font rendering. Call font_init() once at startup. */
typedef struct FontCtx FontCtx;

/* Initialize font context. font_path may be NULL to search common system paths.
 * Returns non-NULL on success, NULL if no font found (text will be skipped). */
FontCtx *font_init(const char *font_path);
void     font_free(FontCtx *ctx);

/* Measure text width and height in pixels for the given scale (pixels high). */
void font_measure(FontCtx *ctx, const char *text, float scale_px,
                  float *out_w, float *out_h);

/* Draw text at (x,y) = top-left corner. */
void font_draw(ImgBuf *dst, FontCtx *ctx, const char *text,
               float scale_px, float x, float y, Color c);
