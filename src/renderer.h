#ifndef RENDERER_H
#define RENDERER_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <windows.h>
#include "text_buffer.h"

#define MAX_GLYPHS_CACHE 128

// GlyphInfo struct holds the metrics and bitmap data for a single glyph. It includes the width and height of the glyph bitmap, the bearing (offset from the cursor position to the top-left corner of the bitmap), the advance (how much to move the cursor after drawing this glyph), and a pointer to the bitmap data itself.
typedef struct {
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
    unsigned char *bitmap;
} GlyphInfo;

// The Renderer struct encapsulates all the information and resources needed for rendering text. It includes the FreeType library and face for font handling, a cache for glyphs to optimize rendering, dimensions for line height and font metrics, a pixel buffer for drawing, and color settings for the background, text, cursor, and line numbers.
typedef struct {
    FT_Library ft_lib;
    FT_Face ft_face; // https://freetype.org/freetype2/docs/reference/ft2-face_creation.html
    int font_size;

    GlyphInfo glyphs[MAX_GLYPHS_CACHE];
    int glyph_cached[MAX_GLYPHS_CACHE];
    int line_height;
    int ascender;
    int descender;

    BITMAPINFOHEADER bmi;
    unsigned char *pixel_buf;
    int buf_width;
    int buf_height;

    COLORREF bg_color;
    COLORREF text_color;
    COLORREF cursor_color;
    COLORREF selection_color;
    COLORREF selection_text_color;
    COLORREF line_number_color;
    COLORREF line_number_bg;

    int text_area_left;
} Renderer;

int renderer_init(Renderer *ren, const char *font_path, int font_size);
void renderer_free(Renderer *ren);
void renderer_resize(Renderer *ren, int width, int height);
void renderer_render(Renderer *ren, TextBuffer *tb, HWND hwnd);
void renderer_invalidate_glyph_cache(Renderer *ren);
int renderer_hit_test(Renderer *ren, TextBuffer *tb, int mouse_x, int mouse_y);

#endif
