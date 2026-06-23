#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include "text_buffer.h"
#include "renderer.h"
#include "WinBase.h"

#define WINDOW_CLASS_NAME L"TextEditorWndClass"
#define WINDOW_TITLE L"TextEdit"

static TextBuffer g_buffer;
static Renderer g_renderer;
static int g_cursor_blink = 1;
static UINT_PTR g_timer_id = 1;
static int g_font_size = 16;
static char g_font_path[MAX_PATH] = "C:\\Windows\\Fonts\\consola.ttf";
static int g_mouse_selecting = 0;

static void app_init(void) {
    tb_init(&g_buffer);
    renderer_init(&g_renderer, g_font_path, g_font_size);
}

static void app_cleanup(void) {
    renderer_free(&g_renderer);
    tb_free(&g_buffer);
}

static void app_open_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    tb_free(&g_buffer);
    tb_init(&g_buffer);
    if (size > 0) {
        g_buffer.len = (int)size;
        while (g_buffer.len + 1 >= g_buffer.cap) {
            g_buffer.cap *= 2;
        }
        g_buffer.data = (char *)realloc(g_buffer.data, g_buffer.cap);
        fread(g_buffer.data, 1, size, f);
        g_buffer.data[size] = '\0';
    }
    g_buffer.cursor = 0;

    fclose(f);
}

static void app_save_file(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(g_buffer.data, 1, g_buffer.len, f);
    fclose(f);
}

static void app_update_scroll(TextBuffer *tb, int client_width, int client_height) {
    int cur_line = tb_get_line_number(tb, tb->cursor);
    int cursor_y = (int)cur_line * g_renderer.line_height;
    int cursor_x = (int)tb_get_line_col(tb, tb->cursor) * (g_renderer.ft_face->size->metrics.max_advance >> 6) + g_renderer.text_area_left;

    if (cursor_y - tb->scroll_y < 0) {
        tb->scroll_y = cursor_y;
    } else if (cursor_y + g_renderer.line_height - tb->scroll_y > client_height) {
        tb->scroll_y = cursor_y + g_renderer.line_height - client_height;
    }

    if (cursor_x - tb->scroll_x < g_renderer.text_area_left) {
        tb->scroll_x = cursor_x - g_renderer.text_area_left;
    } else if (cursor_x + 40 - tb->scroll_x > client_width) {
        tb->scroll_x = cursor_x + 40 - client_width;
    }
}

static void app_copy_selection(HWND hwnd) {
    if (!tb_has_selection(&g_buffer)) return;
    int sel_start, sel_end;
    tb_get_selection_range(&g_buffer, &sel_start, &sel_end);
    int len = sel_end - sel_start;
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    if (!hmem) return;
    char *ptr = (char *)GlobalLock(hmem);
    memcpy(ptr, g_buffer.data + sel_start, len);
    ptr[len] = '\0';
    GlobalUnlock(hmem);
    OpenClipboard(hwnd);
    EmptyClipboard();
    SetClipboardData(CF_TEXT, hmem);
    CloseClipboard();
}

static void app_paste_selection(HWND hwnd) {
    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    OpenClipboard(hwnd);
    HGLOBAL hmem = GetClipboardData(CF_TEXT);
    if (!hmem) {
        CloseClipboard();
        return;
    }
    char *ptr = (char *)GlobalLock(hmem);
    if (ptr) {
        while (*ptr) {
            tb_insert_char(&g_buffer, *ptr);
            ptr++;
        }
    }
    GlobalUnlock(hmem);
    CloseClipboard();
}

static void app_delete_selection(void) {
    if (!tb_has_selection(&g_buffer)) return;
    int sel_start, sel_end;
    tb_get_selection_range(&g_buffer, &sel_start, &sel_end);
    memmove(g_buffer.data + sel_start, g_buffer.data + sel_end, g_buffer.len - sel_end + 1);
    g_buffer.len -= (sel_end - sel_start);
    g_buffer.cursor = sel_start;
    tb_clear_selection(&g_buffer);
}

static void blink_cursor()
{
    g_renderer.cursor_color = g_cursor_blink ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x80, 0x80, 0x80);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        app_init();
        SetTimer(hwnd, g_timer_id, 500, NULL);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, g_timer_id);
        app_cleanup();
        PostQuitMessage(0);
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lparam);
        int h = HIWORD(lparam);
        renderer_resize(&g_renderer, w, h);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wparam == g_timer_id) {
            g_cursor_blink = !g_cursor_blink;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_SETFOCUS:
        KillTimer(hwnd, g_timer_id);
        SetTimer(hwnd, g_timer_id, 500, NULL);
        g_cursor_blink = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KILLFOCUS:
        KillTimer(hwnd, g_timer_id);
        g_cursor_blink = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    // This case handles the WM_PAINT message, which is sent when the window needs to be redrawn. It begins the paint operation, calls the renderer to draw the text buffer onto the window's device context, and then ends the paint operation. The renderer will use the current state of the text buffer, including cursor position and selection, to render the editor's content.
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        blink_cursor();
        renderer_render(&g_renderer, &g_buffer, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN: {
        int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        int shift = GetKeyState(VK_SHIFT) & 0x8000;

        if (!ctrl && !shift) tb_clear_selection(&g_buffer);

        switch (wparam) {
        case VK_LEFT:
            if (shift) {
                if (!g_buffer.has_selection) {
                    g_buffer.selection_anchor = g_buffer.cursor;
                    g_buffer.has_selection = 1;
                }
                tb_move_cursor_left(&g_buffer);
            } else if (ctrl) {
                tb_move_cursor_home(&g_buffer);
            } else {
                tb_move_cursor_left(&g_buffer);
            }
            goto handled;
        case VK_RIGHT:
            if (shift) {
                if (!g_buffer.has_selection) {
                    g_buffer.selection_anchor = g_buffer.cursor;
                    g_buffer.has_selection = 1;
                }
                tb_move_cursor_right(&g_buffer);
            } else if (ctrl) {
                tb_move_cursor_end(&g_buffer);
            } else {
                tb_move_cursor_right(&g_buffer);
            }
            goto handled;
        case VK_UP:
            if (shift) {
                if (!g_buffer.has_selection) {
                    g_buffer.selection_anchor = g_buffer.cursor;
                    g_buffer.has_selection = 1;
                }
            }
            tb_move_cursor_up(&g_buffer);
            goto handled;
        case VK_DOWN:
            if (shift) {
                if (!g_buffer.has_selection) {
                    g_buffer.selection_anchor = g_buffer.cursor;
                    g_buffer.has_selection = 1;
                }
            }
            tb_move_cursor_down(&g_buffer);
            goto handled;
        case VK_HOME:
            if (shift) {
                if (!g_buffer.has_selection) {
                    g_buffer.selection_anchor = g_buffer.cursor;
                    g_buffer.has_selection = 1;
                }
            }
            tb_move_cursor_home(&g_buffer);
            goto handled;
        case VK_END:
            if (shift) {
                if (!g_buffer.has_selection) {
                    g_buffer.selection_anchor = g_buffer.cursor;
                    g_buffer.has_selection = 1;
                }
            }
            tb_move_cursor_end(&g_buffer);
            goto handled;
        case VK_BACK:
            tb_backspace(&g_buffer);
            goto handled;
        case VK_DELETE:
            if (ctrl) 
                app_delete_selection();
            else 
                tb_delete(&g_buffer);

            goto handled;
        case VK_RETURN:
            tb_insert_newline(&g_buffer);
            goto handled;
        case 'A':
            if (ctrl) {
                tb_select_all(&g_buffer);
            }
            goto handled;
        case 'C':
            if (ctrl) {
                app_copy_selection(hwnd);
            }
            goto handled;
        case 'V':
            if (ctrl) {
                app_paste_selection(hwnd);
            }
            goto handled;
        case 'X':
            if (ctrl) {
                app_copy_selection(hwnd);
                app_delete_selection();
            }
            goto handled;
        case 'S':
            if (ctrl) {
                app_save_file("output.txt");
            }
            goto handled;
        case 'O':
            if (ctrl) {
                app_open_file("output.txt");
            }
            goto handled;
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);

    handled:
        g_cursor_blink = 1;
        KillTimer(hwnd, g_timer_id);
        SetTimer(hwnd, g_timer_id, 500, NULL);
        RECT rc;
        GetClientRect(hwnd, &rc);
        app_update_scroll(&g_buffer, rc.right, rc.bottom);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_CHAR: {
        if (wparam == '\r' || wparam == '\b' || wparam == '\t') {
            if (wparam == '\t') {
                for (int i = 0; i < 4; i++) tb_insert_char(&g_buffer, ' ');
            }
            goto char_handled;
        }
        if (wparam >= 32 && wparam < 127) {
            tb_insert_char(&g_buffer, (char)wparam);
        }
    char_handled:
        g_cursor_blink = 1;
        KillTimer(hwnd, g_timer_id);
        SetTimer(hwnd, g_timer_id, 500, NULL);
        RECT rc;
        GetClientRect(hwnd, &rc);
        app_update_scroll(&g_buffer, rc.right, rc.bottom);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lparam);
        int my = GET_Y_LPARAM(lparam);
        SetFocus(hwnd);
        SetCapture(hwnd);
        g_mouse_selecting = 1;
        g_buffer.cursor = renderer_hit_test(&g_renderer, &g_buffer, mx, my);
        g_buffer.selection_anchor = g_buffer.cursor;
        g_buffer.has_selection = 0;
        g_cursor_blink = 1;
        KillTimer(hwnd, g_timer_id);
        SetTimer(hwnd, g_timer_id, 500, NULL);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    // This case handles mouse movement while the left button is held down for text selection. It performs hit testing to determine the character position under the mouse cursor and updates the text buffer's cursor and selection state accordingly. The editor is then invalidated to trigger a redraw with the updated selection.
    case WM_MOUSEMOVE: {
        if (!g_mouse_selecting) return 0;
        int mx = GET_X_LPARAM(lparam);
        int my = GET_Y_LPARAM(lparam);
        int pos = renderer_hit_test(&g_renderer, &g_buffer, mx, my);
        g_buffer.cursor = pos;
        g_buffer.has_selection = (pos != g_buffer.selection_anchor) ? 1 : 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_mouse_selecting) {
            g_mouse_selecting = 0;
            ReleaseCapture();
            if (g_buffer.has_selection && g_buffer.selection_anchor == g_buffer.cursor) {
                g_buffer.has_selection = 0;
            }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        g_buffer.scroll_y -= delta;
        if (g_buffer.scroll_y < 0) g_buffer.scroll_y = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

int WINAPI WinMain(HINSTANCE h_inst, HINSTANCE h_prev_inst, LPSTR cmd_line, int show_cmd)
{
    (void)h_prev_inst;
    (void)cmd_line;

    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h_inst;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class", L"Error", MB_OK);
        return 1;
    }

    HWND hwnd = CreateWindowW(
        WINDOW_CLASS_NAME,
        WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        900, 650,
        NULL, NULL, h_inst, NULL
    );

    if (!hwnd) {
        MessageBoxW(NULL, L"Failed to create window", L"Error", MB_OK);
        return 1;
    }

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
