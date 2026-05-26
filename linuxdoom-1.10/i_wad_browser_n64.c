// N64 runtime browser for selecting IWAD before DOOM startup.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <libdragon.h>

#include "i_wad_browser_n64.h"
#include "n64_debug.h"

#define N64_WAD_MAX_ENTRIES 96
#define N64_WAD_PATH_LEN 256
#define N64_WAD_NAME_LEN 64

#define N64_WAD_VISIBLE_ROWS 10
#define N64_WAD_LIST_X 18
#define N64_WAD_LIST_Y 74
#define N64_WAD_LIST_WIDTH 284
#define N64_WAD_ROW_HEIGHT 12

#define N64_WAD_FONT_ID 1

enum
{
    N64_STYLE_TEXT = 0,
    N64_STYLE_TITLE = 1,
    N64_STYLE_SUBTITLE = 2,
    N64_STYLE_HINT = 3,
    N64_STYLE_SELECTED = 4,
    N64_STYLE_ERROR = 5
};

enum
{
    N64_WAD_COMPAT_OK = 0,
    N64_WAD_COMPAT_NOT_IWAD = 1,
    N64_WAD_COMPAT_CORRUPT = 2,
    N64_WAD_COMPAT_OPEN_FAILED = 3
};

typedef struct n64_wad_entry_s
{
    char path[N64_WAD_PATH_LEN];
    char name[N64_WAD_NAME_LEN];
    int64_t size_bytes;
    int compat;
} n64_wad_entry_t;

static n64_wad_entry_t n64_wad_entries[N64_WAD_MAX_ENTRIES];
static int n64_wad_count;
static int n64_wad_compatible_count;
static char n64_selected_wad[N64_WAD_PATH_LEN] = "rom:/doom.wad";
static rdpq_font_t* n64_wad_font;

static color_t col_bg_dark;
static color_t col_bg_header;
static color_t col_panel;
static color_t col_border;
static color_t col_title;
static color_t col_subtitle;
static color_t col_text;
static color_t col_hint;
static color_t col_selected_bg;
static color_t col_selected_fg;
static color_t col_error;

static int I_IsAlphaUpper(char ch)
{
    return ch >= 'A' && ch <= 'Z';
}

static char I_ToLowerAscii(char ch)
{
    if (I_IsAlphaUpper(ch))
        return (char)(ch + ('a' - 'A'));
    return ch;
}

static int I_StrCaseCmp(const char* left, const char* right)
{
    unsigned char lc;
    unsigned char rc;

    if (!left || !right)
        return (left == right) ? 0 : (left ? 1 : -1);

    for (;;)
    {
        lc = (unsigned char)I_ToLowerAscii(*left++);
        rc = (unsigned char)I_ToLowerAscii(*right++);

        if (lc != rc || !lc || !rc)
            return (int)lc - (int)rc;
    }
}

static const char* I_BaseName(const char* path)
{
    const char* base = path;

    if (!path)
        return "";

    while (*path)
    {
        if (*path == '/' || *path == '\\')
            base = path + 1;
        path++;
    }

    return base;
}

static int I_HasWadExtension(const char* name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen(name);
    if (len < 4)
        return 0;

    return I_ToLowerAscii(name[len - 4]) == '.'
        && I_ToLowerAscii(name[len - 3]) == 'w'
        && I_ToLowerAscii(name[len - 2]) == 'a'
        && I_ToLowerAscii(name[len - 1]) == 'd';
}

static void I_CopyTruncated(char* dst, size_t dst_size, const char* src)
{
    if (!dst || !dst_size)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static uint32_t I_ReadLE32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static const char* I_CompatTag(int compat)
{
    switch (compat)
    {
        case N64_WAD_COMPAT_OK:
            return "IWAD";
        case N64_WAD_COMPAT_NOT_IWAD:
            return "PWAD";
        case N64_WAD_COMPAT_CORRUPT:
            return "BAD";
        default:
            return "ERR";
    }
}

static const char* I_CompatMessage(int compat)
{
    switch (compat)
    {
        case N64_WAD_COMPAT_NOT_IWAD:
            return "NOT COMPATIBLE: FILE IS PWAD, NEEDS IWAD";
        case N64_WAD_COMPAT_CORRUPT:
            return "NOT COMPATIBLE: IWAD HEADER OR DIRECTORY INVALID";
        case N64_WAD_COMPAT_OPEN_FAILED:
            return "NOT COMPATIBLE: FAILED TO READ FILE";
        default:
            return "NOT COMPATIBLE";
    }
}

static int I_ClassifyWadCompatibility(const char* path, int64_t size_bytes)
{
    int fd;
    int bytes_read;
    uint8_t header[12];
    uint32_t numlumps;
    uint32_t infotableofs;
    uint64_t dir_end;
    struct stat st;
    off_t file_end;

    if (!path || !path[0])
        return N64_WAD_COMPAT_OPEN_FAILED;

    fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0)
        return N64_WAD_COMPAT_OPEN_FAILED;

    if (size_bytes < 0)
    {
        if (!fstat(fd, &st) && st.st_size >= 0)
        {
            size_bytes = (int64_t)st.st_size;
        }
        else
        {
            file_end = lseek(fd, 0, SEEK_END);
            if (file_end >= 0)
            {
                size_bytes = (int64_t)file_end;
                lseek(fd, 0, SEEK_SET);
            }
        }
    }

    if (size_bytes >= 0 && size_bytes < (int64_t)sizeof(header))
    {
        close(fd);
        return N64_WAD_COMPAT_CORRUPT;
    }

    bytes_read = (int)read(fd, header, sizeof(header));
    close(fd);

    if (bytes_read != (int)sizeof(header))
        return N64_WAD_COMPAT_OPEN_FAILED;

    if (header[0] != 'I' || header[1] != 'W' || header[2] != 'A' || header[3] != 'D')
        return N64_WAD_COMPAT_NOT_IWAD;

    numlumps = I_ReadLE32(header + 4);
    infotableofs = I_ReadLE32(header + 8);

    if (!numlumps)
        return N64_WAD_COMPAT_CORRUPT;

    if (size_bytes >= 0)
    {
        dir_end = (uint64_t)infotableofs + (uint64_t)numlumps * 16ULL;
        if (dir_end > (uint64_t)size_bytes)
            return N64_WAD_COMPAT_CORRUPT;
    }

    return N64_WAD_COMPAT_OK;
}

static int I_WadPriority(const char* name)
{
    if (!I_StrCaseCmp(name, "doom.wad"))
        return 0;
    if (!I_StrCaseCmp(name, "doomu.wad"))
        return 1;
    if (!I_StrCaseCmp(name, "doom2.wad"))
        return 2;
    if (!I_StrCaseCmp(name, "plutonia.wad"))
        return 3;
    if (!I_StrCaseCmp(name, "tnt.wad"))
        return 4;
    if (!I_StrCaseCmp(name, "doom2f.wad"))
        return 5;
    if (!I_StrCaseCmp(name, "doom1.wad"))
        return 6;

    return 20;
}

static int I_CompareWadEntry(const void* lhs_ptr, const void* rhs_ptr)
{
    const n64_wad_entry_t* lhs = (const n64_wad_entry_t*)lhs_ptr;
    const n64_wad_entry_t* rhs = (const n64_wad_entry_t*)rhs_ptr;
    int lhs_priority = I_WadPriority(lhs->name);
    int rhs_priority = I_WadPriority(rhs->name);
    int cmp;

    if (lhs_priority != rhs_priority)
        return lhs_priority - rhs_priority;

    cmp = I_StrCaseCmp(lhs->name, rhs->name);
    if (cmp)
        return cmp;

    return I_StrCaseCmp(lhs->path, rhs->path);
}

static int I_WadScanCallback(const char* fn, dir_t* dir, void* data)
{
    const char* name;
    n64_wad_entry_t* entry;

    (void)data;

    if (!fn || !dir || dir->d_type != DT_REG)
        return DIR_WALK_CONTINUE;

    name = I_BaseName(fn);
    if (!I_HasWadExtension(name))
        return DIR_WALK_CONTINUE;

    if (n64_wad_count >= N64_WAD_MAX_ENTRIES)
        return DIR_WALK_CONTINUE;

    entry = &n64_wad_entries[n64_wad_count];
    I_CopyTruncated(entry->path, sizeof(entry->path), fn);
    I_CopyTruncated(entry->name, sizeof(entry->name), name);
    entry->size_bytes = dir->d_size;
    entry->compat = I_ClassifyWadCompatibility(fn, dir->d_size);

    if (entry->compat == N64_WAD_COMPAT_OK)
        n64_wad_compatible_count++;

    n64_wad_count++;

    return DIR_WALK_CONTINUE;
}

static void I_ScanWadEntries(void)
{
    int i;

    n64_wad_count = 0;
    n64_wad_compatible_count = 0;
    memset(n64_wad_entries, 0, sizeof(n64_wad_entries));

    dir_walk("rom:/", I_WadScanCallback, NULL);

    if (n64_wad_count > 1)
    {
        qsort(n64_wad_entries,
              n64_wad_count,
              sizeof(n64_wad_entries[0]),
              I_CompareWadEntry);
    }

    N64_DEBUGF("WAD browser: found %d .wad entries (%d compatible)\n",
               n64_wad_count,
               n64_wad_compatible_count);
    for (i = 0; i < n64_wad_count; i++)
    {
        N64_DEBUGF("  [%02d] %s (%lld bytes, %s)\n",
                   i,
                   n64_wad_entries[i].path,
                   (long long)n64_wad_entries[i].size_bytes,
                   I_CompatTag(n64_wad_entries[i].compat));
    }
}

static int I_FindEntryByPath(const char* path)
{
    int i;

    if (!path)
        return -1;

    for (i = 0; i < n64_wad_count; i++)
    {
        if (!I_StrCaseCmp(path, n64_wad_entries[i].path))
            return i;
    }

    return -1;
}

static int I_FindDefaultEntry(void)
{
    int i;

    for (i = 0; i < n64_wad_count; i++)
    {
        if (!I_StrCaseCmp(n64_wad_entries[i].name, "doom.wad")
            && n64_wad_entries[i].compat == N64_WAD_COMPAT_OK)
            return i;
    }

    return -1;
}

static int I_FindFirstCompatibleEntry(void)
{
    int i;

    for (i = 0; i < n64_wad_count; i++)
    {
        if (n64_wad_entries[i].compat == N64_WAD_COMPAT_OK)
            return i;
    }

    return -1;
}

static void I_ClipLabel(char* dst, size_t dst_size, const char* src, size_t max_len)
{
    size_t src_len;

    if (!dst || !dst_size)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    src_len = strlen(src);
    if (src_len <= max_len)
    {
        I_CopyTruncated(dst, dst_size, src);
        return;
    }

    if (max_len < 4)
    {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, max_len - 3);
    strcpy(dst + (max_len - 3), "...");
}

static void I_FillRect(int x, int y, int width, int height, color_t color)
{
    if (width <= 0 || height <= 0)
        return;

    rdpq_set_mode_fill(color);
    rdpq_fill_rectangle(x, y, x + width, y + height);
}

static void I_DrawTextSafe(int x,
                           int y,
                           int width,
                           int style,
                           const char* text)
{
    rdpq_textparms_t parms;
    rdpq_paragraph_t* layout;
    int nbytes;
    float draw_x;
    float draw_y;

    if (!text || width <= 0)
        return;

    memset(&parms, 0, sizeof(parms));
    parms.style_id = style;
    parms.width = width;
    parms.align = ALIGN_LEFT;
    parms.valign = VALIGN_TOP;
    parms.wrap = WRAP_ELLIPSES;

    nbytes = (int)strlen(text);
    layout = rdpq_paragraph_build(&parms, N64_WAD_FONT_ID, text, &nbytes);
    if (!layout)
        return;

    draw_x = (float)x - layout->bbox.x0;
    draw_y = (float)y - layout->bbox.y0;

    rdpq_paragraph_render(layout, draw_x, draw_y);
    rdpq_paragraph_free(layout);
}

static void I_InitTextRenderer(void)
{
    n64_wad_font = rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO);
    assertf(n64_wad_font != NULL, "WAD browser: failed loading builtin font");

    rdpq_font_style(n64_wad_font,
                    N64_STYLE_TEXT,
                    &(rdpq_fontstyle_t){
                        .color = col_text,
                        .outline_color = RGBA32(0, 0, 0, 0)
                    });

    rdpq_font_style(n64_wad_font,
                    N64_STYLE_TITLE,
                    &(rdpq_fontstyle_t){
                        .color = col_title,
                        .outline_color = RGBA32(0, 0, 0, 0)
                    });

    rdpq_font_style(n64_wad_font,
                    N64_STYLE_SUBTITLE,
                    &(rdpq_fontstyle_t){
                        .color = col_subtitle,
                        .outline_color = RGBA32(0, 0, 0, 0)
                    });

    rdpq_font_style(n64_wad_font,
                    N64_STYLE_HINT,
                    &(rdpq_fontstyle_t){
                        .color = col_hint,
                        .outline_color = RGBA32(0, 0, 0, 0)
                    });

    rdpq_font_style(n64_wad_font,
                    N64_STYLE_SELECTED,
                    &(rdpq_fontstyle_t){
                        .color = col_selected_fg,
                        .outline_color = RGBA32(0, 0, 0, 0)
                    });

    rdpq_font_style(n64_wad_font,
                    N64_STYLE_ERROR,
                    &(rdpq_fontstyle_t){
                        .color = col_error,
                        .outline_color = RGBA32(0, 0, 0, 0)
                    });

    rdpq_text_register_font(N64_WAD_FONT_ID, n64_wad_font);
}

static void I_ShutdownTextRenderer(void)
{
    if (!n64_wad_font)
        return;

    rdpq_text_unregister_font(N64_WAD_FONT_ID);
    rdpq_font_free(n64_wad_font);
    n64_wad_font = NULL;
}

static void I_InitPalette(void)
{
    col_bg_dark = RGBA32(18, 10, 8, 255);
    col_bg_header = RGBA32(52, 18, 13, 255);
    col_panel = RGBA32(30, 17, 12, 255);
    col_border = RGBA32(114, 61, 24, 255);
    col_title = RGBA32(228, 179, 74, 255);
    col_subtitle = RGBA32(201, 116, 50, 255);
    col_text = RGBA32(240, 224, 180, 255);
    col_hint = RGBA32(177, 148, 101, 255);
    col_selected_bg = RGBA32(128, 45, 15, 255);
    col_selected_fg = RGBA32(255, 231, 168, 255);
    col_error = RGBA32(232, 106, 90, 255);
}

static void I_DrawStaticFrame(void)
{
    I_FillRect(0, 0, 320, 240, col_bg_dark);
    I_FillRect(0, 0, 320, 54, col_bg_header);
    I_FillRect(0, 54, 320, 2, col_border);

    I_FillRect(10, 62, 300, 142, col_panel);
    I_FillRect(10, 62, 300, 1, col_border);
    I_FillRect(10, 203, 300, 1, col_border);
    I_FillRect(10, 62, 1, 142, col_border);
    I_FillRect(309, 62, 1, 142, col_border);

    I_DrawTextSafe(18, 9, 152, N64_STYLE_TITLE, "DOOM N64");
    I_DrawTextSafe(18, 23, 152, N64_STYLE_SUBTITLE, "SELECT IWAD");

    I_DrawTextSafe(176, 9, 142, N64_STYLE_HINT, "D-PAD: MOVE");
    I_DrawTextSafe(176, 18, 142, N64_STYLE_HINT, "C-UP/DOWN: PAGE");
    I_DrawTextSafe(176, 27, 142, N64_STYLE_HINT, "A/START: LOAD");
    I_DrawTextSafe(176, 36, 142, N64_STYLE_HINT, "B: DEFAULT");
}

static void I_DrawEntryRow(int row, int entry_index, int selected)
{
    char label[32];
    char size_label[16];
    const char* compat_tag;
    int x;
    int y;
    int style;
    n64_wad_entry_t* entry;
    unsigned long size_kib;

    x = N64_WAD_LIST_X;
    y = N64_WAD_LIST_Y + row * N64_WAD_ROW_HEIGHT;
    entry = &n64_wad_entries[entry_index];

    if (selected)
        I_FillRect(x - 2, y - 1, N64_WAD_LIST_WIDTH, N64_WAD_ROW_HEIGHT - 1, col_selected_bg);

    I_ClipLabel(label, sizeof(label), entry->name, 22);

    style = selected ? N64_STYLE_SELECTED : N64_STYLE_TEXT;
    I_DrawTextSafe(x, y, 222, style, label);

    if (entry->compat == N64_WAD_COMPAT_OK && entry->size_bytes >= 0)
    {
        size_kib = (unsigned long)((entry->size_bytes + 1023) / 1024);
        snprintf(size_label, sizeof(size_label), "%5luK", size_kib);
        I_DrawTextSafe(248, y, 62, style, size_label);
    }
    else
    {
        compat_tag = I_CompatTag(entry->compat);
        I_DrawTextSafe(248, y, 62, N64_STYLE_ERROR, compat_tag);
    }
}

static void I_DrawBrowserFrame(int cursor, int page, int reject_notice_ticks)
{
    char footer[64];
    int i;
    int index;
    n64_wad_entry_t* selected_entry;

    I_DrawStaticFrame();

    selected_entry = &n64_wad_entries[cursor];

    if (reject_notice_ticks > 0)
    {
        I_DrawTextSafe(16,
                       55,
                       304,
                       N64_STYLE_ERROR,
                       "SELECTED FILE IS NOT A COMPATIBLE IWAD");
    }

    for (i = 0; i < N64_WAD_VISIBLE_ROWS; i++)
    {
        index = page + i;
        if (index >= n64_wad_count)
            break;
        I_DrawEntryRow(i, index, index == cursor);
    }

    if (page > 0)
    {
        I_DrawTextSafe(132, 64, 120, N64_STYLE_HINT, "^ MORE ^");
    }

    if ((page + N64_WAD_VISIBLE_ROWS) < n64_wad_count)
    {
        I_DrawTextSafe(132, 193, 120, N64_STYLE_HINT, "v MORE v");
    }

    snprintf(footer,
             sizeof(footer),
             "WADS: %d  COMPATIBLE: %d",
             n64_wad_count,
             n64_wad_compatible_count);
    I_DrawTextSafe(16, 210, 304, N64_STYLE_HINT, footer);

    if (selected_entry->compat == N64_WAD_COMPAT_OK)
    {
        I_DrawTextSafe(16, 221, 304, N64_STYLE_TEXT, selected_entry->path);
    }
    else
    {
        I_DrawTextSafe(16,
                       221,
                       304,
                       N64_STYLE_ERROR,
                       I_CompatMessage(selected_entry->compat));
    }
}

static void I_DrawFallbackFrame(void)
{
    I_DrawStaticFrame();

    I_DrawTextSafe(18, 86, 280, N64_STYLE_TEXT, "NO .WAD FILES FOUND IN ROM FS");
    I_DrawTextSafe(18, 104, 280, N64_STYLE_HINT, "ADD IWADS UNDER filesystem/");
    I_DrawTextSafe(18, 114, 280, N64_STYLE_HINT, "AND REBUILD ROM IMAGE");

    I_DrawTextSafe(18, 136, 280, N64_STYLE_TEXT, "PRESS A OR START TO CONTINUE");
    I_DrawTextSafe(18, 148, 280, N64_STYLE_HINT, "FALLBACK: rom:/doom.wad");

    I_DrawTextSafe(16, 221, 304, N64_STYLE_HINT, "B ALSO CONTINUES WITH FALLBACK");
}

static void I_ClampCursorAndPage(int* cursor, int* page)
{
    if (!cursor || !page)
        return;

    if (*cursor < 0)
        *cursor = 0;
    if (*cursor >= n64_wad_count)
        *cursor = n64_wad_count - 1;

    if (*page > *cursor)
        *page = *cursor;

    if (*cursor >= (*page + N64_WAD_VISIBLE_ROWS))
        *page = *cursor - N64_WAD_VISIBLE_ROWS + 1;

    if (*page < 0)
        *page = 0;
}

static int I_RunSelectionLoop(void)
{
    joypad_buttons_t pressed;
    joypad_buttons_t held;
    joypad_inputs_t inputs;
    surface_t* disp;
    int cursor;
    int page;
    int move;
    int default_entry;
    bool done;
    bool used_default;
    bool stick_up_latch;
    bool stick_down_latch;
    bool input_armed;
    int reject_notice_ticks;

    if (!n64_wad_count)
        return -1;

    cursor = I_FindEntryByPath(n64_selected_wad);
    if (cursor < 0)
        cursor = 0;
    page = 0;
    stick_up_latch = false;
    stick_down_latch = false;
    done = false;
    used_default = false;
    input_armed = false;
    reject_notice_ticks = 0;

    while (!done)
    {
        joypad_poll();
        pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        held = joypad_get_buttons_held(JOYPAD_PORT_1);
        inputs = joypad_get_inputs(JOYPAD_PORT_1);
        move = 0;

        if (!held.raw)
            input_armed = true;

        if (pressed.d_up)
            move = -1;
        else if (pressed.d_down)
            move = 1;
        else if (pressed.d_left)
            move = -N64_WAD_VISIBLE_ROWS;
        else if (pressed.d_right)
            move = N64_WAD_VISIBLE_ROWS;
        else if (pressed.c_up)
            move = -N64_WAD_VISIBLE_ROWS;
        else if (pressed.c_down)
            move = N64_WAD_VISIBLE_ROWS;

        if (inputs.stick_y > 48)
        {
            if (!stick_up_latch)
                move = -1;
            stick_up_latch = true;
        }
        else
        {
            stick_up_latch = false;
        }

        if (inputs.stick_y < -48)
        {
            if (!stick_down_latch)
                move = 1;
            stick_down_latch = true;
        }
        else
        {
            stick_down_latch = false;
        }

        if (move)
        {
            cursor += move;
            I_ClampCursorAndPage(&cursor, &page);
        }

        if (input_armed && (pressed.a || pressed.start))
        {
            if (n64_wad_entries[cursor].compat == N64_WAD_COMPAT_OK)
            {
                done = true;
            }
            else
            {
                reject_notice_ticks = 60;
                N64_DEBUGF("WAD browser: rejected incompatible selection index=%d path=%s (%s)\n",
                           cursor,
                           n64_wad_entries[cursor].path,
                           I_CompatTag(n64_wad_entries[cursor].compat));
            }
        }

        if (input_armed && pressed.b)
        {
            default_entry = I_FindDefaultEntry();
            if (default_entry < 0)
                default_entry = I_FindFirstCompatibleEntry();

            if (default_entry >= 0)
            {
                cursor = default_entry;
                used_default = true;
                done = true;
            }
            else
            {
                reject_notice_ticks = 60;
                N64_DEBUGF("WAD browser: no compatible IWAD available for default selection\n");
            }
        }

        disp = display_get();
        rdpq_attach_clear(disp, NULL);
        I_DrawBrowserFrame(cursor, page, reject_notice_ticks);
        rdpq_detach_show();

        if (reject_notice_ticks > 0)
            reject_notice_ticks--;
    }

    if (!used_default)
    {
        N64_DEBUGF("WAD browser: selected index=%d path=%s\n",
                   cursor,
                   n64_wad_entries[cursor].path);
        return cursor;
    }

    N64_DEBUGF("WAD browser: using default index=%d path=%s\n",
               cursor,
               n64_wad_entries[cursor].path);
    return cursor;
}

static void I_RunFallbackLoop(void)
{
    joypad_buttons_t pressed;
    joypad_buttons_t held;
    surface_t* disp;
    bool input_armed;

    input_armed = false;

    for (;;)
    {
        joypad_poll();
        pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        held = joypad_get_buttons_held(JOYPAD_PORT_1);

        if (!held.raw)
            input_armed = true;

        disp = display_get();
        rdpq_attach_clear(disp, NULL);
        I_DrawFallbackFrame();
        rdpq_detach_show();

        if (input_armed && (pressed.a || pressed.start || pressed.b))
        {
            N64_DEBUGF("WAD browser: fallback continue with path=%s\n", n64_selected_wad);
            return;
        }
    }
}

void I_N64RunWadBrowser(void)
{
    int selected_index;

    I_ScanWadEntries();
    N64_DEBUGF("WAD browser: entering UI with %d entries\n", n64_wad_count);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    display_set_fps_limit(60.0f);
    rdpq_init();
    I_InitPalette();
    I_InitTextRenderer();

    if (!n64_wad_count)
    {
        I_RunFallbackLoop();
        N64_DEBUGF("WAD browser: no entries, selected path=%s\n", n64_selected_wad);
        rspq_wait();
        I_ShutdownTextRenderer();
        rdpq_close();
        return;
    }

    selected_index = I_RunSelectionLoop();
    if (selected_index >= 0 && selected_index < n64_wad_count)
    {
        I_CopyTruncated(n64_selected_wad,
                        sizeof(n64_selected_wad),
                        n64_wad_entries[selected_index].path);
    }

    N64_DEBUGF("WAD browser: final selected path=%s\n", n64_selected_wad);

    rspq_wait();
    I_ShutdownTextRenderer();
    rdpq_close();
}

const char* I_N64GetSelectedWadPath(void)
{
    return n64_selected_wad;
}
