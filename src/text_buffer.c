#include "text_buffer.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>

void tb_init(TextBuffer *tb) {
    tb->cap = TB_INITIAL_CAP;
    tb->data = (char *)malloc(tb->cap);
    tb->data[0] = '\0';
    tb->len = 0;
    tb->cursor = 0;
    tb->selection_anchor = 0;
    tb->has_selection = 0;
    tb->scroll_x = 0;
    tb->scroll_y = 0;
}

void tb_free(TextBuffer *tb) {
    free(tb->data);
    tb->data = NULL;
    tb->len = 0;
    tb->cap = 0;
}

static void tb_ensure_cap(TextBuffer *tb, int needed) {
    if (tb->len + needed >= tb->cap) {
        int new_cap = tb->cap * 2;
        while (new_cap <= tb->len + needed) {
            new_cap *= 2;
        }
        tb->data = (char *)realloc(tb->data, new_cap);
        tb->cap = new_cap;
    }
}

void tb_debug_print(TextBuffer *tb) {
    MyDebugOutput(L"TB:%.*s\n", tb->len, tb->data);
}

int tb_has_selection(TextBuffer *tb) {
    return tb->has_selection && tb->selection_anchor != tb->cursor;
}

void tb_get_selection_range(TextBuffer *tb, int *out_start, int *out_end) {
    if (!tb_has_selection(tb)) {
        *out_start = tb->cursor;
        *out_end = tb->cursor;
        return;
    }
    if (tb->selection_anchor < tb->cursor) {
        *out_start = tb->selection_anchor;
        *out_end = tb->cursor;
    } else {
        *out_start = tb->cursor;
        *out_end = tb->selection_anchor;
    }
}

void tb_delete_selection(TextBuffer *tb) {
    if (!tb_has_selection(tb)) return;
    int sel_start, sel_end;
    tb_get_selection_range(tb, &sel_start, &sel_end);
    int del_len = sel_end - sel_start;
    memmove(tb->data + sel_start, tb->data + sel_end, tb->len - sel_end + 1);
    tb->cursor = sel_start;
    tb->len -= del_len;
    tb->has_selection = 0;
}

void tb_clear_selection(TextBuffer *tb) {
    tb->has_selection = 0;
}

void tb_select_all(TextBuffer *tb) {
    tb->selection_anchor = 0;
    tb->cursor = tb->len;
    tb->has_selection = 1;
}

// This function converts a given line and column number to a character index in the text buffer. It iterates through the text buffer to find the start of the specified line, then adds the column offset to determine the final character position. This is useful for hit testing and cursor movement based on line and column coordinates.
int tb_pos_from_line_col(TextBuffer *tb, int line, int col) {
    int current_line = 0;
    int pos = 0;
    while (pos < tb->len && current_line < line) {
        if (tb->data[pos] == '\n') current_line++;
        pos++;
    }
    int line_start = pos;
    int line_end = tb_get_line_end(tb, pos);
    int line_len = line_end - line_start;
    return line_start + (col < line_len ? col : line_len);
}

void tb_insert_char(TextBuffer *tb, char ch) {
    MyDebugOutput(L"TB: Inserting char '%x' at cursor position %d\n", ch, tb->cursor);
    if (tb_has_selection(tb)) tb_delete_selection(tb);
    tb_ensure_cap(tb, 2);
    memmove(tb->data + tb->cursor + 1, tb->data + tb->cursor, tb->len - tb->cursor + 1);
    tb->data[tb->cursor] = ch;
    tb->cursor++;
    tb->len++;

    int line = tb_get_line_number(tb, tb->cursor);
    int col = tb_get_line_col(tb, tb->cursor);
    MyDebugOutput(L"TB: Cursor is now at line %d, col %d\n", line, col);
}

void tb_insert_newline(TextBuffer *tb) {
    tb_insert_char(tb, '\n');
}

void tb_backspace(TextBuffer *tb) {
    if (tb_has_selection(tb)) {
        tb_delete_selection(tb);
        return;
    }
    if (tb->cursor == 0) return;
    memmove(tb->data + tb->cursor - 1, tb->data + tb->cursor, tb->len - tb->cursor + 1);
    tb->cursor--;
    tb->len--;
}

void tb_delete(TextBuffer *tb) {
    if (tb_has_selection(tb)) {
        tb_delete_selection(tb);
        return;
    }
    if (tb->cursor >= tb->len) return;
    memmove(tb->data + tb->cursor, tb->data + tb->cursor + 1, tb->len - tb->cursor);
    tb->len--;
}

void tb_move_cursor_left(TextBuffer *tb) {
    if (tb->cursor > 0) tb->cursor--;
}

void tb_move_cursor_right(TextBuffer *tb) {
    if (tb->cursor < tb->len) tb->cursor++;
}

// The following functions handle vertical cursor movement (up and down) and moving to the beginning or end of the line. They calculate the new cursor position based on the current line and column, ensuring that the cursor stays within valid bounds of the text buffer.
void tb_move_cursor_up(TextBuffer *tb) {
    int line_start = tb_get_line_start(tb, tb->cursor);
    if (line_start == 0) {
        tb->cursor = 0;
        return;
    }
    int col = tb->cursor - line_start;
    int prev_line_end = line_start - 1;
    int prev_line_start = tb_get_line_start(tb, prev_line_end);
    int prev_line_len = prev_line_end - prev_line_start;
    int new_col = (col < prev_line_len) ? col : prev_line_len;
    tb->cursor = prev_line_start + new_col;
    MyDebugOutput(L"TB: Moved cursor up to position %d (line start %d, prev line start %d, prev line end %d, col %d)\n", tb->cursor, line_start, prev_line_start, prev_line_end, new_col);

    int line = tb_get_line_number(tb, tb->cursor);
    new_col = tb_get_line_col(tb, tb->cursor);
    MyDebugOutput(L"TB: Cursor is now at line %d, col %d\n", line, new_col);
}

void tb_move_cursor_down(TextBuffer *tb) {
    int line_start = tb_get_line_start(tb, tb->cursor);
    int line_end = tb_get_line_end(tb, tb->cursor);
    if (line_end >= tb->len) return;
    int col = tb->cursor - line_start;
    int next_line_start = line_end + 1;
    int next_line_end = tb_get_line_end(tb, next_line_start);
    int next_line_len = next_line_end - next_line_start;
    tb->cursor = next_line_start + (col < next_line_len ? col : next_line_len);
}

void tb_move_cursor_home(TextBuffer *tb) {
    tb->cursor = tb_get_line_start(tb, tb->cursor);
}

void tb_move_cursor_end(TextBuffer *tb) {
    tb->cursor = tb_get_line_end(tb, tb->cursor);
}

// The following functions calculate the start and end positions of a line based on a given character index. They are used for various cursor movement and selection operations to determine the boundaries of lines in the text buffer.
int tb_get_line_start(TextBuffer *tb, int pos) {
    if (pos > tb->len) pos = tb->len;
    while (pos > 0 && tb->data[pos - 1] != '\n') pos--;
    return pos;
}

int tb_get_line_end(TextBuffer *tb, int pos) {
    if (pos > tb->len) pos = tb->len;
    while (pos < tb->len && tb->data[pos] != '\n') pos++;
    return pos;
}

int tb_get_line_number(TextBuffer *tb, int pos) {
    int line = 0;
    for (int i = 0; i < pos && i < tb->len; i++) {
        if (tb->data[i] == '\n') line++;
    }
    return line;
}

int tb_get_line_col(TextBuffer *tb, int pos) {
    return pos - tb_get_line_start(tb, pos);
}
