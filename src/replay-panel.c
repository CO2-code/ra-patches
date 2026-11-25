// RA2-style playback panel for native replay playback (NativeReplay_ModeValue == 2).
//
//  [|<] [<|] [<<] [||] [>>] [|>] [>|]        1:12 / 11:49   1.00x
//  [=====-----------------------------------------------------]
//
//  |<  restart          (not available: no checkpoints, disabled)
//  <|  step back        (not available: no checkpoints, disabled)
//  <<  slower           0.25x .. 16x
//  ||  pause / resume   (same state as the F2 hotkey)
//  >>  faster
//  |>  step one frame   (pauses first if needed)
//  >|  skip to end      (fastest speed, render throttled)
//
// Pacing: the engine's FrameTimer is zeroed in ReplayPanel_Frame (Queue_AI, before
// Sync_Delay) and the panel waits itself, so the speed multiplier works for both the
// skirmish/single-player pacing (Options.GameSpeed) and the multiplayer path where the
// engine otherwise runs playback unpaced.
//
// Drawing happens inside GScreenClass::Render on the hidden page, after the map and
// sidebar and before the blit (hook in replay-panel-hooks.asm), so the panel is repainted
// every Render pass and never leaves ghosts. Only two palette indices (COL_BG/COL_FG) are used; disabled buttons are drawn dithered.

#include <windows.h>
#include <stdint.h>
#include "macros/patch.h"
#include "ra95.h"
#include "patch.h"

extern int NativeReplay_ModeValue;
extern unsigned NativeReplay_TotalFrames(void);
extern int ReplayControlsVisible;
extern int ReplayControlsPaused;
extern uint16_t KeyReplayControls; // [WinHotkeys] KeyReplayControls (VK code, default F1)
extern uint16_t KeyReplayPause;    // [WinHotkeys] KeyReplayPause    (VK code, default F2)
extern uint32_t MaxFPS;
void ReplayControls_Pump(void);
void ReplayPanel_FillRect(int x1, int y1, int x2, int y2, int color); // replay-panel-hooks.asm, x2/y2 inclusive

#define GAME_ACTIVE      (*(volatile int *)0x00669924)
#define GAME_IN_FOCUS    (*(volatile int *)0x00665F64)   // GameInFocus, as tested by Main_Loop
#define GAME_FRAME       (*(volatile uint32_t *)0x006680C4)
#define COMM_PROTOCOL    (*(volatile uint8_t *)0x0067F2B5)   // 2 == multiplayer lockstep
#define DESIRED_FPS      (*(volatile int *)0x0067F32D)
#define FRAME_TIMER_DUR  (*(volatile uint32_t *)0x006807FD)  // FrameTimer duration in 1/60 s ticks

#define TICKS_PER_SECOND 15       // game frames per game-second, as in DEFINES.H
#define TIMER_HZ         60

// Palette indices (change here): COL_BG = panel fill / lit-button glyph, COL_FG = borders, glyphs, bar.
#define COL_BG 5
#define COL_FG 7

#define PANEL_W   392
#define PANEL_H   48
#define BTN_COUNT 7
#define BTN_W     28
#define BTN_H     22
#define BTN_GAP   4

enum { BTN_RESTART, BTN_STEP_BACK, BTN_SLOWER, BTN_PAUSE, BTN_FASTER, BTN_STEP, BTN_END };

static const uint16_t SpeedX100[] = { 25, 50, 100, 200, 400, 800, 1600, 0 }; // 0 == unlimited
#define SPEED_COUNT   8
#define SPEED_NORMAL  2
#define SPEED_MAX     (SPEED_COUNT - 1)

static int speedIdx = SPEED_NORMAL;
static int stepRequested;
static int hoverBtn = -1;
static int pressedBtn = -1;
static int mouseWasDown;
static int controlsKeyWasDown;
static int pauseKeyWasDown;
static int lastVisible = 1;
static uint32_t lastSignature = 0xFFFFFFFF;
static uint32_t nextMs;
static uint32_t nextFrac;
static int resync = 1;
static uint32_t savedMaxFPS;
static int maxFpsSaved;

// ---- helpers --------------------------------------------------------------

static void RequestScreenUpdate(int fullRedraw)
{
    MouseClass_Map[0] |= fullRedraw ? 3 : 2; // bit0 IsToRedraw, bit1 IsToUpdate
}

static int KeyDown(unsigned vk)
{
    return vk && vk < 256 && GAME_IN_FOCUS && (GetAsyncKeyState((int)vk) & 0x8000) != 0;
}

static int ButtonEnabled(int b)
{
    return b != BTN_RESTART && b != BTN_STEP_BACK;
}

static void PanelOrigin(int *x, int *y)
{
    int width = SideBarPanelLeftPosX > 0 ? SideBarPanelLeftPosX : ScreenWidth;
    *x = (width - PANEL_W) / 2;
    if (*x < 0) *x = 0;
    *y = ScreenHeight - PANEL_H - 6;
}

static void ButtonRect(int b, int *x, int *y)
{
    int px, py;
    PanelOrigin(&px, &py);
    *x = px + 8 + b * (BTN_W + BTN_GAP);
    *y = py + 6;
}

static int HitTest(int mx, int my)
{
    for (int b = 0; b < BTN_COUNT; b++)
    {
        int x, y;
        ButtonRect(b, &x, &y);
        if (mx >= x && mx < x + BTN_W && my >= y && my < y + BTN_H)
            return b;
    }
    return -1;
}

static void TimeText(char *out, unsigned frames)
{
    unsigned s = frames / TICKS_PER_SECOND;
    unsigned m = s / 60;
    s %= 60;
    char tmp[12];
    int n = 0, o = 0;
    do { tmp[n++] = '0' + m % 10; m /= 10; } while (m);
    while (n) out[o++] = tmp[--n];
    out[o++] = ':';
    out[o++] = '0' + s / 10;
    out[o++] = '0' + s % 10;
    out[o] = 0;
}

static void Append(char *dst, const char *src)
{
    while (*dst) dst++;
    while ((*dst++ = *src++)) {}
}

static void SpeedText(char *out)
{
    unsigned v = SpeedX100[speedIdx];
    if (!v) { out[0] = 'M'; out[1] = 'A'; out[2] = 'X'; out[3] = 0; return; }
    int o = 0;
    if (v >= 1000) out[o++] = '0' + v / 1000;
    out[o++] = '0' + (v / 100) % 10;
    out[o++] = '.';
    out[o++] = '0' + (v / 10) % 10;
    out[o++] = '0' + v % 10;
    out[o++] = 'x';
    out[o] = 0;
}

// ---- input ----------------------------------------------------------------

static void Activate(int b)
{
    switch (b)
    {
    case BTN_SLOWER:
        if (speedIdx > 0) speedIdx--;
        resync = 1;
        break;
    case BTN_FASTER:
        if (speedIdx < SPEED_MAX) speedIdx++;
        resync = 1;
        break;
    case BTN_PAUSE:
        ReplayControlsPaused = !ReplayControlsPaused;
        break;
    case BTN_STEP:
        ReplayControlsPaused = 1;
        stepRequested = 1;
        break;
    case BTN_END:
        speedIdx = SPEED_MAX;
        ReplayControlsPaused = 0;
        resync = 1;
        break;
    default:
        break;
    }
}

// Called from Render() every pass. Polls the left mouse button because the panel is
// not a game gadget; clicks only count while the game has focus (GameInFocus).
void ReplayPanel_Poll(void)
{
    if (NativeReplay_ModeValue != 2) return;

    // The only place the replay hotkeys are handled (edge-triggered, works while paused too).
    int controlsDown = KeyDown(KeyReplayControls);
    int pauseDown = KeyDown(KeyReplayPause);
    if (controlsDown && !controlsKeyWasDown) ReplayControlsVisible = !ReplayControlsVisible;
    if (pauseDown && !pauseKeyWasDown) ReplayControlsPaused = !ReplayControlsPaused;
    controlsKeyWasDown = controlsDown;
    pauseKeyWasDown = pauseDown;

    int visible = ReplayControlsVisible != 0;
    if (visible != lastVisible)
    {
        lastVisible = visible;
        RequestScreenUpdate(1); // repaint the map under the panel
    }
    if (!visible) { hoverBtn = pressedBtn = -1; mouseWasDown = 0; return; }

    int mx = Get_Mouse_X();
    int my = Get_Mouse_Y();
    int hover = HitTest(mx, my);
    int down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && GAME_IN_FOCUS != 0;

    if (down && !mouseWasDown && hover >= 0 && ButtonEnabled(hover))
        pressedBtn = hover;
    if (!down && mouseWasDown)
    {
        if (pressedBtn >= 0 && pressedBtn == hover)
            Activate(pressedBtn);
        pressedBtn = -1;
    }
    mouseWasDown = down;
    hoverBtn = hover;

    // Repaint when anything visible changed (the map may not be dirty, e.g. while paused).
    unsigned total = NativeReplay_TotalFrames();
    unsigned bar = total ? (GAME_FRAME * 1024u) / total : 0; // 32-bit only: the build has no libgcc
    uint32_t sig = (GAME_FRAME / TICKS_PER_SECOND) * 31u + bar * 7u + (hoverBtn + 2) * 1009u +
                   (pressedBtn + 2) * 4001u + speedIdx * 17u + (ReplayControlsPaused ? 100003u : 0);
    if (sig != lastSignature)
    {
        lastSignature = sig;
        RequestScreenUpdate(0);
    }
}

// ---- drawing --------------------------------------------------------------

static void Rect(int x, int y, int w, int h, int color, int dim)
{
    if (w <= 0 || h <= 0) return;
    if (!dim) { ReplayPanel_FillRect(x, y, x + w - 1, y + h - 1, color); return; }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (((x + i + y + j) & 1) == 0)
                ReplayPanel_FillRect(x + i, y + j, x + i, y + j, color);
}

static void Frame1(int x, int y, int w, int h, int color)
{
    Rect(x, y, w, 1, color, 0);
    Rect(x, y + h - 1, w, 1, color, 0);
    Rect(x, y, 1, h, color, 0);
    Rect(x + w - 1, y, 1, h, color, 0);
}

// Triangle in a w*h box, pointing right (dir > 0) or left.
static void Triangle(int x, int y, int w, int h, int dir, int color, int dim)
{
    for (int c = 0; c < w; c++)
    {
        int inset = (c * h) / (2 * w);
        int col = dir > 0 ? x + c : x + w - 1 - c;
        Rect(col, y + inset, 1, h - 2 * inset, color, dim);
    }
}

static void Glyph(int b, int x, int y, int color, int dim)
{
    const int h = 12, tw = 6;
    int gy = y + (BTN_H - h) / 2;
    int cx = x + BTN_W / 2;
    switch (b)
    {
    case BTN_RESTART:   Rect(cx - 6, gy, 2, h, color, dim); Triangle(cx - 3, gy, 8, h, -1, color, dim); break;
    case BTN_STEP_BACK: Triangle(cx - 5, gy, 8, h, -1, color, dim); Rect(cx + 4, gy, 2, h, color, dim); break;
    case BTN_SLOWER:    Triangle(cx - 7, gy, tw, h, -1, color, dim); Triangle(cx - 1, gy, tw, h, -1, color, dim); break;
    case BTN_PAUSE:
        if (ReplayControlsPaused) Triangle(cx - 4, gy, 9, h, 1, color, dim);
        else { Rect(cx - 5, gy, 3, h, color, dim); Rect(cx + 2, gy, 3, h, color, dim); }
        break;
    case BTN_FASTER:    Triangle(cx - 7, gy, tw, h, 1, color, dim); Triangle(cx - 1, gy, tw, h, 1, color, dim); break;
    case BTN_STEP:      Rect(cx - 6, gy, 2, h, color, dim); Triangle(cx - 3, gy, 8, h, 1, color, dim); break;
    case BTN_END:       Triangle(cx - 5, gy, 8, h, 1, color, dim); Rect(cx + 4, gy, 2, h, color, dim); break;
    }
}

// Called from GScreenClass::Render with the hidden page as the logic page.
void ReplayPanel_Draw(void)
{
    if (NativeReplay_ModeValue != 2 || !ReplayControlsVisible) return;

    int px, py;
    PanelOrigin(&px, &py);

    Rect(px, py, PANEL_W, PANEL_H, COL_BG, 0);
    Frame1(px, py, PANEL_W, PANEL_H, COL_FG);

    for (int b = 0; b < BTN_COUNT; b++)
    {
        int x, y;
        ButtonRect(b, &x, &y);
        int enabled = ButtonEnabled(b);
        int lit = enabled && (b == pressedBtn || (b == hoverBtn && pressedBtn < 0));
        Rect(x, y, BTN_W, BTN_H, lit ? COL_FG : COL_BG, 0);
        Frame1(x, y, BTN_W, BTN_H, COL_FG);
        Glyph(b, x, y, lit ? COL_BG : COL_FG, !enabled);
    }

    char cur[16], tot[16], text[40], speed[8];
    unsigned total = NativeReplay_TotalFrames();
    TimeText(cur, GAME_FRAME);
    TimeText(tot, total);
    text[0] = 0;
    Append(text, cur);
    Append(text, " / ");
    Append(text, tot);
    SpeedText(speed);

    int textY = py + 9;
    Fancy_Text_Print(text, px + 244, textY, (void *)0x00666E8A, 0x0C, 0x4046);
    Fancy_Text_Print(speed, px + PANEL_W - 8, textY, (void *)0x00666E8A, 0x0C, 0x4216);

    // Progress bar (display only: replays can't seek without checkpoints).
    int sx = px + 8, sy = py + 34, sw = PANEL_W - 16, sh = 8;
    Frame1(sx, sy, sw, sh, COL_FG);
    if (total)
    {
        uint32_t f = GAME_FRAME > total ? total : GAME_FRAME;
        int fill = (int)((f * (uint32_t)(sw - 4)) / total);
        Rect(sx + 2, sy + 2, fill, sh - 4, COL_FG, 0);
    }
}

// ---- frame control --------------------------------------------------------

static void ApplyRenderThrottle(void)
{
    if (!maxFpsSaved) { savedMaxFPS = MaxFPS; maxFpsSaved = 1; }
    if (speedIdx >= 4) // 4x and above: don't spend the time rendering every frame
        MaxFPS = (savedMaxFPS == 0 || savedMaxFPS > 30) ? 30 : savedMaxFPS;
    else
        MaxFPS = savedMaxFPS;
}

// Called once per simulation frame from the Queue_AI hook, after Main_Loop set FrameTimer
// and before Sync_Delay. Handles pause/step, then paces the frame itself.
void ReplayPanel_Frame(void)
{
    if (GAME_FRAME == 0)
    {
        speedIdx = SPEED_NORMAL;
        stepRequested = 0;
        pressedBtn = hoverBtn = -1;
        lastSignature = 0xFFFFFFFF;
        resync = 1;
    }

    // Base frame length in 1/60 s ticks, then take the wait away from the engine.
    uint32_t ticks;
    if (COMM_PROTOCOL == 2)
    {
        int rate = DESIRED_FPS;
        if (rate <= 0) rate = TIMER_HZ;
        ticks = TIMER_HZ / rate;
    }
    else
    {
        ticks = FRAME_TIMER_DUR;
    }
    FRAME_TIMER_DUR = 0;

    while (ReplayControlsPaused && GAME_ACTIVE)
    {
        if (stepRequested) { stepRequested = 0; break; }
        resync = 1;
        ReplayControls_Pump();
    }
    if (!GAME_ACTIVE) return;

    ApplyRenderThrottle();

    unsigned speed = SpeedX100[speedIdx];
    if (!speed || !ticks) { resync = 1; return; } // unlimited

    // interval in microseconds: ticks * (1e6/60) / (speed/100)
    uint32_t intervalUs = (ticks * 16667u * 100u) / speed;
    uint32_t now = timeGetTime();
    if (resync || (int32_t)(now - nextMs) > 200)
    {
        nextMs = now;
        nextFrac = 0;
        resync = 0;
    }
    nextMs += intervalUs / 1000;
    nextFrac += intervalUs % 1000;
    if (nextFrac >= 1000) { nextFrac -= 1000; nextMs++; }

    while ((int32_t)(timeGetTime() - nextMs) < 0 && GAME_ACTIVE && !ReplayControlsPaused)
    {
        if ((int32_t)(nextMs - timeGetTime()) > 2000) break; // never stall on a bad interval
        ReplayControls_Pump();
    }
}
