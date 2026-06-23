#include "renderer.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void renderer_blend_glyph(Renderer *ren, GlyphInfo *glyph, int x, int y);
static GlyphInfo *renderer_get_glyph(Renderer *ren, unsigned long charcode);
static void renderer_fill_rect(Renderer *ren, int x, int y, int w, int h, COLORREF color);
static void renderer_draw_cursor(Renderer *ren, TextBuffer *tb);
static void renderer_draw_line_numbers(Renderer *ren, TextBuffer *tb);
static void renderer_draw_selection(Renderer *ren, TextBuffer *tb);

int renderer_init(Renderer *ren, const char *font_path, int font_size) {
    memset(ren, 0, sizeof(Renderer));
    ren->font_size = font_size;
    ren->bg_color = RGB(0x1E, 0x1E, 0x2E);
    ren->text_color = RGB(0xC0, 0xC0, 0xC0);
    ren->cursor_color = RGB(0xFF, 0xFF, 0xFF);
    ren->selection_color = RGB(0x40, 0x40, 0x70);
    ren->selection_text_color = RGB(0xFF, 0xFF, 0xFF);
    ren->line_number_color = RGB(0xFF, 0xFF, 0xFF); //RGB(0x80, 0x80, 0x80);
    ren->line_number_bg = RGB(0x25, 0x25, 0x35);
    ren->text_area_left = 60;

    FT_Error err = FT_Init_FreeType(&ren->ft_lib);
    if (err) return -1;

    err = FT_New_Face(ren->ft_lib, font_path, 0, &ren->ft_face);
    if (err) {
        FT_Done_FreeType(ren->ft_lib);
        return -1;
    }

    FT_Select_Charmap(ren->ft_face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(ren->ft_face, 0, font_size);

    ren->line_height = (ren->ft_face->size->metrics.height >> 6);
    ren->ascender = (ren->ft_face->size->metrics.ascender >> 6);
    ren->descender = (ren->ft_face->size->metrics.descender >> 6);

    if (ren->line_height < font_size) ren->line_height = font_size + 2;

    memset(ren->glyph_cached, 0, sizeof(ren->glyph_cached));

    ren->buf_width = 800;
    ren->buf_height = 600;
    ren->pixel_buf = (unsigned char *)malloc(ren->buf_width * ren->buf_height * 4);
    ren->bmi.biSize = sizeof(BITMAPINFOHEADER);
    ren->bmi.biWidth = ren->buf_width;
    ren->bmi.biHeight = -ren->buf_height;
    ren->bmi.biPlanes = 1;
    ren->bmi.biBitCount = 32;
    ren->bmi.biCompression = BI_RGB;

    return 0;
}

void renderer_free(Renderer *ren) {
    for (int i = 0; i < MAX_GLYPHS_CACHE; i++) {
        if (ren->glyph_cached[i]) {
            free(ren->glyphs[i].bitmap);
        }
    }
    if (ren->ft_face) FT_Done_Face(ren->ft_face);
    if (ren->ft_lib) FT_Done_FreeType(ren->ft_lib);
    free(ren->pixel_buf);
}

void renderer_resize(Renderer *ren, int width, int height) {
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;
    free(ren->pixel_buf);
    ren->buf_width = width;
    ren->buf_height = height;
    ren->pixel_buf = (unsigned char *)malloc(width * height * 4);
    ren->bmi.biWidth = width;
    ren->bmi.biHeight = -height;
}

void renderer_invalidate_glyph_cache(Renderer *ren) {
    for (int i = 0; i < MAX_GLYPHS_CACHE; i++) {
        if (ren->glyph_cached[i]) {
            free(ren->glyphs[i].bitmap);
            ren->glyph_cached[i] = 0;
        }
    }
}

// This function retrieves the glyph information for a given character code. It checks if the glyph is already cached, and if not, it loads and renders the glyph using FreeType, then caches it for future use. The returned GlyphInfo contains the bitmap data and metrics needed to render the character on the screen.
static GlyphInfo *renderer_get_glyph(Renderer *ren, unsigned long charcode) {
    if (charcode < MAX_GLYPHS_CACHE && ren->glyph_cached[charcode]) {
        return &ren->glyphs[charcode];
    }

    FT_UInt glyph_index = FT_Get_Char_Index(ren->ft_face, charcode);
    if (glyph_index == 0 && charcode != 0) return NULL;

    FT_Error err = FT_Load_Glyph(ren->ft_face, glyph_index, FT_LOAD_DEFAULT);
    if (err) return NULL;

    err = FT_Render_Glyph(ren->ft_face->glyph, FT_RENDER_MODE_NORMAL);
    if (err) return NULL;

    FT_Bitmap *bmp = &ren->ft_face->glyph->bitmap;

    if (charcode < MAX_GLYPHS_CACHE) {
        GlyphInfo *gi = &ren->glyphs[charcode];
        gi->width = bmp->width;
        gi->height = bmp->rows;
        gi->bearing_x = ren->ft_face->glyph->bitmap_left;
        gi->bearing_y = ren->ft_face->glyph->bitmap_top;
        gi->advance = ren->ft_face->glyph->advance.x >> 6;

        if (bmp->width > 0 && bmp->rows > 0) {
            gi->bitmap = (unsigned char *)malloc(bmp->width * bmp->rows);
            for (unsigned int row = 0; row < bmp->rows; row++) {
                memcpy(gi->bitmap + row * bmp->width,
                       bmp->buffer + row * bmp->pitch,
                       bmp->width);
            }
        } else {
            gi->bitmap = NULL;
        }

        ren->glyph_cached[charcode] = 1;
        return gi;
    }

    return NULL;
}

// Glyph bitmap is a grayscale alpha mask. We need to blend it with the background color.
// The position (x, y) is the top-left corner of the glyph's bounding box. We need to account for the bearing.
// The final position to draw the bitmap is (x + bearing_x, y - bearing_y).
// We also need to ensure we don't write outside the pixel buffer bounds.
// The glyph bitmap has dimensions (glyph->width, glyph->height) and is stored in glyph->bitmap as a 1D array.
// Each pixel in the bitmap is an alpha value from 0 to 255. We will blend it with the background color using the text color.  
// Blending formula: out_color = bg_color * (1 - alpha) + text_color * alpha
// The pixel buffer is in BGRA format, so we need to write the colors accordingly.
// surf_x and surf_y are the top-left corner of the glyph bitmap on the pixel buffer.
// bearing_x is the horizontal offset from the cursor position to the left edge of the glyph bitmap.
// bearing_y is the vertical offset from the baseline to the top edge of the glyph bitmap. Since y is the baseline, we subtract bearing_y to get the top edge position.
static void renderer_blend_glyph(Renderer *ren, GlyphInfo *glyph, int x, int y) {

    int surf_x = x + glyph->bearing_x;
    int surf_y = y - glyph->bearing_y + ren->ascender;
    int gw = glyph->width;
    int gh = glyph->height;
    MyDebugOutput(L"BG: Blending glyph at x=%d, y=%d (surf_x=%d, surf_y=%d, gw=%d, gh=%d)\n", x, y, surf_x, surf_y, gw, gh);
    for (int gy = 0; gy < gh; gy++) {
        int py = surf_y + gy;
        if (py < 0 || py >= ren->buf_height) continue;
        for (int gx = 0; gx < gw; gx++) {
            int px = surf_x + gx;
            if (px >= ren->buf_width) continue;

            unsigned char a = glyph->bitmap[gy * gw + gx];
            if (a == 0) continue;

            float alpha = a / 255.0f;
            int offset = (py * ren->buf_width + px) * 4;

            unsigned char bg_r = ren->pixel_buf[offset + 2];
            unsigned char bg_g = ren->pixel_buf[offset + 1];
            unsigned char bg_b = ren->pixel_buf[offset + 0];

            unsigned char fg_r = GetRValue(ren->text_color);
            unsigned char fg_g = GetGValue(ren->text_color);
            unsigned char fg_b = GetBValue(ren->text_color);

            ren->pixel_buf[offset + 0] = (unsigned char)(bg_b * (1 - alpha) + fg_b * alpha);
            ren->pixel_buf[offset + 1] = (unsigned char)(bg_g * (1 - alpha) + fg_g * alpha);
            ren->pixel_buf[offset + 2] = (unsigned char)(bg_r * (1 - alpha) + fg_r * alpha);
            ren->pixel_buf[offset + 3] = 255;
        }
    }
}

static void renderer_fill_rect(Renderer *ren, int x, int y, int w, int h, COLORREF color) {
    for (int ry = y; ry < y + h && ry < ren->buf_height; ry++) {
        if (ry < 0) continue;
        for (int rx = x; rx < x + w && rx < ren->buf_width; rx++) {
            if (rx < 0) continue;
            int offset = (ry * ren->buf_width + rx) * 4;
            ren->pixel_buf[offset + 0] = GetBValue(color);
            ren->pixel_buf[offset + 1] = GetGValue(color);
            ren->pixel_buf[offset + 2] = GetRValue(color);
            ren->pixel_buf[offset + 3] = 255;
        }
    }
}

static void renderer_draw_cursor(Renderer *ren, TextBuffer *tb) {
    int line = tb_get_line_number(tb, tb->cursor);
    int col = tb_get_line_col(tb, tb->cursor);

    int x = col * (ren->ft_face->size->metrics.max_advance >> 6) + ren->text_area_left - tb->scroll_x;
    int y = (int)line * ren->line_height - tb->scroll_y;

    if (x >= ren->text_area_left && x < ren->buf_width && y >= 0 && y < ren->buf_height) {
        renderer_fill_rect(ren, x, y, 2, ren->line_height, ren->cursor_color);
    }
}

// This function is responsible for drawing the line numbers on the left side of the editor. It calculates the total number of lines in the text buffer, fills the background for the line number area, and then renders each line number using the cached glyphs. The line numbers are right-aligned within the line number area, and their color is set to a different color than the main text for better visibility.
static void renderer_draw_line_numbers(Renderer *ren, TextBuffer *tb) {
    int line_num_width = ren->text_area_left;
    int advance = ren->ft_face->size->metrics.max_advance >> 6;
    int total_lines = 1;
    for (int i = 0; i < tb->len; i++) {
        if (tb->data[i] == '\n') total_lines++;
    }

    renderer_fill_rect(ren, 0, 0, line_num_width, ren->buf_height, ren->line_number_bg);
    MyDebugOutput(L"DL: Total lines: %d, width: %d, height: %d, color: %x\n", total_lines, line_num_width, ren->buf_height, ren->line_number_bg);

    int cur_line = 0;
    int y = -tb->scroll_y;
    char num_buf[16];
    for (int line = 0; line < total_lines; line++) {
        int draw_y = y + line * ren->line_height;
        if (draw_y + ren->line_height < 0) continue;
        if (draw_y > ren->buf_height) break;

        _snprintf(num_buf, sizeof(num_buf), "%d", line + 1);
        // Calculate the x position to right-align the line number within the line number area. We take the width of the line number text and subtract it from the total line number area width, then add a small padding (10 pixels) to keep it away from the edge.
        int num_x = ren->text_area_left - 8 - (int)strlen(num_buf) * advance;

        MyDebugOutput(L"DL: Drawing line number '%c' at line %d (x=%d, y=%d)\n", num_buf[0], line + 1, num_x, draw_y);

        // Save the current text color, set it to the line number color, draw the line number, and then restore the original text color. This ensures that the line numbers are drawn in a different color than the main text, improving readability.
        COLORREF old_text = ren->text_color;
        ren->text_color = ren->line_number_color;
        for (int c = 0; num_buf[c]; c++) {
            GlyphInfo *glyph = renderer_get_glyph(ren, (unsigned long)num_buf[c]);
            if (glyph) {
                renderer_blend_glyph(ren, glyph, num_x, draw_y);
                num_x += glyph->advance;
            }
        }
        ren->text_color = old_text;
    }
}

// This function performs hit testing to determine which character position corresponds to the given mouse coordinates. It calculates the line and column based on the mouse position and the current scroll offset, then converts that to a character index in the text buffer. This allows the editor to move the cursor or update the selection based on where the user clicks or drags the mouse.
int renderer_hit_test(Renderer *ren, TextBuffer *tb, int mouse_x, int mouse_y) {
    int line_num_width = ren->text_area_left;
    int advance = ren->ft_face->size->metrics.max_advance >> 6;
    int adjusted_x = mouse_x + tb->scroll_x;
    int adjusted_y = mouse_y + tb->scroll_y;

    if (adjusted_x < line_num_width) adjusted_x = line_num_width;

    int target_line = (int)(adjusted_y / ren->line_height);
    int target_col = (int)((adjusted_x - line_num_width) / (advance > 0 ? advance : 1));

    int total_lines = 1;
    for (int i = 0; i < tb->len; i++) {
        if (tb->data[i] == '\n') total_lines++;
    }
    if (target_line >= total_lines) return tb->len;

    return tb_pos_from_line_col(tb, target_line, target_col);
}

// This function draws the selection highlight for the selected text range. It calculates the pixel coordinates for the selection based on the line and column of the selected text, and fills a rectangle with the selection color. The function handles multi-line selections and ensures that the selection highlight is only drawn within the visible area of the editor.
static void renderer_draw_selection(Renderer *ren, TextBuffer *tb) {
    if (!tb_has_selection(tb)) return;

    int sel_start, sel_end;
    tb_get_selection_range(tb, &sel_start, &sel_end);

    int line_num_width = ren->text_area_left;
    int advance = ren->ft_face->size->metrics.max_advance >> 6;

    int cur_line = 0;
    int x = line_num_width;
    int y = 0;

    for (int i = 0; i < sel_end && i < tb->len; ) {
        int line_start = tb_get_line_start(tb, i);
        int line_end_pos = tb_get_line_end(tb, i);

        if (line_end_pos > sel_start || line_start < sel_end) {
            int draw_y = y - tb->scroll_y;
            if (draw_y + ren->line_height >= 0 && draw_y < ren->buf_height) {
                int eff_start = line_start > sel_start ? line_start : sel_start;
                int eff_end = line_end_pos < sel_end ? line_end_pos : sel_end;

                int sel_x1 = line_num_width + (int)(eff_start - line_start) * advance - tb->scroll_x;
                int sel_x2 = line_num_width + (int)(eff_end - line_start) * advance - tb->scroll_x;

                if (sel_x1 < line_num_width) sel_x1 = line_num_width;
                if (sel_x2 > ren->buf_width) sel_x2 = ren->buf_width;

                if (sel_x2 > sel_x1) {
                    renderer_fill_rect(ren, sel_x1, draw_y, sel_x2 - sel_x1, ren->line_height, ren->selection_color);
                }
            }
        }

        y += ren->line_height;
        i = line_end_pos + 1;
    }
}

// This is the main rendering function that draws the entire text buffer onto the pixel buffer. It first fills the background, then draws line numbers and selection highlights. It iterates through each character in the text buffer, retrieves the corresponding glyph, and blends it onto the pixel buffer at the correct position. Finally, it uses Windows API to blit the pixel buffer onto the window's device context.
void renderer_render(Renderer *ren, TextBuffer *tb, HWND hwnd) {
    
    COLORREF bg = ren->bg_color;
    for (int i = 0; i < ren->buf_width * ren->buf_height * 4; i += 4) {
        ren->pixel_buf[i + 0] = GetBValue(bg);
        ren->pixel_buf[i + 1] = GetGValue(bg);
        ren->pixel_buf[i + 2] = GetRValue(bg);
        ren->pixel_buf[i + 3] = 255;
    }

    MyDebugOutput(L"RR:Starting render: buf_width=%d, buf_height=%d\n", ren->buf_width, ren->buf_height);

    renderer_draw_line_numbers(ren, tb);
    renderer_draw_selection(ren, tb);

    int line_num_width = ren->text_area_left;
    int advance = ren->ft_face->size->metrics.max_advance >> 6;

    int cur_line = 0;
    int x = line_num_width;
    int y = 0 - tb->scroll_y;

    int sel_start = 0, sel_end = 0;
    int in_selection = 0;
    if (tb_has_selection(tb)) {
        tb_get_selection_range(tb, &sel_start, &sel_end);
        in_selection = 1;
    }

    for (int i = 0; i <= tb->len; i++) {
        if (i > 0 && tb->data[i] == '\n') {
            cur_line++;
            x = line_num_width;
            y += ren->line_height;
            continue;
        }

        if (i < tb->len) {
            unsigned long ch = (unsigned long)tb->data[i];
            GlyphInfo *glyph = renderer_get_glyph(ren, ch);
            if (glyph) {
                int draw_x = x - tb->scroll_x;
                int draw_y = y - tb->scroll_y;
                if (draw_y + ren->line_height >= 0 && draw_y < ren->buf_height) {
                    COLORREF old_text = ren->text_color;
                    if (in_selection && i >= sel_start && i < sel_end) {
                        ren->text_color = ren->selection_text_color;
                    }
                    MyDebugOutput (L"RR: Drawing char '%c' at line %zu, col %zu (x=%d, y=%d)\n", ch, cur_line, (x - line_num_width) / advance, draw_x, draw_y);
                    renderer_blend_glyph(ren, glyph, draw_x, draw_y);
                    ren->text_color = old_text;
                }
                x += glyph->advance;
            } else {
                x += advance;
            }
        }
    }

    if (!in_selection) {
        renderer_draw_cursor(ren, tb);
    }

      HDC hdc = GetDC(hwnd);
    SetDIBitsToDevice(hdc, 0, 0, ren->buf_width, ren->buf_height,
                      0, 0, 0, ren->buf_height, ren->pixel_buf,
                      (BITMAPINFO *)&ren->bmi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
}

