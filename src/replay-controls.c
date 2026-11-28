#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include "replay-native.h"
#include "replay-controls.h"

/* ---- Engine addresses (Red Alert 3.03 + ra-patches) ---- */
#define ADDR_SCREEN_WIDTH       0x006016B0u
#define ADDR_SCREEN_HEIGHT      0x006016B4u
#define ADDR_FRAME              0x006680C4u
#define ADDR_MOUSE_MAP          0x00668250u
#define ADDR_MAP_TACTICAL_COORD (ADDR_MOUSE_MAP + 0x0C4Au)
#define ADDR_MAP_LEFT_STRIP_X   (ADDR_MOUSE_MAP + 0x104Fu)
#define ADDR_MAP_SIDEBAR_FLAG   (ADDR_MOUSE_MAP + 0x15F6u)
#define ADDR_GAME_ACTIVE        0x00669924u
#define ADDR_OPTIONS_DIALOG     0x00680834u
#define ADDR_OPTIONS_VOLUME     0x00668190u
#define ADDR_FRAME_TIMER_DELAY  0x006807FDu
#define ADDR_SCEN_RANDOM        0x006678E8u
#define ADDR_CRYPT_RANDOM       0x00680654u
#define ADDR_OUTLIST_COUNT      0x0066AB5Cu
#define ADDR_OUTLIST_HEAD       0x0066AB60u
#define ADDR_OUTLIST_TAIL       0x0066AB64u
#define ADDR_DOLIST_COUNT       0x0066B068u
#define ADDR_DOLIST_HEAD        0x0066B06Cu
#define ADDR_DOLIST_TAIL        0x0066B070u
#define ADDR_DOLIST_ARRAY       0x0066B074u
#define ADDR_MP_HOUSE_FRAMES    0x00687804u
#define ADDR_CURRENT_PALETTE    0x0060BA60u
#define ADDR_FALLBACK_PALETTE   0x0060CE90u
#define ADDR_HIDPAGE            0x006807CCu

#define DOLIST_CAP              4096u
#define EVENT_SIZE              20u
#define MAX_SNAPSHOTS           64
#define SNAP_BASE_SLOT          900
#define MIN_SNAP_INTERVAL       450u

#define BAR_MAX_W               760
#define BAR_H                   40

enum {
    PB_ACT_JUMP_START = 0,
    PB_ACT_REWIND_15S = 1,
    PB_ACT_SLOWER     = 2,
    PB_ACT_PLAY_PAUSE = 3,
    PB_ACT_FASTER     = 4,
    PB_ACT_STEP_FWD   = 5,
    PB_ACT_JUMP_END   = 6,
    PB_ACT_TOGGLE_VIS = 7
};

extern int NativeReplay_ModeValue;

/* ASM Watcom/cdecl shims in replay-controls-hooks.asm */
void pb_watcom_call_back(void);
void pb_watcom_keyboard_check(void);
void pb_watcom_render_sync_delay(void);
void pb_watcom_flag_redraw(int forced);
void pb_watcom_redraw_cell_at(int px, int py);
void pb_watcom_set_tactical_pos(uint32_t coord);
int  pb_watcom_save_game(int slot, const char *desc);
int  pb_watcom_load_game(int slot);
int  pb_watcom_hidpage_lock(void);
void pb_watcom_hidpage_unlock(void);

static uint32_t muldiv_u32(uint32_t a, uint32_t b, uint32_t c, uint32_t add) {
    uint64_t num = (uint64_t)a * (uint64_t)b + (uint64_t)add;
    uint32_t hi = (uint32_t)(num >> 32);
    uint32_t lo = (uint32_t)num;
    uint32_t q = 0;
    int i;
    if (c == 0) return 0;
    if (hi == 0) return lo / c;
    for (i = 31; i >= 0; i--) {
        uint32_t carry = hi >> 31;
        hi = (hi << 1) | (lo >> 31);
        lo <<= 1;
        q <<= 1;
        if (carry || hi >= c) {
            hi -= c;
            q |= 1u;
        }
    }
    return q;
}

typedef struct {
    uint8_t *offset;
    int width;
    int height;
    int x_add;
    int y_add;
    int unk14;
    int pitch;
} GraphicViewPort;

typedef struct {
    HMODULE (WINAPI *LoadLibraryA)(LPCSTR);
    FARPROC (WINAPI *GetProcAddress)(HMODULE, LPCSTR);
    HANDLE  (WINAPI *GetProcessHeap)(void);
    LPVOID  (WINAPI *HeapAlloc)(HANDLE, DWORD, SIZE_T);
    BOOL    (WINAPI *HeapFree)(HANDLE, DWORD, LPVOID);
    BOOL    (WINAPI *DeleteFileA)(LPCSTR);
    DWORD   (WINAPI *GetTickCount)(void);
    void    (WINAPI *Sleep)(DWORD);
} WinApi;

typedef struct {
    uint32_t frame;
    uint32_t event_pos;
    uint32_t check_pos;
    uint32_t last_frame;
    uint32_t crypt_random;
    uint32_t scen_random;
    uint32_t mp_house_frames[7];
    uint32_t do_count;
    uint32_t do_head;
    uint32_t do_tail;
    uint8_t *do_list;
} ReplaySnapshot;

typedef struct {
    int bar_x, bar_y, bar_w, bar_h;
    int seek_x, seek_y, seek_w, seek_h;
    int valid;
} ControlsLayout;

/* Shared state visible to ASM hooks */
int pb_paused = 0;
int pb_seeking = 0;
int pb_restoring_snap = 0;
int pb_saving_snap = 0;

static WinApi wapi;
static HANDLE proc_heap;
static int api_ready = 0;

static int pb_visible = 1;
static int pb_at_end = 0;
static int pb_speed_idx = 2; /* 1.00x */
static int pb_step_frames = 0;
static int pb_seek_target = -1;
static int pb_pending_restore_slot = -1;
static int pb_was_paused_before_seek = 0;
static uint32_t pb_tick_rem_q8 = 0;
static uint32_t pb_last_seek_paint_ms = 0;

static int pb_audio_muted = 0;
static uint16_t pb_saved_volume = 0;

static int pb_last_drawn_frame = -1;
static int pb_last_drawn_paused = -1;
static int pb_last_drawn_speed = -1;
static int pb_last_drawn_visible = -1;

static uint8_t saved_bg[BAR_MAX_W * BAR_H];
static int saved_bg_valid = 0;
static int saved_bg_x = 0, saved_bg_y = 0, saved_bg_w = 0, saved_bg_h = 0;
static int saved_bg_sw = 0, saved_bg_sh = 0;

/* Track last rendered bar rect on SeenPage + TacticalCoord for scroll smear cleanup */
static int seen_bar_valid = 0;
static int seen_bar_x = 0, seen_bar_y = 0, seen_bar_w = 0, seen_bar_h = 0;
static int scroll_tac_valid = 0;
static uint32_t scroll_pre_tac = 0;

static ReplaySnapshot snapshots[MAX_SNAPSHOTS];
static int snapshot_count = 0;
static uint32_t snap_interval = MIN_SNAP_INTERVAL;

/* Palette cache & LUTs */
static uint8_t pal_cache[768];
static uint8_t pal_lut_panel[256];
static int pal_initialized = 0;

static uint8_t col_panel_border;
static uint8_t col_status_play;
static uint8_t col_status_pause;
static uint8_t col_text_time;
static uint8_t col_text_speed;
static uint8_t col_text_hint;
static uint8_t col_text_shadow;
static uint8_t col_seek_border;
static uint8_t col_seek_bg;
static uint8_t col_seek_fill;
static uint8_t col_thumb_fill;
static uint8_t col_thumb_border;

static const int speed_q8[6] = { 64, 128, 256, 512, 1024, 2048 };
static const char * const speed_labels[6] = {
    "0.25x", "0.50x", "1.00x", "2.00x", "4.00x", "8.00x"
};

/* Crisp 5x8 pixel font for time, speed, status & hotkey hints */
typedef struct { char ch; uint8_t rows[8]; } Glyph5x8;
static const Glyph5x8 font_5x8[] = {
    { '0', { 0x0E, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x0E, 0x00 } },
    { '1', { 0x06, 0x0E, 0x06, 0x06, 0x06, 0x06, 0x0F, 0x00 } },
    { '2', { 0x0E, 0x1B, 0x03, 0x06, 0x0C, 0x18, 0x1F, 0x00 } },
    { '3', { 0x0E, 0x1B, 0x03, 0x0E, 0x03, 0x1B, 0x0E, 0x00 } },
    { '4', { 0x03, 0x07, 0x0B, 0x13, 0x1F, 0x03, 0x03, 0x00 } },
    { '5', { 0x1F, 0x18, 0x1E, 0x03, 0x03, 0x1B, 0x0E, 0x00 } },
    { '6', { 0x0E, 0x18, 0x1E, 0x1B, 0x1B, 0x1B, 0x0E, 0x00 } },
    { '7', { 0x1F, 0x03, 0x06, 0x06, 0x0C, 0x0C, 0x0C, 0x00 } },
    { '8', { 0x0E, 0x1B, 0x1B, 0x0E, 0x1B, 0x1B, 0x0E, 0x00 } },
    { '9', { 0x0E, 0x1B, 0x1B, 0x0F, 0x03, 0x06, 0x0C, 0x00 } },
    { 'A', { 0x0E, 0x1B, 0x1B, 0x1F, 0x1B, 0x1B, 0x1B, 0x00 } },
    { 'C', { 0x0E, 0x1B, 0x18, 0x18, 0x18, 0x1B, 0x0E, 0x00 } },
    { 'D', { 0x1E, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1E, 0x00 } },
    { 'E', { 0x1F, 0x18, 0x18, 0x1E, 0x18, 0x18, 0x1F, 0x00 } },
    { 'H', { 0x1B, 0x1B, 0x1B, 0x1F, 0x1B, 0x1B, 0x1B, 0x00 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 } },
    { 'K', { 0x1B, 0x1B, 0x16, 0x1C, 0x16, 0x1B, 0x1B, 0x00 } },
    { 'L', { 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1F, 0x00 } },
    { 'P', { 0x1E, 0x1B, 0x1B, 0x1E, 0x18, 0x18, 0x18, 0x00 } },
    { 'S', { 0x0E, 0x1B, 0x18, 0x0E, 0x03, 0x1B, 0x0E, 0x00 } },
    { 'U', { 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x0E, 0x00 } },
    { 'Y', { 0x1B, 0x1B, 0x1B, 0x0E, 0x04, 0x04, 0x04, 0x00 } },
    { ':', { 0x00, 0x00, 0x0C, 0x00, 0x00, 0x0C, 0x00, 0x00 } },
    { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 } },
    { ',', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00 } },
    { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { 'x', { 0x00, 0x00, 0x1B, 0x0E, 0x0E, 0x1B, 0x00, 0x00 } },
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }
};

static int ensure_api(void) {
    HMODULE k;
    if (api_ready) return 1;
    wapi.LoadLibraryA = *(HMODULE (WINAPI **)(LPCSTR))0x005E663Cu;
    wapi.GetProcAddress = *(FARPROC (WINAPI **)(HMODULE, LPCSTR))0x005E65FCu;
    k = wapi.LoadLibraryA("kernel32.dll");
    if (!k) return 0;
    wapi.GetProcessHeap = (void *)wapi.GetProcAddress(k, "GetProcessHeap");
    wapi.HeapAlloc      = (void *)wapi.GetProcAddress(k, "HeapAlloc");
    wapi.HeapFree       = (void *)wapi.GetProcAddress(k, "HeapFree");
    wapi.DeleteFileA    = (void *)wapi.GetProcAddress(k, "DeleteFileA");
    wapi.GetTickCount   = (void *)wapi.GetProcAddress(k, "GetTickCount");
    wapi.Sleep          = (void *)wapi.GetProcAddress(k, "Sleep");
    if (!wapi.GetProcessHeap || !wapi.HeapAlloc || !wapi.HeapFree ||
        !wapi.DeleteFileA || !wapi.GetTickCount || !wapi.Sleep)
        return 0;
    proc_heap = wapi.GetProcessHeap();
    api_ready = (proc_heap != 0);
    return api_ready;
}

static void format_savegame_name(char *buf, int slot) {
    /* "SAVEGAME.%03d" */
    static const char prefix[] = "SAVEGAME.";
    int i;
    for (i = 0; i < 9; i++) buf[i] = prefix[i];
    buf[9]  = (char)('0' + (slot / 100) % 10);
    buf[10] = (char)('0' + (slot / 10) % 10);
    buf[11] = (char)('0' + (slot % 10));
    buf[12] = 0;
}

static void cleanup_snapshot_files(void) {
    char path[24];
    int i;
    if (!ensure_api()) return;
    for (i = 0; i < MAX_SNAPSHOTS; i++) {
        format_savegame_name(path, SNAP_BASE_SLOT + i);
        wapi.DeleteFileA(path);
        if (snapshots[i].do_list) {
            wapi.HeapFree(proc_heap, 0, snapshots[i].do_list);
            snapshots[i].do_list = 0;
        }
        snapshots[i].do_count = 0;
    }
    snapshot_count = 0;
}

static void mute_audio_for_seek(void) {
    if (!pb_audio_muted) {
        pb_saved_volume = *(volatile uint16_t *)ADDR_OPTIONS_VOLUME;
        pb_audio_muted = 1;
    }
    *(volatile uint16_t *)ADDR_OPTIONS_VOLUME = 0;
}

static void unmute_audio_after_seek(void) {
    if (pb_audio_muted) {
        *(volatile uint16_t *)ADDR_OPTIONS_VOLUME = pb_saved_volume;
        pb_audio_muted = 0;
    }
}

void ReplayControls_OnInit(void) {
    uint32_t total;
    ensure_api();
    cleanup_snapshot_files();
    pb_visible = 1;
    pb_paused = 0;
    pb_seeking = 0;
    pb_restoring_snap = 0;
    pb_saving_snap = 0;
    pb_at_end = 0;
    pb_speed_idx = 2;
    pb_step_frames = 0;
    pb_seek_target = -1;
    pb_pending_restore_slot = -1;
    pb_was_paused_before_seek = 0;
    pb_tick_rem_q8 = 0;
    pb_audio_muted = 0;
    pb_last_drawn_frame = -1;
    pb_last_drawn_paused = -1;
    pb_last_drawn_speed = -1;
    pb_last_drawn_visible = -1;
    saved_bg_valid = 0;
    seen_bar_valid = 0;
    scroll_tac_valid = 0;
    pal_initialized = 0;

    total = NativeReplay_GetTotalFrames();
    snap_interval = MIN_SNAP_INTERVAL;
    if (total > (uint32_t)(MAX_SNAPSHOTS - 2) * MIN_SNAP_INTERVAL) {
        snap_interval = (total + (uint32_t)(MAX_SNAPSHOTS - 3)) / (uint32_t)(MAX_SNAPSHOTS - 2);
        if (snap_interval < MIN_SNAP_INTERVAL) snap_interval = MIN_SNAP_INTERVAL;
    }
}

void ReplayControls_OnClose(void) {
    unmute_audio_after_seek();
    cleanup_snapshot_files();
    pb_visible = 1;
    pb_paused = 0;
    pb_seeking = 0;
    pb_at_end = 0;
    pb_seek_target = -1;
    pb_pending_restore_slot = -1;
    saved_bg_valid = 0;
    seen_bar_valid = 0;
    scroll_tac_valid = 0;
}

/* ---- Palette & Rendering Helpers ---- */
static uint8_t find_nearest_color(const uint8_t *pal6, int r, int g, int b) {
    int best_idx = 0;
    uint32_t best_dist = 0xFFFFFFFFu;
    int i;
    for (i = 0; i < 256; i++) {
        int pr = ((int)pal6[i * 3 + 0] << 2) | (pal6[i * 3 + 0] >> 4);
        int pg = ((int)pal6[i * 3 + 1] << 2) | (pal6[i * 3 + 1] >> 4);
        int pb = ((int)pal6[i * 3 + 2] << 2) | (pal6[i * 3 + 2] >> 4);
        int dr = pr - r;
        int dg = pg - g;
        int db = pb - b;
        uint32_t dist = (uint32_t)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return (uint8_t)best_idx;
}

static void rebuild_palette_luts(void) {
    const uint8_t *pal = *(const uint8_t * const *)ADDR_CURRENT_PALETTE;
    int i;
    if (!pal) pal = (const uint8_t *)ADDR_FALLBACK_PALETTE;
    if (pal_initialized && nr_memcmp(pal_cache, pal, 768) == 0) return;
    nr_memcpy(pal_cache, pal, 768);
    pal_initialized = 1;

    col_panel_border = find_nearest_color(pal, 106, 114, 128);
    col_status_play  = find_nearest_color(pal, 118, 224, 152);
    col_status_pause = find_nearest_color(pal, 245, 192, 92);
    col_text_time    = find_nearest_color(pal, 255, 255, 255);
    col_text_speed   = find_nearest_color(pal, 182, 190, 202);
    col_text_hint    = find_nearest_color(pal, 134, 144, 158);
    col_text_shadow  = find_nearest_color(pal, 10, 14, 20);
    col_seek_border  = find_nearest_color(pal, 78, 88, 102);
    col_seek_bg      = find_nearest_color(pal, 35, 43, 54);
    col_seek_fill    = find_nearest_color(pal, 90, 168, 245);
    col_thumb_fill   = find_nearest_color(pal, 255, 255, 255);
    col_thumb_border = find_nearest_color(pal, 24, 30, 40);

    for (i = 0; i < 256; i++) {
        int sr = ((int)pal[i * 3 + 0] << 2) | (pal[i * 3 + 0] >> 4);
        int sg = ((int)pal[i * 3 + 1] << 2) | (pal[i * 3 + 1] >> 4);
        int sb = ((int)pal[i * 3 + 2] << 2) | (pal[i * 3 + 2] >> 4);

        /* Panel background: 76% dark slate (14,18,24) + 24% underlying map */
        pal_lut_panel[i] = find_nearest_color(
            pal, (sr * 61 + 14 * 195) >> 8,
                 (sg * 61 + 18 * 195) >> 8,
                 (sb * 61 + 24 * 195) >> 8);
    }
}

static void compute_layout(ControlsLayout *out) {
    int sw = *(volatile int *)ADDR_SCREEN_WIDTH;
    int sh = *(volatile int *)ADDR_SCREEN_HEIGHT;
    int sidebar_open = (*(volatile uint8_t *)ADDR_MAP_SIDEBAR_FLAG) & 1;
    int view_w = sw;
    int bar_w, bar_x, bar_y;

    if (sw < 320 || sh < 200) {
        out->valid = 0;
        return;
    }
    if (sidebar_open) {
        int sb_x = *(volatile int *)ADDR_MAP_LEFT_STRIP_X - 16;
        if (sb_x >= 320 && sb_x <= sw) view_w = sb_x;
        else if (sw > 160) view_w = sw - 160;
    }

    bar_w = view_w - 16;
    if (bar_w > BAR_MAX_W) bar_w = BAR_MAX_W;
    if (bar_w < 300) bar_w = view_w - 8;
    if (bar_w < 240) bar_w = 240;

    bar_x = 8;
    if (bar_x + bar_w > view_w - 4) bar_x = (view_w - bar_w) / 2;
    if (bar_x < 2) bar_x = 2;

    bar_y = sh - BAR_H - 10;
    if (bar_y < 2) bar_y = 2;

    out->bar_x = bar_x;
    out->bar_y = bar_y;
    out->bar_w = bar_w;
    out->bar_h = BAR_H;
    out->seek_x = bar_x + 10;
    out->seek_y = bar_y + 24;
    out->seek_w = bar_w - 20;
    out->seek_h = 8;
    out->valid = 1;
}

static uint32_t get_clamped_display_frame(uint32_t total) {
    uint32_t f = *(volatile uint32_t *)ADDR_FRAME;
    if (total > 0 && f > total) f = total;
    return f;
}

static void put_pixel(uint8_t *buf, int stride, int w, int h, int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) {
        buf[y * stride + x] = c;
    }
}

static void fill_rect(uint8_t *buf, int stride, int w, int h, int x, int y, int rw, int rh, uint8_t c) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + rw) > w ? w : (x + rw);
    int y1 = (y + rh) > h ? h : (y + rh);
    int yy, xx;
    if (x0 >= x1 || y0 >= y1) return;
    for (yy = y0; yy < y1; yy++) {
        uint8_t *row = buf + yy * stride;
        for (xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

static void lut_rect(uint8_t *buf, int stride, int w, int h, int x, int y, int rw, int rh, const uint8_t *lut) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + rw) > w ? w : (x + rw);
    int y1 = (y + rh) > h ? h : (y + rh);
    int yy, xx;
    if (x0 >= x1 || y0 >= y1) return;
    for (yy = y0; yy < y1; yy++) {
        uint8_t *row = buf + yy * stride;
        for (xx = x0; xx < x1; xx++) row[xx] = lut[row[xx]];
    }
}

static void draw_rect_outline(uint8_t *buf, int stride, int w, int h, int x, int y, int rw, int rh, uint8_t c) {
    fill_rect(buf, stride, w, h, x, y, rw, 1, c);
    fill_rect(buf, stride, w, h, x, y + rh - 1, rw, 1, c);
    fill_rect(buf, stride, w, h, x, y, 1, rh, c);
    fill_rect(buf, stride, w, h, x + rw - 1, y, 1, rh, c);
}

static void draw_status_icon(uint8_t *buf, int stride, int w, int h, int x, int y, int paused, uint8_t col, uint8_t shadow) {
    if (paused) {
        fill_rect(buf, stride, w, h, x + 1, y + 1, 2, 7, shadow);
        fill_rect(buf, stride, w, h, x + 5, y + 1, 2, 7, shadow);
        fill_rect(buf, stride, w, h, x,     y,     2, 7, col);
        fill_rect(buf, stride, w, h, x + 4, y,     2, 7, col);
    } else {
        int c0;
        for (c0 = 0; c0 <= 3; c0++) {
            int span = 3 - c0;
            int yy;
            for (yy = (y + 3) - span; yy <= (y + 3) + span; yy++) {
                put_pixel(buf, stride, w, h, x + c0 + 1, yy + 1, shadow);
                put_pixel(buf, stride, w, h, x + c0,     yy,     col);
            }
        }
    }
}

static int measure_text(const char *s) {
    int w = 0;
    while (*s) {
        w += (*s == ':' || *s == '.' || *s == ',') ? 4 : (*s == ' ' ? 4 : 6);
        s++;
    }
    return w > 0 ? w - 1 : 0;
}

static void draw_text_5x8(uint8_t *buf, int stride, int w, int h, int x, int y, const char *s, uint8_t col, uint8_t shadow) {
    while (*s) {
        char ch = *s++;
        int g, row, c;
        const uint8_t *rows = 0;
        int narrow = (ch == ':' || ch == '.' || ch == ',');
        int advance = narrow ? 4 : (ch == ' ' ? 4 : 6);
        for (g = 0; g < (int)(sizeof(font_5x8) / sizeof(font_5x8[0])); g++) {
            if (font_5x8[g].ch == ch) { rows = font_5x8[g].rows; break; }
        }
        if (rows) {
            for (row = 0; row < 8; row++) {
                uint8_t bits = rows[row];
                for (c = 0; c < 5; c++) {
                    if (bits & (0x10 >> c)) {
                        int px = x + (narrow ? (c - 1) : c);
                        put_pixel(buf, stride, w, h, px + 1, y + row + 1, shadow);
                        put_pixel(buf, stride, w, h, px, y + row, col);
                    }
                }
            }
        }
        x += advance;
    }
}

static char *append_digits(char *p, uint32_t v, int min_digits) {
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v && n < 12);
    while (n < min_digits) tmp[n++] = '0';
    while (n) *p++ = tmp[--n];
    return p;
}

static char *append_time(char *p, uint32_t secs) {
    uint32_t hrs = secs / 3600u;
    uint32_t mins = (secs % 3600u) / 60u;
    uint32_t s = secs % 60u;
    if (hrs > 0) {
        p = append_digits(p, hrs, 1);
        *p++ = ':';
        p = append_digits(p, mins, 2);
    } else {
        p = append_digits(p, mins, 1);
    }
    *p++ = ':';
    p = append_digits(p, s, 2);
    return p;
}

static void copy_rect_to_saved(const uint8_t *buf, int stride, int w, int h, int rx, int ry, int rw, int rh) {
    int yy;
    saved_bg_valid = 0;
    if (rx < 0 || ry < 0 || rw <= 0 || rh <= 0 || rw > BAR_MAX_W || rh > BAR_H || rx + rw > w || ry + rh > h) return;
    for (yy = 0; yy < rh; yy++) {
        nr_memcpy(saved_bg + yy * rw, buf + (ry + yy) * stride + rx, (unsigned)rw);
    }
    saved_bg_x = rx;
    saved_bg_y = ry;
    saved_bg_w = rw;
    saved_bg_h = rh;
    saved_bg_sw = w;
    saved_bg_sh = h;
    saved_bg_valid = 1;
}

static void restore_saved_to_buf(uint8_t *buf, int stride, int w, int h) {
    int yy;
    if (!saved_bg_valid || w != saved_bg_sw || h != saved_bg_sh) {
        saved_bg_valid = 0;
        return;
    }
    if (saved_bg_x >= 0 && saved_bg_y >= 0 && saved_bg_x + saved_bg_w <= w && saved_bg_y + saved_bg_h <= h) {
        for (yy = 0; yy < saved_bg_h; yy++) {
            nr_memcpy(buf + (saved_bg_y + yy) * stride + saved_bg_x, saved_bg + yy * saved_bg_w, (unsigned)saved_bg_w);
        }
    }
    saved_bg_valid = 0;
}

static int lepton_to_px(int lep) {
    return ((lep * 24) + 128) >> 8;
}

void pb_on_pre_render(void) {
    GraphicViewPort *vp;
    int stride;
    if (NativeReplay_ModeValue != 2) return;
    if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) {
        saved_bg_valid = 0;
        scroll_tac_valid = 0;
        return;
    }

    scroll_pre_tac = *(volatile uint32_t *)ADDR_MAP_TACTICAL_COORD;
    scroll_tac_valid = 1;

    if (!saved_bg_valid) return;
    if (!pb_watcom_hidpage_lock()) return;
    vp = (GraphicViewPort *)ADDR_HIDPAGE;
    stride = vp->width + vp->x_add + vp->pitch;
    if (vp->offset && vp->width > 0 && vp->height > 0 && stride >= vp->width) {
        restore_saved_to_buf(vp->offset, stride, vp->width, vp->height);
    } else {
        saved_bg_valid = 0;
    }
    pb_watcom_hidpage_unlock();
}

/* Runs inside DisplayClass::Draw_It at 0x004B1E06 right after scroll blit and
   TacticalCoord update, before dirty terrain cells / objects / shroud redraw. */
void pb_on_draw_it_post_scroll(int complete) {
    uint32_t new_tac;
    int old_lx, old_ly, new_lx, new_ly;
    int dx, dy;
    int x0, y0, x1, y1, px, py;
    int sw, sh;

    if (NativeReplay_ModeValue != 2) return;
    if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) return;

    new_tac = *(volatile uint32_t *)ADDR_MAP_TACTICAL_COORD;
    if (complete != 0 || !seen_bar_valid || !scroll_tac_valid || new_tac == scroll_pre_tac) {
        scroll_pre_tac = new_tac;
        return;
    }

    old_lx = (int)(scroll_pre_tac & 0xFFFFu);
    old_ly = (int)((scroll_pre_tac >> 16) & 0xFFFFu);
    new_lx = (int)(new_tac & 0xFFFFu);
    new_ly = (int)((new_tac >> 16) & 0xFFFFu);

    dx = lepton_to_px(old_lx) - lepton_to_px(new_lx);
    dy = lepton_to_px(old_ly) - lepton_to_px(new_ly);

    sw = *(volatile int *)ADDR_SCREEN_WIDTH;
    sh = *(volatile int *)ADDR_SCREEN_HEIGHT;

    x0 = seen_bar_x + dx - 24;
    y0 = seen_bar_y + dy - 24;
    x1 = seen_bar_x + dx + seen_bar_w + 24;
    y1 = seen_bar_y + dy + seen_bar_h + 24;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= sw) x1 = sw - 1;
    if (y1 >= sh) y1 = sh - 1;

    for (py = y0; py <= y1; py += 12) {
        for (px = x0; px <= x1; px += 12) {
            pb_watcom_redraw_cell_at(px, py);
        }
    }

    scroll_pre_tac = new_tac;
}

void pb_on_render(void) {
    GraphicViewPort *vp;
    ControlsLayout L;
    uint8_t *buf;
    int stride, w, h;
    uint32_t total, cur_frame, fps;
    int fill_w, thumb_x;
    char time_buf[40];
    char *tp;

    if (NativeReplay_ModeValue != 2) return;
    if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) return;

    if (!pb_visible) {
        pb_last_drawn_visible = 0;
        seen_bar_valid = 0;
        saved_bg_valid = 0;
        return;
    }

    compute_layout(&L);
    if (!L.valid) return;

    if (!pb_watcom_hidpage_lock()) return;
    vp = (GraphicViewPort *)ADDR_HIDPAGE;
    buf = vp->offset;
    w = vp->width;
    h = vp->height;
    stride = vp->width + vp->x_add + vp->pitch;
    if (!buf || w <= 0 || h <= 0 || stride < w) {
        pb_watcom_hidpage_unlock();
        return;
    }

    rebuild_palette_luts();
    copy_rect_to_saved(buf, stride, w, h, L.bar_x, L.bar_y, L.bar_w, L.bar_h);

    total = NativeReplay_GetTotalFrames();
    cur_frame = get_clamped_display_frame(total);
    fps = NativeReplay_GetFps();

    /* 1. Outer translucent dark-slate panel + 1px slate border */
    draw_rect_outline(buf, stride, w, h, L.bar_x, L.bar_y, L.bar_w, L.bar_h, col_panel_border);
    lut_rect(buf, stride, w, h, L.bar_x + 1, L.bar_y + 1, L.bar_w - 2, L.bar_h - 2, pal_lut_panel);

    /* 2. Top row: status badge (left), subtle hotkey hints (center), time + speed (right) */
    {
        const char *status_str = pb_paused ? "PAUSED" : "PLAY";
        uint8_t status_col = pb_paused ? col_status_pause : col_status_play;
        int text_y = L.bar_y + 8;
        int status_x = L.bar_x + 10;
        int label_x = status_x + 11;
        int status_end = label_x + measure_text(status_str);

        const char *spd = speed_labels[pb_speed_idx];
        int spd_w = measure_text(spd);
        int spd_x = L.bar_x + L.bar_w - 10 - spd_w;
        int time_w;
        int time_x;

        tp = time_buf;
        tp = append_time(tp, cur_frame / fps);
        *tp++ = ' '; *tp++ = '/'; *tp++ = ' ';
        tp = append_time(tp, total / fps);
        *tp = 0;

        time_w = measure_text(time_buf);
        time_x = spd_x - 16 - time_w;

        draw_status_icon(buf, stride, w, h, status_x, text_y, pb_paused, status_col, col_text_shadow);
        draw_text_5x8(buf, stride, w, h, label_x, text_y, status_str, status_col, col_text_shadow);

        {
            const char *hint_full  = "SPC:PLAY  -/=:SPD  ,/.:SEEK  H:HIDE";
            const char *hint_short = "SPC -/= ,/. H";
            int free_left = status_end + 12;
            int free_right = time_x - 12;
            int free_w = free_right - free_left;
            int w_full = measure_text(hint_full);
            int w_short = measure_text(hint_short);
            if (free_w >= w_full) {
                int hx = free_left + (free_w - w_full) / 2;
                draw_text_5x8(buf, stride, w, h, hx, text_y, hint_full, col_text_hint, col_text_shadow);
            } else if (free_w >= w_short) {
                int hx = free_left + (free_w - w_short) / 2;
                draw_text_5x8(buf, stride, w, h, hx, text_y, hint_short, col_text_hint, col_text_shadow);
            }
        }

        if (time_x >= status_end + 8) {
            draw_text_5x8(buf, stride, w, h, time_x, text_y, time_buf, col_text_time, col_text_shadow);
        }
        if (spd_x >= status_end + 8) {
            draw_text_5x8(buf, stride, w, h, spd_x, text_y, spd, col_text_speed, col_text_shadow);
        }
    }

    /* 3. Bottom progress track */
    draw_rect_outline(buf, stride, w, h, L.seek_x, L.seek_y, L.seek_w, L.seek_h, col_seek_border);
    fill_rect(buf, stride, w, h, L.seek_x + 1, L.seek_y + 1, L.seek_w - 2, L.seek_h - 2, col_seek_bg);

    if (total > 0 && L.seek_w > 6) {
        int inner_w = L.seek_w - 2;
        fill_w = (int)muldiv_u32(cur_frame, (uint32_t)inner_w, total, 0u);
        if (fill_w < 0) fill_w = 0;
        if (fill_w > inner_w) fill_w = inner_w;
    } else {
        fill_w = 0;
    }

    if (fill_w > 0) {
        fill_rect(buf, stride, w, h, L.seek_x + 1, L.seek_y + 1, fill_w, L.seek_h - 2, col_seek_fill);
    }

    thumb_x = L.seek_x + 1 + fill_w - 2;
    if (thumb_x < L.seek_x) thumb_x = L.seek_x;
    if (thumb_x > L.seek_x + L.seek_w - 5) thumb_x = L.seek_x + L.seek_w - 5;

    draw_rect_outline(buf, stride, w, h, thumb_x, L.seek_y - 2, 5, L.seek_h + 4, col_thumb_border);
    fill_rect(buf, stride, w, h, thumb_x + 1, L.seek_y - 1, 3, L.seek_h + 2, col_thumb_fill);

    seen_bar_x = L.bar_x;
    seen_bar_y = L.bar_y;
    seen_bar_w = L.bar_w;
    seen_bar_h = L.bar_h;
    seen_bar_valid = 1;

    pb_last_drawn_frame = (int)cur_frame;
    pb_last_drawn_paused = pb_paused;
    pb_last_drawn_speed = pb_speed_idx;
    pb_last_drawn_visible = 1;

    pb_watcom_hidpage_unlock();
}

/* ---- Snapshot Keyframe Management ---- */
static void capture_snapshot_here(uint32_t frame) {
    int slot_idx;
    ReplaySnapshot *snap;
    char desc[16];
    uint32_t do_count, do_head;

    if (!ensure_api() || snapshot_count >= MAX_SNAPSHOTS) return;
    if (snapshot_count > 0 && frame <= snapshots[snapshot_count - 1].frame) return;

    slot_idx = snapshot_count;
    snap = &snapshots[slot_idx];
    nr_memset(snap, 0, sizeof(*snap));

    snap->frame = frame;
    NativeReplay_GetCursors(&snap->event_pos, &snap->check_pos, &snap->last_frame);
    snap->crypt_random = *(volatile uint32_t *)ADDR_CRYPT_RANDOM;
    snap->scen_random  = *(volatile uint32_t *)ADDR_SCEN_RANDOM;
    nr_memcpy(snap->mp_house_frames, (const void *)ADDR_MP_HOUSE_FRAMES, sizeof(snap->mp_house_frames));

    do_count = *(volatile uint32_t *)ADDR_DOLIST_COUNT;
    do_head  = *(volatile uint32_t *)ADDR_DOLIST_HEAD;
    snap->do_count = do_count;
    snap->do_head  = do_head;
    snap->do_tail  = *(volatile uint32_t *)ADDR_DOLIST_TAIL;
    if (do_count > 0 && do_count <= DOLIST_CAP) {
        snap->do_list = (uint8_t *)wapi.HeapAlloc(proc_heap, 0, do_count * EVENT_SIZE);
        if (!snap->do_list) return;
        {
            const uint8_t *base = (const uint8_t *)ADDR_DOLIST_ARRAY;
            uint32_t i;
            for (i = 0; i < do_count; i++) {
                uint32_t pos = (do_head + i) & (DOLIST_CAP - 1u);
                nr_memcpy(snap->do_list + i * EVENT_SIZE, base + pos * EVENT_SIZE, EVENT_SIZE);
            }
        }
    }

    format_savegame_name(desc, SNAP_BASE_SLOT + slot_idx);
    pb_saving_snap = 1;
    if (!pb_watcom_save_game(SNAP_BASE_SLOT + slot_idx, "REPLAY")) {
        pb_saving_snap = 0;
        if (snap->do_list) {
            wapi.HeapFree(proc_heap, 0, snap->do_list);
            snap->do_list = 0;
        }
        return;
    }
    pb_saving_snap = 0;
    snapshot_count++;
}

static int restore_snapshot_slot(int slot_idx) {
    ReplaySnapshot *snap;
    uint32_t saved_tactical;
    uint8_t saved_sidebar_bit;
    uint8_t cur_sidebar;

    if (slot_idx < 0 || slot_idx >= snapshot_count) return 0;
    snap = &snapshots[slot_idx];

    saved_tactical = *(volatile uint32_t *)ADDR_MAP_TACTICAL_COORD;
    saved_sidebar_bit = (*(volatile uint8_t *)ADDR_MAP_SIDEBAR_FLAG) & 1;

    pb_restoring_snap = 1;
    pb_watcom_load_game(SNAP_BASE_SLOT + slot_idx);
    pb_restoring_snap = 0;

    if (pb_audio_muted) {
        *(volatile uint16_t *)ADDR_OPTIONS_VOLUME = 0;
    }

    *(volatile uint32_t *)ADDR_FRAME        = snap->frame;
    *(volatile uint32_t *)ADDR_CRYPT_RANDOM = snap->crypt_random;
    *(volatile uint32_t *)ADDR_SCEN_RANDOM  = snap->scen_random;
    nr_memcpy((void *)ADDR_MP_HOUSE_FRAMES, snap->mp_house_frames, sizeof(snap->mp_house_frames));

    *(volatile uint32_t *)ADDR_OUTLIST_COUNT = 0;
    *(volatile uint32_t *)ADDR_OUTLIST_HEAD  = 0;
    *(volatile uint32_t *)ADDR_OUTLIST_TAIL  = 0;

    *(volatile uint32_t *)ADDR_DOLIST_COUNT = snap->do_count;
    if (snap->do_count > 0 && snap->do_list) {
        uint8_t *base = (uint8_t *)ADDR_DOLIST_ARRAY;
        nr_memcpy(base, snap->do_list, snap->do_count * EVENT_SIZE);
        *(volatile uint32_t *)ADDR_DOLIST_HEAD = 0;
        *(volatile uint32_t *)ADDR_DOLIST_TAIL = snap->do_count & (DOLIST_CAP - 1u);
    } else {
        *(volatile uint32_t *)ADDR_DOLIST_HEAD = snap->do_head;
        *(volatile uint32_t *)ADDR_DOLIST_TAIL = snap->do_tail;
    }

    NativeReplay_SetCursors(snap->event_pos, snap->check_pos, snap->last_frame);

    cur_sidebar = *(volatile uint8_t *)ADDR_MAP_SIDEBAR_FLAG;
    *(volatile uint8_t *)ADDR_MAP_SIDEBAR_FLAG = (uint8_t)((cur_sidebar & ~1u) | saved_sidebar_bit);
    if (saved_tactical) pb_watcom_set_tactical_pos(saved_tactical);
    saved_bg_valid = 0;
    seen_bar_valid = 0;
    scroll_tac_valid = 0;
    pb_watcom_flag_redraw(1);
    pb_at_end = 0;
    return 1;
}

static void start_seek_to(int target_frame) {
    int total = (int)NativeReplay_GetTotalFrames();
    int cur = (int)(*(volatile uint32_t *)ADDR_FRAME);
    int i, best;

    if (target_frame < 0) target_frame = 0;
    if (target_frame > total) target_frame = total;
    if (cur > total) cur = total;

    if (target_frame == cur && !pb_at_end) return;

    if (target_frame > cur && !pb_at_end) {
        pb_was_paused_before_seek = pb_paused;
        pb_seek_target = target_frame;
        pb_seeking = 1;
        pb_paused = 0;
        pb_step_frames = 0;
        mute_audio_for_seek();
        pb_last_seek_paint_ms = ensure_api() ? wapi.GetTickCount() : 0;
        return;
    }

    /* Backward seek (or restart from end): find highest keyframe <= target_frame */
    if (snapshot_count <= 0) return;
    best = 0;
    for (i = 0; i < snapshot_count; i++) {
        if ((int)snapshots[i].frame <= target_frame) best = i;
        else break;
    }

    pb_was_paused_before_seek = pb_paused;
    pb_seek_target = target_frame;
    pb_pending_restore_slot = best;
    pb_seeking = 1;
    pb_paused = 0;
    pb_step_frames = 0;
    pb_at_end = 0;
    mute_audio_for_seek();
    pb_last_seek_paint_ms = ensure_api() ? wapi.GetTickCount() : 0;
}

static void trigger_action(int act) {
    uint32_t total = NativeReplay_GetTotalFrames();
    uint32_t cur = get_clamped_display_frame(total);
    uint32_t fps = NativeReplay_GetFps();
    int jump = (int)(15u * fps);

    switch (act) {
        case PB_ACT_JUMP_START:
            if (cur > 0 || pb_at_end) start_seek_to(0);
            break;
        case PB_ACT_REWIND_15S:
            if (cur > 0 || pb_at_end) {
                int t = (int)cur - jump;
                start_seek_to(t < 0 ? 0 : t);
            }
            break;
        case PB_ACT_SLOWER:
            if (pb_speed_idx > 0) pb_speed_idx--;
            break;
        case PB_ACT_PLAY_PAUSE:
            if (pb_at_end) {
                pb_paused = 0;
                start_seek_to(0);
                pb_was_paused_before_seek = 0;
            } else {
                pb_paused = !pb_paused;
                pb_step_frames = 0;
            }
            break;
        case PB_ACT_FASTER:
            if (pb_speed_idx < 5) pb_speed_idx++;
            break;
        case PB_ACT_STEP_FWD:
            if (pb_at_end) break;
            if (pb_paused) {
                pb_step_frames = 1;
            } else {
                int t = (int)cur + jump;
                start_seek_to(t > (int)total ? (int)total : t);
            }
            break;
        case PB_ACT_JUMP_END:
            if (cur < total) start_seek_to((int)total);
            break;
        case PB_ACT_TOGGLE_VIS:
            pb_visible = !pb_visible;
            break;
    }
    pb_watcom_flag_redraw(0);
}

/* ---- Engine Lifecycle Hooks ---- */
void pb_on_main_loop_top(void) {
    uint32_t frame;
    if (NativeReplay_ModeValue != 2) return;
    if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) return;

    frame = *(volatile uint32_t *)ADDR_FRAME;
    if (pb_pending_restore_slot >= 0) return;

    if (!pb_at_end) {
        if (snapshot_count == 0 && frame == 0) {
            capture_snapshot_here(0);
        } else if (snapshot_count > 0 && snapshot_count < MAX_SNAPSHOTS) {
            uint32_t last_snap = snapshots[snapshot_count - 1].frame;
            if (frame > last_snap && (frame - last_snap) >= snap_interval) {
                capture_snapshot_here(frame);
            }
        }
    }
}

/* Returns: 0 = normal frame timer path; 1 = skip RenderMainLoop cleanly during fast seek */
int pb_on_main_loop_timer(void) {
    uint32_t base_delay;
    if (NativeReplay_ModeValue != 2) return 0;

    if (pb_seeking) {
        DWORD now;
        *(volatile uint32_t *)ADDR_FRAME_TIMER_DELAY = 0;
        now = ensure_api() ? wapi.GetTickCount() : 0;
        if (now - pb_last_seek_paint_ms >= 33u) {
            pb_last_seek_paint_ms = now;
            pb_watcom_keyboard_check();
            pb_watcom_flag_redraw(0);
            pb_watcom_render_sync_delay();
        }
        return 1; /* skip RenderMainLoop */
    }

    base_delay = *(volatile uint32_t *)ADDR_FRAME_TIMER_DELAY;
    if (pb_speed_idx != 2 && base_delay > 0) {
        uint32_t num_q8 = ((base_delay << 8) * 256u) / (uint32_t)speed_q8[pb_speed_idx] + pb_tick_rem_q8;
        uint32_t scaled = num_q8 >> 8;
        pb_tick_rem_q8 = num_q8 & 0xFFu;
        *(volatile uint32_t *)ADDR_FRAME_TIMER_DELAY = scaled;
    } else {
        pb_tick_rem_q8 = 0;
    }
    return 0;
}

/* Runs at Sync_Delay end (0x004A7C60). Returns 1 if pause/restore altered control flow, else 0. */
int pb_on_sync_delay_end(void) {
    if (NativeReplay_ModeValue != 2) return 0;
    if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) return 0;

    /* 1. Perform pending snapshot restore before entering next Main_Loop iteration */
    if (pb_pending_restore_slot >= 0) {
        int slot = pb_pending_restore_slot;
        pb_pending_restore_slot = -1;
        restore_snapshot_slot(slot);
        if ((int)(*(volatile uint32_t *)ADDR_FRAME) >= pb_seek_target) {
            pb_seeking = 0;
            pb_seek_target = -1;
            pb_paused = pb_was_paused_before_seek;
            unmute_audio_after_seek();
            pb_watcom_flag_redraw(1);
            pb_watcom_render_sync_delay();
        } else {
            return 1; /* Immediately return to Main_Game -> Main_Loop to advance fast */
        }
    }

    /* 2. Check forward seek completion */
    if (pb_seeking && pb_seek_target >= 0) {
        int cur = (int)(*(volatile uint32_t *)ADDR_FRAME);
        if (cur >= pb_seek_target || NativeReplay_IsAtEnd()) {
            pb_seeking = 0;
            pb_seek_target = -1;
            if (NativeReplay_IsAtEnd()) {
                pb_at_end = 1;
                pb_paused = 1;
            } else {
                pb_paused = pb_was_paused_before_seek;
            }
            unmute_audio_after_seek();
            pb_watcom_flag_redraw(1);
            pb_watcom_render_sync_delay();
        } else {
            return 1;
        }
    }

    /* 3. Stop cleanly at end of replay instead of abruptly exiting the window */
    if (NativeReplay_IsAtEnd() && !pb_at_end) {
        pb_at_end = 1;
        pb_paused = 1;
        pb_step_frames = 0;
        unmute_audio_after_seek();
        pb_watcom_flag_redraw(0);
    }

    /* 4. Frame step when paused */
    if (pb_step_frames > 0) {
        pb_step_frames--;
        if (!pb_at_end) return 0;
    }

    /* 5. Interactive Pause Loop */
    if (pb_paused) {
        DWORD last_paint = ensure_api() ? wapi.GetTickCount() : 0;
        pb_watcom_flag_redraw(0);
        pb_watcom_render_sync_delay();

        while (pb_paused && !pb_seeking && pb_pending_restore_slot < 0 && pb_step_frames == 0) {
            DWORD now;
            if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) break;
            if (*(volatile uint8_t *)ADDR_OPTIONS_DIALOG != 0) break;

            pb_watcom_call_back();
            pb_watcom_keyboard_check();

            if (pb_pending_restore_slot >= 0) {
                int slot = pb_pending_restore_slot;
                pb_pending_restore_slot = -1;
                restore_snapshot_slot(slot);
                if ((int)(*(volatile uint32_t *)ADDR_FRAME) >= pb_seek_target) {
                    pb_seeking = 0;
                    pb_seek_target = -1;
                    pb_paused = pb_was_paused_before_seek;
                    unmute_audio_after_seek();
                    pb_watcom_flag_redraw(1);
                    pb_watcom_render_sync_delay();
                } else {
                    return 1;
                }
            }

            if (pb_seeking || !pb_paused || pb_step_frames > 0) break;

            now = ensure_api() ? wapi.GetTickCount() : 0;
            if (now - last_paint >= 16u ||
                pb_paused != pb_last_drawn_paused ||
                pb_speed_idx != pb_last_drawn_speed ||
                pb_visible != pb_last_drawn_visible) {
                last_paint = now;
                pb_watcom_flag_redraw(0);
                pb_watcom_render_sync_delay();
            }

            if (ensure_api()) wapi.Sleep(10);
        }

        if (pb_step_frames > 0 && !pb_at_end) {
            pb_step_frames--;
        }
    }

    return 0;
}

/* Runs at 0x004CB093 right before Map.AI(key, mouseX, mouseY).
   Never steals mouse/keyboard from Map.AI; only flags bar redraw when state/frame changes. */
int pb_on_gscreen_input(short *key, int *mouse_x, int *mouse_y) {
    uint32_t cur_frame;
    (void)key;
    (void)mouse_x;
    (void)mouse_y;

    if (NativeReplay_ModeValue != 2) return 0;

    if (!pb_visible) {
        if (pb_last_drawn_visible == 1) {
            pb_watcom_flag_redraw(0);
        }
        return 0;
    }

    cur_frame = get_clamped_display_frame(NativeReplay_GetTotalFrames());
    if ((int)cur_frame != pb_last_drawn_frame ||
        pb_paused != pb_last_drawn_paused ||
        pb_speed_idx != pb_last_drawn_speed ||
        pb_visible != pb_last_drawn_visible) {
        pb_watcom_flag_redraw(0);
    }

    return 0;
}

/* Runs at 0x005B37D6 BEFORE WWKeyboardClass::Message_Handler.
   Returns 1 to swallow handled replay hotkeys from RA1, 0 to pass to RA1. */
int pb_on_wndproc(void *hwnd, unsigned msg, unsigned wparam, unsigned lparam) {
    (void)hwnd;

    if (NativeReplay_ModeValue != 2) return 0;
    if (*(volatile uint32_t *)ADDR_GAME_ACTIVE == 0) return 0;
    if (pb_saving_snap || pb_restoring_snap) return 0;
    if (*(volatile uint8_t *)ADDR_OPTIONS_DIALOG != 0) return 0;

    if (msg == WM_KEYDOWN) {
        int repeat = (lparam & 0x40000000u) != 0;
        if (wparam == VK_SPACE && !repeat) {
            trigger_action(PB_ACT_PLAY_PAUSE);
            return 1;
        }
        if ((wparam == 0xBD || wparam == 0xDB) && !repeat) { /* '-' or '[' */
            trigger_action(PB_ACT_SLOWER);
            return 1;
        }
        if ((wparam == 0xBB || wparam == 0xDD) && !repeat) { /* '=' or ']' */
            trigger_action(PB_ACT_FASTER);
            return 1;
        }
        if (wparam == 0xBC && !repeat) { /* ',' rewind 15s */
            trigger_action(PB_ACT_REWIND_15S);
            return 1;
        }
        if (wparam == 0xBE && (!repeat || pb_paused)) { /* '.' +15s playing, +1 frame paused */
            trigger_action(PB_ACT_STEP_FWD);
            return 1;
        }
        if (wparam == VK_HOME && !repeat) {
            trigger_action(PB_ACT_JUMP_START);
            return 1;
        }
        if (wparam == VK_END && !repeat) {
            trigger_action(PB_ACT_JUMP_END);
            return 1;
        }
        if (wparam == 'H' && !repeat) {
            trigger_action(PB_ACT_TOGGLE_VIS);
            return 1;
        }
        return 0;
    }

    if (msg == WM_CHAR) {
        if (wparam == ' ' || wparam == '-' || wparam == '=' || wparam == '+' ||
            wparam == '[' || wparam == ']' || wparam == '.' || wparam == ',' ||
            wparam == '<' || wparam == '>' || wparam == 'h' || wparam == 'H') {
            return 1;
        }
        return 0;
    }

    return 0;
}
