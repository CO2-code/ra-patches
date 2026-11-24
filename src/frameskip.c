#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "macros/patch.h"
#include "ra95.h"
#include "patch.h"

CLEAR(0x00529CD9, 0x90, 0x00529D19);
CALL(0x00529CD9, _RenderWaitForPlayers);

CLEAR(0x004A7C1C, 0x90, 0x004A7C5B);
CALL(0x004A7C1C, _RenderSyncDelay);

CLEAR(0x004A7E8D, 0x90, 0x004A7ECD);
CALL(0x004A7E8D, _RenderMainLoop);

uint32_t MaxFPS = 0;
int ReplayControlsPaused = 0;
extern int NativeReplay_ModeValue;

static bool LastFrameRendered = true;

static void Render()
{
    if (MaxFPS > 0)
    {
        static uint32_t nextTick;
        uint32_t curTick = timeGetTime();
        if (curTick < nextTick)
            return;
        nextTick = curTick + (1000 / MaxFPS);
    }

    short key = 0;
    int mouseY = 0;
    int mouseX = 0;

    WWMouseClass__Erase_Mouse(WWMouseClass__WWMouse, &GraphicsViewPortClass_HidPage, 1);
    GScreenClass__Input(MouseClass_Map, &key, &mouseX, &mouseY);
    if (key != 0)
    {
        if (NativeReplay_ModeValue == 2 && (key & 0xFF) == VK_F2)
        {
            ReplayControlsPaused = !ReplayControlsPaused;
            key = 0;
        }
        if (key != 0)
            Keyboard_Process(&key);
    }

    GScreenClass__Render(MouseClass_Map);

}

void ReplayControls_Pump()
{
    Render();
    Sleep(1);
}

//try to avoid rendering in the main_loop and rather let sync_delay and wait_for_players do it
//sync_delay will only skip the frame if there is not enough time left to render
//wait_for_players will only render if there is lag
void RenderMainLoop()
{
    //Don't render again if sync_delay or wait_for_players just did it in the previous frame
    //Make sure we never skip 2 frames in a row to keep it somewhat smooth
    if ((LastFrameRendered = !LastFrameRendered))
        Render();
}

void RenderSyncDelay()
{
    Render();
    LastFrameRendered = true;
}

void RenderWaitForPlayers()
{
    Render();
    LastFrameRendered = true;
}
