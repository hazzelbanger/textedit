#ifndef TEXT_BUFFER_H
#define TEXT_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#define TB_INITIAL_CAP 4096

// TextBuffer is a dynamic text buffer that supports basic text editing operations, cursor movement, and selection. It maintains the text data, cursor position, selection state, and scroll offsets for rendering. The API provides functions for inserting and deleting characters, moving the cursor in various directions, managing text selection, and converting between line/column and character positions.
typedef struct {
    char *data;
    int len;
    int cap;

    int cursor; // Current cursor position (character index)
    int selection_anchor; // The position where the selection started
    int has_selection; // Whether there is an active selection

    int scroll_x; // Horizontal scroll offset in pixels
    int scroll_y; // Vertical scroll offset in pixels
} TextBuffer;

void tb_init(TextBuffer *tb);
void tb_free(TextBuffer *tb);
void tb_insert_char(TextBuffer *tb, char ch);
void tb_insert_newline(TextBuffer *tb);
void tb_backspace(TextBuffer *tb);
void tb_delete(TextBuffer *tb);
void tb_move_cursor_left(TextBuffer *tb);
void tb_move_cursor_right(TextBuffer *tb);
void tb_move_cursor_up(TextBuffer *tb);
void tb_move_cursor_down(TextBuffer *tb);
void tb_move_cursor_home(TextBuffer *tb);
void tb_move_cursor_end(TextBuffer *tb);
int tb_get_line_start(TextBuffer *tb, int pos);
int tb_get_line_end(TextBuffer *tb, int pos);
int tb_get_line_number(TextBuffer *tb, int pos);
int tb_get_line_col(TextBuffer *tb, int pos);
int tb_has_selection(TextBuffer *tb);
void tb_get_selection_range(TextBuffer *tb, int *out_start, int *out_end);
void tb_delete_selection(TextBuffer *tb);
void tb_clear_selection(TextBuffer *tb);
void tb_select_all(TextBuffer *tb);
int tb_pos_from_line_col(TextBuffer *tb, int line, int col);
void tb_debug_print(TextBuffer *tb);

#endif
